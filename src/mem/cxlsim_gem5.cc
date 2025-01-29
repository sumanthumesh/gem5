#include "mem/cxlsim_gem5.hh"

#include "base/callback.hh"
#include "base/trace.hh"
#include "debug/CXLSimGem5.hh"
#include "debug/Drain.hh"
#include "sim/system.hh"

// spdlog collides with gem5...
#pragma push_macro("warn")
#undef warn

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
    tickEvent([this]{ tick(); }, name())
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

    registerExitCallback([this]() {
        std::cout<<"FInished CXL Simulation\n";    
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

    DPRINTF(CXLSimGem5, "Attempting to send response for %lu\n", responseQueue.front()->id);

    bool success = port.sendTimingResp(responseQueue.front());
    if (success) {
        DPRINTF(CXLSimGem5, "Sent response for %lu\n", responseQueue.front()->id);
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
    return nbrOutstandingReads + nbrOutstandingWrites + responseQueue.size();
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
    DPRINTF(CXLSimGem5, "recvTimingReq: request %s addr %#x size %d id %lu\n",
            pkt->cmdString(), pkt->getAddr(), pkt->getSize(), pkt->id);

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

    uint64_t addr = pkt->getAddr(), id = pkt->id;
    CXL::opcode op = pkt->isRead() ? CXL::opcode::Req : CXL::opcode::RwD;


    bool enqueue_success = false;
    if (pkt->isRead()) 
    {
        // Generate ramulator READ request and try to send to ramulator's memory system
        enqueue_success = mem_model->add_external_req(addr, op, id, 
            [this](uint64_t addr) {
                auto& pkt_q = outstandingReads.find(addr)->second;
                PacketPtr pkt = pkt_q.front();
                DPRINTF(CXLSimGem5, "Read to ID: %lu, Addr: %#lx completed.\n", pkt->id, addr);
                pkt_q.pop_front();
                if (!pkt_q.size())
                    outstandingReads.erase(addr);

                // added counter to track requests in flight
                --nbrOutstandingReads;

                accessAndRespond(pkt);
            });

        if (enqueue_success) 
        {
            DPRINTF(CXLSimGem5, "Added read id %lu to mem_model\n", id);
            outstandingReads[pkt->getAddr()].push_back(pkt);

            // we count a transaction as outstanding until it has left the
            // queue in the controller, and the response has been sent
            // back, note that this will differ for reads and writes
            ++nbrOutstandingReads;
        } 
        else 
        {
            retryReq = true;
        }
    } else if (pkt->isWrite()) {
        // Generate ramulator WRITE request and try to send to ramulator's memory system
        enqueue_success = mem_model->
            add_external_req(addr, op, id, 
            [this](uint64_t addr) {
                auto& pkt_q = outstandingWrites.find(addr)->second;
                PacketPtr pkt = pkt_q.front();
                DPRINTF(CXLSimGem5, "Write to ID: %lu, Addr: %#lx completed.\n", pkt->id, addr);
                pkt_q.pop_front();
                if (!pkt_q.size())
                    outstandingWrites.erase(addr);

                // added counter to track requests in flight
                --nbrOutstandingWrites;

                // accessAndRespond(pkt);
            });

        if (enqueue_success) 
        {
            DPRINTF(CXLSimGem5, "Added write id %lu to mem_model\n", id);

            outstandingWrites[pkt->getAddr()].push_back(pkt);

            ++nbrOutstandingWrites;

            // perform the access for writes
            accessAndRespond(pkt);
        } 
        else 
        {
            retryReq = true;
        }
    } else {
        // keep it simple and just respond if necessary
        accessAndRespond(pkt);
        return true;
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
    DPRINTF(CXLSimGem5, "Access for req id %lu addr %#lx\n", pkt->id, pkt->getAddr());

    bool needsResponse = pkt->needsResponse();

    access(pkt);

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