#include "mem/cxlsim_gem5.hh"
#include "debug/CXLSimGem5.hh"
#include "sim/system.hh"

namespace gem5 {

namespace memory {

CXLSimGem5::CXLSimGem5(const Params &p)
    : AbstractMemory(p), port(name() + ".port", *this), retryReq(false), retryResp(false), startTick(0),
      nbrOutstandingReads(0), nbrOutstandingWrites(0), sendResponseEvent([this] { sendResponse(); }, name()),
      tickEvent([this] { tick(); }, name()), req_id(0) {
    DPRINTF(CXLSimGem5, "Instantiated CXLSimGem5 \n");

    // Set the ticks_per_ns parameter in the simulator
    smem.set_ticks_per_ns(sim_clock::as_float::ns);
    smem.set_tclk(p.tck);
    registerExitCallback([this]() {
        smem.finalize();
        for (auto &v : this->region_counts) {
            std::cout << std::dec << "Region" << v.first << ":" << v.second << "\n";
        }
    });
    // if (this->system() != nullptr) {
    //     addr_regions = this->system()->get_special_addr_regions();
    // } else {
    //     std::cerr << "Error: _system is null!" << std::endl;
    //     exit(1);
    // }
    // addr_regions = this->_system->get_special_addr_regions();
}

void CXLSimGem5::init() {
    AbstractMemory::init();

    if (!port.isConnected()) {
        fatal("CXLSimGem5 %s is unconnected!\n", name());
    } else {
        port.sendRangeChange();
    }

    if (!system()) {
        panic("System pointer could not be determined in CXLSimGem5!");
    }

    addr_regions = system()->get_special_addr_regions();
}

void CXLSimGem5::startup() {
    startTick = curTick();

    // kick off the clock ticks
    schedule(tickEvent, clockEdge());
}

void CXLSimGem5::resetStats() {
    // wrapper.resetStats();
}

void CXLSimGem5::sendResponse() {
    assert(!retryResp);
    assert(!responseQueue.empty());

    // DPRINTF(CXLSimGem5, "Attempting to send response\n");
    DPRINTF(CXLSimGem5, "Starting %s\n", __func__);

    bool success = port.sendTimingResp(responseQueue.front());
    if (success) {
        PacketPtr p = responseQueue.front();
        DPRINTF(CXLSimGem5, "Sent resp for addr:  %#x, %s\n", p->getAddr(), (p->isRead() ? "R" : "W"));
        responseQueue.pop_front();

        // DPRINTF(
        // CXLSimGem5, "Have %d read, %d write, %d responses outstanding\n",
        // nbrOutstandingReads, nbrOutstandingWrites, responseQueue.size());

        if (!responseQueue.empty() && !sendResponseEvent.scheduled())
            schedule(sendResponseEvent, curTick());

        if (nbrOutstanding() == 0)
            signalDrainDone();
    } else {
        retryResp = true;

        DPRINTF(CXLSimGem5, "Need to retry sending resp\n");

        assert(!sendResponseEvent.scheduled());
    }
    DPRINTF(CXLSimGem5, "Finished %s\n", __func__);
}

unsigned int CXLSimGem5::nbrOutstanding() const {
    return pending_reads.size() + pending_writes.size();
    // return nbrOutstandingReads + nbrOutstandingWrites + responseQueue.size();
}

void CXLSimGem5::tick() {

    // DPRINTF(CXLSimGem5, "Mem update\n");
    // DPRINTF(CXLSimGem5, "Started %s\n", __func__);

    // Only tick when it's timing mode
    if (system()->isTimingMode()) {
        // ramulator2_memorysystem->tick();
        smem.update(curTick());

        // is the connected port waiting for a retry, if so check the
        // state and send a retry if conditions have changed
        if (retryReq) {
            DPRINTF(CXLSimGem5, "Mem sent retryReq\n");
            retryReq = false;
            port.sendRetryReq();
        }
    }

    schedule(tickEvent, curTick() + smem.get_tclk() * sim_clock::as_float::ns);

    if (nbrOutstanding() == 0)
        signalDrainDone();

    // DPRINTF(CXLSimGem5, "Finished %s\n", __func__);
}

Tick CXLSimGem5::recvAtomic(PacketPtr pkt) {
    panic_if(pkt->cacheResponding(), "Should not see packets where cache "
                                     "is responding");

    access(pkt);
    return 50000; // Arbitary latency of 50ns
}

void CXLSimGem5::recvFunctional(PacketPtr pkt) {
    pkt->pushLabel(name());
    functionalAccess(pkt);

    for (auto i = responseQueue.begin(); i != responseQueue.end(); ++i)
        pkt->trySatisfyFunctional(*i);

    pkt->popLabel();
}

bool CXLSimGem5::recvTimingReq(PacketPtr pkt) {
    DPRINTF(CXLSimGem5, "recvTimingReq: %d request %s addr %#x size %d, type %s\n", req_id, pkt->cmdString(),
            pkt->getAddr(), pkt->getSize(), (pkt->isRead() ? "R" : (pkt->isWrite() ? "W" : "Unknown Op")));
    DPRINTF(CXLSimGem5, "Current simplemem buffer occupancy %d\n", smem.buffer_occupancy());

    panic_if(pkt->cacheResponding(), "Should not see packets where cache "
                                     "is responding");

    panic_if(!(pkt->isRead() || pkt->isWrite()),
             "Should only see read and writes at memory controller, "
             "saw %s to %#llx\n",
             pkt->cmdString(), pkt->getAddr());

    // we should not get a new request after committing to retry the
    // current one, but unfortunately the CPU violates this rule, so
    // simply ignore it for now
    if (retryReq) {
        DPRINTF(CXLSimGem5, "Already have a req that needs to be retried, "
                               "so not accepting this req\n");
        return false;
    }
    // This variable will be set to true if SimpleMem accepts the request. The
    // only reason it might not accept the request is if ran out of space in the
    // buffer

    // Placeholder
    size_t region_id = 0;

    // Note down the virtual address. Useful for checking if it is part of
    // special region
    if (pkt->isRead() || pkt->isWrite()) {
        DPRINTF(CXLSimGem5, "Paddr:%#lx,Vaddr:%#lx\n", pkt->req->getPaddr(),
                pkt->req->hasVaddr() ? pkt->req->getVaddr() : (uint64_t)0);
        // std::cout << std::hex << "Req:V0x" << (pkt->req->hasVaddr() ? pkt->req->getVaddr() : (uint64_t)0) << ",P0x"
        //           << pkt->req->getPaddr() << "," << pkt->getSize() << std::endl;
    }

    bool enqueue_success = false;
    if (pkt->isRead()) {
        // Generate SimpleMem READ request and try to send to memory system
        // Create the request (id, addr, callback)
        simple_mem::Req req(req_id, pkt->getAddr(), simple_mem::OpType::READ);
        auto regions_accessed =
            find_special_addr_region(pkt->req->hasVaddr() ? pkt->req->getVaddr() : (uint64_t)0, region_id);
        // if (regions_accessed.size() > 0) {
        //     std::cout << "Load P0x" << std::hex << req.addr << ", V0x" << pkt->req->getVaddr() << "," << region_id
        //               << std::endl;
        // }
        enqueue_success = smem.add_req_external(req, curTick(), [this](simple_mem::Req &req) {
            DPRINTF(CXLSimGem5, "Read to %ld,%#lx,%s completed.\n", req.id, req.addr,
                    (req.op == simple_mem::OpType::READ ? "R" : "W"));
            panic_if(pending_reads.find(req.id) == pending_reads.end(),
                     "Req %ld to addr %#x not found in pending reads\n", req.id, req.addr);
            PacketPtr pkt = pending_reads.find(req.id)->second;
            // auto &pkt_q = outstandingReads.find(req.addr)->second;
            // PacketPtr pkt = pkt_q.front();
            // pkt_q.pop_front();
            // if (!pkt_q.size())
            // outstandingReads.erase(req.addr);

            // Access the packet and try to send the response back
            accessAndRespond(pkt);

            // Remove the entry from the pending reads structure
            pending_reads.erase(req.id);
            DPRINTF(CXLSimGem5, "Removed reqid: %d from pending reads\n", req.id);

            // added counter to track requests in flight
            // --nbrOutstandingReads;
        });

        if (enqueue_success) {
            // Add this request to the pending reads structure
            panic_if(pending_reads.find(req.id) != pending_reads.end(), "Req %ld already exists\n", req.id);
            auto emplace_result = pending_reads.emplace(req.id, pkt);
            panic_if(!emplace_result.second, "Placing pkt did not succeed\n");
            // Retrieve entry we just added to verify
            DPRINTF(CXLSimGem5, "Req ID: %d, Addr %#x added to pending reads\n", emplace_result.first->first,
                    emplace_result.first->second->getAddr());
            // outstandingReads[pkt->getAddr()].push_back(pkt);

            // we count a transaction as outstanding until it has left the
            // queue in the controller, and the response has been sent
            // back, note that this will differ for reads and writes
            // ++nbrOutstandingReads;
        } else {
            retryReq = true;
        }
    } else if (pkt->isWrite()) {
        // Generate SimpleMem READ request and try to send to memory system
        // Create the request (id, addr, callback)
        simple_mem::Req req(req_id, pkt->getAddr(), simple_mem::OpType::WRITE);
        auto regions_accessed =
            find_special_addr_region(pkt->req->hasVaddr() ? pkt->req->getVaddr() : (uint64_t)0, region_id);
        // if (regions_accessed.size() > 0) {
        //     std::cout << "Store P0x" << std::hex << req.addr << ", V0x" << pkt->req->getVaddr() << "," << region_id
        //               << std::endl;
        // }
        enqueue_success = smem.add_req_external(req, curTick(), [this](simple_mem::Req &req) {
            DPRINTF(CXLSimGem5, "Write to %ld,%#lx,%s completed.\n", req.id, req.addr,
                    (req.op == simple_mem::OpType::READ ? "R" : "W"));
            panic_if(pending_writes.find(req.id) == pending_writes.end(),
                     "Req %ld to addr %#x not found in pending writes\n", req.id, req.addr);
            PacketPtr pkt = pending_writes.find(req.id)->second;
            // auto &pkt_q = outstandingReads.find(req.addr)->second;
            // PacketPtr pkt = pkt_q.front();
            // pkt_q.pop_front();
            // if (!pkt_q.size())
            // outstandingReads.erase(req.addr);

            // Access the packet and try to send the response back
            accessAndRespond(pkt);
            // Remove the entry from the pending reads structure
            pending_writes.erase(req.id);
            DPRINTF(CXLSimGem5, "Removed reqid: %d from pending writes\n", req.id);

            // added counter to track requests in flight
            // --nbrOutstandingReads;
        });

        if (enqueue_success) {
            // Add this request to the pending reads structure
            panic_if(pending_writes.find(req.id) != pending_writes.end(), "Req %ld already exists\n", req.id);

            auto emplace_result = pending_writes.emplace(req.id, pkt);
            panic_if(!emplace_result.second, "Placing pkt did not succeed\n");
            // Retrieve entry we just added to verify
            DPRINTF(CXLSimGem5, "Req ID: %d, Addr %#x added to pending writes\n", emplace_result.first->first,
                    emplace_result.first->second->getAddr());
            // outstandingReads[pkt->getAddr()].push_back(pkt);

            // we count a transaction as outstanding until it has left the
            // queue in the controller, and the response has been sent
            // back, note that this will differ for reads and writes
            // ++nbrOutstandingReads;
        } else {
            retryReq = true;
        }
    }
    if (enqueue_success) {
        DPRINTF(CXLSimGem5, "Successfully added req %#x to memory\n", pkt->getAddr());
        req_id++;
    }
    return enqueue_success;
}

void CXLSimGem5::recvRespRetry() {
    DPRINTF(CXLSimGem5, "Retrying sending response\n");

    assert(retryResp);
    retryResp = false;
    sendResponse();
}

void CXLSimGem5::accessAndRespond(PacketPtr pkt) {
    DPRINTF(CXLSimGem5, "Access for address %#x\n", pkt->getAddr());

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

        DPRINTF(CXLSimGem5, "Queuing response for address %#x\n", pkt->getAddr());

        // queue it to be sent back
        responseQueue.push_back(pkt);

        // if we are not already waiting for a retry, or are scheduled
        // to send a response, schedule an event
        if (!retryResp && !sendResponseEvent.scheduled())
            schedule(sendResponseEvent, time);
    } else {
        // queue the packet for deletion
        DPRINTF(CXLSimGem5, "Deleting packet for addr %#x\n", pkt->getAddr());
        pendingDelete.reset(pkt);
    }
}

/**
 * Check if address ranges overlap
 * First range is x1->y1
 * Second range is x2->y2
 * the y component is not inclusive i.e., the range is [x,y)
 * The function will return true if there is any overlap between [x1,y1) and
 * [x2,y2)
 */
bool is_overlap(uint64_t x1, uint64_t y1, uint64_t x2, uint64_t y2) {
    // Two ranges do not overlap if one of the following is true
    // 1. y1 < x2
    // 2. y2 < x1

    if (y1 < x2 || y2 < x1)
        return false;
    return true;
}

std::vector<size_t> CXLSimGem5::find_special_addr_region(uint64_t addr, size_t size) {

    /**
     * Function takes a request as input (virtual address, number of bytes
     * accessed) and returns a list of special regions accessed by the request
     * Doing this becuase there is a chance that a single access accesses
     * multiple regions
     */

    DPRINTF(CXLSimGem5, "A:0x%#lx,S:%d\n", addr, size);
    // Find first recorded special region whose start address is >= the addr
    auto it = addr_regions->lower_bound(addr);

    std::vector<size_t> regions_accessed;

    // Check if the retrieved address region and requested region overlap
    if (is_overlap(addr, addr + size + 1, it->first, it->second.first)) {
        regions_accessed.push_back(it->second.second);
        // std::cout << "Region:" << it->second.second << "(" << std::hex << it->first << "," << it->second.first << ")"
        //           << std::endl;
    }
    if (it != addr_regions->begin()) {
        --it;
        if (is_overlap(addr, addr + size + 1, it->first, it->second.first)) {
            regions_accessed.push_back(it->second.second);
            // std::cout << "Region:" << std::hex << it->second.second << "(" << it->first << "," << it->second.first
            //           << ")" << std::endl;
        }
    }
    if (it != addr_regions->end()) {
        ++it;
        if (is_overlap(addr, addr + size + 1, it->first, it->second.first)) {
            regions_accessed.push_back(it->second.second);
            // std::cout << "Region:" << std::hex << it->second.second << "(" << it->first << "," << it->second.first
            //           << ")" << std::endl;
        }
    }
    for (auto &v : regions_accessed) {
        if (region_counts.find(v) != region_counts.end()) {
            region_counts[v]++;
        } else {
            region_counts.insert({v, 1});
        }
    }
    return regions_accessed;
}

Port &CXLSimGem5::getPort(const std::string &if_name, PortID idx) {
    if (if_name != "port") {
        return ClockedObject::getPort(if_name, idx);
    } else {
        return port;
    }
}

DrainState CXLSimGem5::drain() {
    // check our outstanding reads and writes and if any they need to
    // drain
    return nbrOutstanding() != 0 ? DrainState::Draining : DrainState::Drained;
}

CXLSimGem5::MemorySystemPort::MemorySystemPort(const std::string &_name, CXLSimGem5 &mem)
    : ResponsePort(_name), mem(mem) {}

} // namespace memory
} // namespace gem5

#pragma pop_macro("warn")
