#include "mem/cxlsim_gem5.hh"

#include "base/callback.hh"
#include "base/trace.hh"
#include "debug/CXLSimGem5.hh"
#include "debug/Drain.hh"
#include "sim/system.hh"

#include "CXLStats.h"

// spdlog collides with gem5...
#pragma push_macro("warn")
#undef warn

namespace CXL{
    extern CXLStats stats;
}

namespace gem5
{

namespace memory
{

CXLSimGem5::CXLSimGem5(const Params &p) :
    AbstractMemory(p),
    port(name() + ".port", *this),
    config_path(p.config_path),
    retryReq(false), retryResp(false), startTick(0),
    nbrOutstandingReads(0), nbrOutstandingWrites(0),
    sendResponseEvent([this]{ sendResponse(); }, name()),
    tickEvent([this]{ tick(); }, name()), record(p.record), num_reads(0), num_writes(0), req_id(0), record_file("record_cxlsim.dat"),
    cxl_accesses(0), dam_accesses(0), cxl_accesses_roi(0), dam_accesses_roi(0), all_cxl(p.all_cxl), all_dam(p.all_dam)
{
    DPRINTF(CXLSimGem5, "Instantiated CXLSimGem5 \n");

    // Instantiate the memory model
    mem_model = std::make_unique<CXL::CXLWrapper>(config_path);
    // Configure parameters
    CXL::params.ticks_per_ns = static_cast<int64_t>(sim_clock::as_float::ns);
    CXL::params.ticks_per_ins = 1 * static_cast<int64_t>(sim_clock::as_float::ns);
    CXL::params.ramulator_update_delay_ns = static_cast<int64_t>(p.tck*CXL::params.ticks_per_ns);

    // Add clocks
    mem_model->add_clk(static_cast<int64_t>(CXL::params.ticks_per_ins));
    mem_model->add_clk(static_cast<int64_t>(CXL::params.ramulator_update_delay_ns));

    // Record data written to and read from memory
    if (record)
        record_file_ptr.open(record_file);

    // Note down the callback order
    // std::ofstream f("callback_order.trace");
    // f.close();

    // Make sure that all_cxl and all_dam are not set at the same time
    panic_if(all_cxl & all_dam, "Both all_cxl and all_dam are set to true");

    registerExitCallback([this]() {
        std::cout<<"Finished CXL Simulation\n";    
        std::cout<<"NUM READS       : "<<num_reads<<"\n";
        std::cout<<"NUM WRITES      : "<<num_writes<<"\n";
        std::cout<<"NUM CXL         : "<<cxl_accesses<<"\n";
        std::cout<<"NUM DAM         : "<<dam_accesses<<"\n";
        std::cout<<"NUM CXL in ROI  : "<<cxl_accesses_roi<<"\n";
        std::cout<<"NUM DAM in ROI  : "<<dam_accesses_roi<<"\n";
        std::cout<<"Stats from inside CXLSIM\n";
        std::cout<<CXL::stats.sprint();    
        // Close the record file
        if (record)
            record_file_ptr.close();
        // Print out all accessed special regions
        for (auto &v: region_access_counts){
            std::cout<<v.first<<" : "<<v.second<<"\n";
        }
        if(nbrOutstanding()!=0)
            std::cout<< nbrOutstanding() <<" requests remaining in buffers"<<std::endl;
    });
}

void
CXLSimGem5::init()
{
    AbstractMemory::init();

    if (!port.isConnected()) {
        fatal("CXLSimGem5 %s is unconnected!\n", name());
    } else {
        port.sendRangeChange();
    }

    // if (system()->cacheLineSize() != wrapper.burstSize())
    //     fatal("CXLSimGem5 burst size %d does not match cache line size %d\n",
    //           wrapper.burstSize(), system()->cacheLineSize());
}

void
CXLSimGem5::startup()
{
    startTick = curTick();

    // kick off the clock ticks
    schedule(tickEvent, clockEdge());
}

void
CXLSimGem5::resetStats() {
    // wrapper.resetStats();
}

void
CXLSimGem5::sendResponse()
{
    assert(!retryResp);
    assert(!responseQueue.empty());

    DPRINTF(CXLSimGem5, "Attempting to send response for %lx\n", responseQueue.front()->getAddr());

    bool success = port.sendTimingResp(responseQueue.front());
    if (success) {
        // Note down the callback order
        auto pkt = responseQueue.front();
        // if(record)
        //     record_file_ptr<<"R,"<<std::hex<<pkt->getAddr()<<","<<(pkt->isRead()?"R":"W")<<"\n";
        DPRINTF(CXLSimGem5, "Sent response for %lx\n", responseQueue.front()->getAddr());
        responseQueue.pop_front();

        DPRINTF(CXLSimGem5, "Have %d read, %d write, %d responses outstanding\n",
                nbrOutstandingReads, nbrOutstandingWrites,
                responseQueue.size());

        if (!responseQueue.empty() && !sendResponseEvent.scheduled())
            schedule(sendResponseEvent, curTick());

        if (nbrOutstanding() == 0)
            signalDrainDone();

    } else {
        retryResp = true;

        DPRINTF(CXLSimGem5, "Waiting for response retry\n");

        assert(!sendResponseEvent.scheduled());
    }
}

unsigned int
CXLSimGem5::nbrOutstanding() const
{
    // return nbrOutstandingReads + nbrOutstandingWrites + responseQueue.size();
    return pendingRequests.size() + responseQueue.size();
}

void
CXLSimGem5::tick()
{
    // Only tick when it's timing mode
    if (system()->isTimingMode()) {
        // Set current clock tick
        CXL::curr_tick = curTick();
        mem_model->sys->update();
        mem_model->sys->hosts[0].update_DRAM();

        // is the connected port waiting for a retry, if so check the
        // state and send a retry if conditions have changed
        if (retryReq) {
            retryReq = false;
            port.sendRetryReq();
        }
    }

    Tick next_tick = mem_model->find_next_tick(curTick());
    // std::cout<<"Schedule next tick @"<<next_tick<<std::endl;
    schedule(tickEvent, next_tick);
}

Tick
CXLSimGem5::recvAtomic(PacketPtr pkt)
{
    panic_if(pkt->cacheResponding(), "Should not see packets where cache "
             "is responding");

    access(pkt);
    return 50000;   // Arbitary latency of 50ns
}

void
CXLSimGem5::recvFunctional(PacketPtr pkt)
{
    pkt->pushLabel(name());
    functionalAccess(pkt);

    for (auto i = responseQueue.begin(); i != responseQueue.end(); ++i)
        pkt->trySatisfyFunctional(*i);

    pkt->popLabel();
}

bool
CXLSimGem5::recvTimingReq(PacketPtr pkt)
{
    DPRINTF(CXLSimGem5, "recvTimingReq: request %s addr %#x size %d\n",
            pkt->cmdString(), pkt->getAddr(), pkt->getSize());

    panic_if(pkt->cacheResponding(), "Should not see packets where cache "
             "is responding");

    panic_if(!(pkt->isRead() || pkt->isWrite()),
             "Should only see read and writes at memory controller, "
             "saw %s to %#llx\n", pkt->cmdString(), pkt->getAddr());

    // we should not get a new request after committing to retry the
    // current one, but unfortunately the CPU violates this rule, so
    // simply ignore it for now
    if (retryReq)
        return false;

    /**
     * How we decide whether to send access to CXL or DAM
     * Setting the is_cxl_access variable means the access will goto CRAM and not DAM
     * If all_dam is set, then is_cxl_access should always be false
     * If all_cxl is set, then is_cxl_access should always be true
     * If neither of all_dam or all_cxl is set,
     * 1. Find out if any special regions are accessed
     * 2. Check if this region is present in the mapped_regions
     * 3. If it is in mapped regions, it is a CXL access, if not it will be a DAM access
     */
     
    //Get address and opcode
    uint64_t addr = pkt->getAddr(), id = req_id;
    CXL::opcode op = pkt->isRead() ? CXL::opcode::Req : CXL::opcode::RwD;
    //Instantiate is_cxl_access
    bool is_cxl_access = false;
    //Variable to store accessed regions
    std::vector<size_t> accessed_regions;
    //Check if all_dam is set
    if(all_dam)
    {
        is_cxl_access = false;
    }
    else if(all_cxl)
    {
        is_cxl_access = true;
    }
    else
    {
        accessed_regions = find_accessed_region(pkt->req->hasVaddr()?((pkt->req->getVaddr()>>6)<<6):0,pkt->req->getSize());
        auto mapped_regions = system()->getMappedRegions();
        for (auto &v: accessed_regions)
        {
            if (mapped_regions->find(v) == mapped_regions->end())
                continue;
            else {
                is_cxl_access = true;
                break;
            }
        }
    }
    // if(accessed_regions.size()>0)
    //     std::cout<<(pkt->req->hasVaddr()?((pkt->req->getVaddr()>>6)<<6):0)<<std::endl;
    // See if any accessed region is part of mapped region, if so it should goto CXL

    DPRINTF(CXLSimGem5, "Rcvd req %s,%lu,%#lx\n", pkt->isRead()?"R":"W", id, addr);

    bool enqueue_success = false;
    if (pkt->isRead()) 
    {
        // accessAndRespond(pkt);
        // Generate ramulator READ request and try to send to ramulator's memory system
        enqueue_success = mem_model->add_external_req(addr, op, id, 
            [this, addr](uint64_t rid) {
                // std::cout<<"Callback for ID:" << rid << std::endl;
                panic_if(pendingRequests.find(rid) == pendingRequests.end(), "Request %lu not found in pendingRequests @%lu\n", rid, curTick());
                PacketPtr pkt = pendingRequests.find(rid)->second;
                // std::cout<<"Retrieved Pkt id "<<pkt->id<<std::endl;
                panic_if(!pkt->isValidAddr(), "Valid addr flag is not set for pkt %lu", pkt->id);
                panic_if(addr != pkt->getAddr(), "Captured address %#lx and packet address %#lx do not match\n", addr, pkt->getAddr());
                DPRINTF(CXLSimGem5, "Callback for ID: %lu, Addr: %#lx\n", rid, pkt->getAddr());

                // added counter to track requests in flight
                // --nbrOutstandingReads;

                accessAndRespond(pkt);

                // Note down the callback order
                // if(record)
                //     record_file_ptr<<"C,"<<std::hex<<pkt->getAddr()<<","<<(pkt->isRead()?"R":"W")<<"\n";

                // Delete packet from pending requests
                pendingRequests.erase(rid);
                panic_if(pendingRequests.find(rid) != pendingRequests.end(), "Pkt found after deletion\n");
                DPRINTF(CXLSimGem5, "Read for ID: %lu, Addr: %#lx completed\n", rid, pkt->getAddr());

            }, is_cxl_access);

        if (enqueue_success) 
        {
            DPRINTF(CXLSimGem5, "Added read id %lu pkt id %lu addr %#lx to mem_model\n", id, pkt->id, pkt->getAddr());
            // outstandingReads[pkt->getAddr()].push_back(pkt);
            panic_if(pendingRequests.find(id) != pendingRequests.end(), "Request already exists in pendingRequests\n");
            pendingRequests.insert({id,pkt});
            PacketPtr p = pendingRequests.find(id)->second;
            DPRINTF(CXLSimGem5, "Verification read id %lu pkt id %lu addr %#lx\n", id, p->id, p->getAddr());
            // we count a transaction as outstanding until it has left the
            // queue in the controller, and the response has been sent
            // back, note that this will differ for reads and writes
            // ++nbrOutstandingReads;
            num_reads++;
        } 
        else 
        {
            retryReq = true;
        }
    } else if (pkt->isWrite()) {
        
        // accessAndRespond(pkt);
        // Generate ramulator READ request and try to send to ramulator's memory system
        enqueue_success = mem_model->add_external_req(addr, op, id, 
            [this, addr](uint64_t rid) {
                // std::cout<<"Callback for ID:" << rid << std::endl;
                panic_if(pendingRequests.find(rid) == pendingRequests.end(), "Request %lu not found in pendingRequests\n", rid);
                PacketPtr pkt = pendingRequests.find(rid)->second;
                // std::cout<<"Retrieved Pkt id "<<pkt->id<<std::endl;
                // panic_if(!pkt->isValidAddr(), "Valid addr flag is not set for pkt %lu", pkt->id);
                // panic_if(addr != pkt->getAddr(), "Captured address %#lx and packet address %#lx do not match\n", addr, pkt->getAddr());
                DPRINTF(CXLSimGem5, "Callback for ID: %lu, Addr: %#lx\n", rid, addr);
                // DPRINTF(CXLSimGem5, "Callback for ID: %lu, Addr: %#lx\n", rid, pkt->getAddr());

                // added counter to track requests in flight
                // --nbrOutstandingReads;


                // Note down the callback order
                // record_file_ptr<<"C,"<<std::hex<<pkt->getAddr()<<","<<(pkt->isRead()?"R":"W")<<"\n";              

                // Delete packet from pending requests
                pendingRequests.erase(rid);
                panic_if(pendingRequests.find(rid) != pendingRequests.end(), "Pkt found after deletion\n");
                DPRINTF(CXLSimGem5, "Write for ID: %lu, Addr: %#lx completed\n", rid, addr);
                // DPRINTF(CXLSimGem5, "Write for ID: %lu, Addr: %#lx completed\n", rid, pkt->getAddr());
            }, is_cxl_access);

        if (enqueue_success) 
        {
            DPRINTF(CXLSimGem5, "Added write id %lu pkt id %lu addr %#lx to mem_model\n", id, pkt->id, pkt->getAddr());
            // outstandingReads[pkt->getAddr()].push_back(pkt);
            panic_if(pendingRequests.find(id) != pendingRequests.end(), "Request already exists in pendingRequests\n");
            pendingRequests.insert({id,pkt});
            PacketPtr p = pendingRequests.find(id)->second;
            DPRINTF(CXLSimGem5, "Verification write id %lu pkt id %lu addr %#lx\n", id, p->id, p->getAddr());
            if (id == 546) {
                std::cout<<"Debug"<<std::endl;
            }
            // we count a transaction as outstanding until it has left the
            // queue in the controller, and the response has been sent
            // back, note that this will differ for reads and writes
            // ++nbrOutstandingReads;
            accessAndRespond(pkt);
            num_writes++;
        } 
        else 
        {
            retryReq = true;
        }
        
        // accessAndRespond(pkt);
        // // Generate ramulator WRITE request and try to send to ramulator's memory system
        // enqueue_success = mem_model->
        //     add_external_req(addr, op, id, 
        //     [this](uint64_t addr) {
        //         auto& pkt_q = outstandingWrites.find(addr)->second;
        //         PacketPtr pkt = pkt_q.front();
        //         DPRINTF(CXLSimGem5, "Write to ID: %lu, Addr: %#lx completed.\n", pkt->id, addr);
        //         pkt_q.pop_front();
        //         if (!pkt_q.size())
        //             outstandingWrites.erase(addr);

        //         // added counter to track requests in flight
        //         --nbrOutstandingWrites;

        //         accessAndRespond(pkt);
        //     });

        // if (enqueue_success) 
        // {
        //     DPRINTF(CXLSimGem5, "Added write id %lu to mem_model\n", id);

        //     outstandingWrites[pkt->getAddr()].push_back(pkt);

        //     ++nbrOutstandingWrites;

        //     // perform the access for writes
        //     // accessAndRespond(pkt);
            
        //     num_writes++;
        //     req_id++;
        // } 
        // else 
        // {
        //     retryReq = true;
        // }
    } else {
        panic("Shouldnt ever reach here\n");
        // keep it simple and just respond if necessary
        accessAndRespond(pkt);
        return true;
    }
    if (enqueue_success)
    {
        // Increment counters
        req_id++;
        if (is_cxl_access)
            cxl_accesses++;
        else
            dam_accesses++;
        
        // Find out if this access was part of a special memory region and update the per region counters
        // Remember to give the virtual address here, not physical address
        // Also ensure it aligns to 64 byte cacheline
        if (system()->isMemRegionROI())
        {
            // Implement region of interest counter
            if (is_cxl_access)
                cxl_accesses_roi++;
            else
                dam_accesses_roi++;
            // find_accessed_region(pkt->req->hasVaddr()?((pkt->req->getVaddr()>>6)<<6):0,pkt->req->getSize());
        }
        // Increment the per region counts
        for (auto &r : accessed_regions)
        {
            // std::cout<<"INCR"<<std::endl;
            if (region_access_counts.find(r) == region_access_counts.end())
                region_access_counts[r] = 1;
            else
                region_access_counts[r]++;
        }
        if (record)
            record_file_ptr << std::dec << curTick() << " " << std::hex << (pkt->req->hasVaddr() ? pkt->req->getVaddr() : 0) << " " << (pkt->isRead() ? "R" : "W") << std::endl;
    }

    return enqueue_success;
}

void
CXLSimGem5::recvRespRetry()
{
    DPRINTF(CXLSimGem5, "Retrying to send response\n");

    assert(retryResp);
    retryResp = false;
    sendResponse();
}

void
CXLSimGem5::accessAndRespond(PacketPtr pkt)
{
    DPRINTF(CXLSimGem5, "Access for addr %#lx\n", pkt->getAddr());

    bool needsResponse = pkt->needsResponse();


    Addr before = pkt->getAddr();
    access(pkt);
    Addr after = pkt->getAddr();
    panic_if(before != after, "Addr changed before %#lx and after %#lx access\n",before,after);

    // turn packet around to go back to requestor if response expected
    if (needsResponse) {
        // access already turned the packet into a response
        assert(pkt->isResponse());

        // Assume frontend latency = 0
        Tick time = curTick() + pkt->headerDelay + pkt->payloadDelay;
        // Here we reset the timing of the packet before sending it out.
        pkt->headerDelay = pkt->payloadDelay = 0;

        DPRINTF(CXLSimGem5, "Queuing response for address %lld\n",
                pkt->getAddr());

        // queue it to be sent back
        responseQueue.push_back(pkt);

        // if we are not already waiting for a retry, or are scheduled
        // to send a response, schedule an event
        if (!retryResp && !sendResponseEvent.scheduled())
            schedule(sendResponseEvent, time);
    } else {
        // queue the packet for deletion
        pendingDelete.reset(pkt);
    }
}


Port&
CXLSimGem5::getPort(const std::string &if_name, PortID idx)
{
    if (if_name != "port") {
        return ClockedObject::getPort(if_name, idx);
    } else {
        return port;
    }
}

DrainState
CXLSimGem5::drain()
{
    // check our outstanding reads and writes and if any they need to
    // drain
    return nbrOutstanding() != 0 ? DrainState::Draining : DrainState::Drained;
}

CXLSimGem5::MemorySystemPort::MemorySystemPort(const std::string& _name,
                                 CXLSimGem5& _cxl_mem)
    : ResponsePort(_name), cxl_mem(_cxl_mem)
{ }


} // namespace memory
} // namespace gem5

#pragma pop_macro("warn")