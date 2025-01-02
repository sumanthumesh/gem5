#include "mem/simple_gem5_mem.hh"
#include "debug/SimpleGem5Mem.hh"
#include "sim/system.hh"

namespace gem5 {

namespace memory {

SimpleGem5Mem::SimpleGem5Mem(const Params &p)
    : AbstractMemory(p), port(name() + ".port", *this), retryReq(false),
      retryResp(false), startTick(0), nbrOutstandingReads(0),
      nbrOutstandingWrites(0),
      sendResponseEvent([this] { sendResponse(); }, name()),
      tickEvent([this] { tick(); }, name()), req_id(0) {
    DPRINTF(SimpleGem5Mem, "Instantiated SimpleGem5Mem \n");

    // Set the ticks_per_ns parameter in the simulator
    smem.set_ticks_per_ns(sim_clock::as_float::ns);
    smem.set_tclk(p.tck);
    registerExitCallback([this]() { smem.finalize(); });
}

void SimpleGem5Mem::init() {
    AbstractMemory::init();

    if (!port.isConnected()) {
        fatal("SimpleGem5Mem %s is unconnected!\n", name());
    } else {
        port.sendRangeChange();
    }
}

void SimpleGem5Mem::startup() {
    startTick = curTick();

    // kick off the clock ticks
    schedule(tickEvent, clockEdge());
}

void SimpleGem5Mem::resetStats() {
    // wrapper.resetStats();
}

void SimpleGem5Mem::sendResponse() {
    assert(!retryResp);
    assert(!responseQueue.empty());

    // DPRINTF(SimpleGem5Mem, "Attempting to send response\n");
    DPRINTF(SimpleGem5Mem, "Starting %s\n", __func__);

    bool success = port.sendTimingResp(responseQueue.front());
    if (success) {
        PacketPtr p = responseQueue.front();
        DPRINTF(SimpleGem5Mem, "Sent resp for addr:  %#x, %s\n", p->getAddr(),
                (p->isRead() ? "R" : "W"));
        responseQueue.pop_front();

        // DPRINTF(
        // SimpleGem5Mem, "Have %d read, %d write, %d responses outstanding\n",
        // nbrOutstandingReads, nbrOutstandingWrites, responseQueue.size());

        if (!responseQueue.empty() && !sendResponseEvent.scheduled())
            schedule(sendResponseEvent, curTick());

        if (nbrOutstanding() == 0)
            signalDrainDone();
    } else {
        retryResp = true;

        DPRINTF(SimpleGem5Mem, "Need to retry sending resp\n");

        assert(!sendResponseEvent.scheduled());
    }
    DPRINTF(SimpleGem5Mem, "Finished %s\n", __func__);
}

unsigned int SimpleGem5Mem::nbrOutstanding() const {
    return pending_reads.size() + pending_writes.size();
    // return nbrOutstandingReads + nbrOutstandingWrites + responseQueue.size();
}

void SimpleGem5Mem::tick() {

    // DPRINTF(SimpleGem5Mem, "Mem update\n");
    // DPRINTF(SimpleGem5Mem, "Started %s\n", __func__);

    // Only tick when it's timing mode
    if (system()->isTimingMode()) {
        // ramulator2_memorysystem->tick();
        smem.update(curTick());

        // is the connected port waiting for a retry, if so check the
        // state and send a retry if conditions have changed
        if (retryReq) {
            DPRINTF(SimpleGem5Mem, "Mem sent retryReq\n");
            retryReq = false;
            port.sendRetryReq();
        }
    }

    schedule(tickEvent, curTick() + smem.get_tclk() * sim_clock::as_float::ns);

    if (nbrOutstanding() == 0)
        signalDrainDone();

    // DPRINTF(SimpleGem5Mem, "Finished %s\n", __func__);
}

Tick SimpleGem5Mem::recvAtomic(PacketPtr pkt) {
    panic_if(pkt->cacheResponding(), "Should not see packets where cache "
                                     "is responding");

    access(pkt);
    return 50000; // Arbitary latency of 50ns
}

void SimpleGem5Mem::recvFunctional(PacketPtr pkt) {
    pkt->pushLabel(name());
    functionalAccess(pkt);

    for (auto i = responseQueue.begin(); i != responseQueue.end(); ++i)
        pkt->trySatisfyFunctional(*i);

    pkt->popLabel();
}

bool SimpleGem5Mem::recvTimingReq(PacketPtr pkt) {
    DPRINTF(SimpleGem5Mem, "recvTimingReq: %d request %s addr %#x size %d, type %s\n",
            req_id, pkt->cmdString(), pkt->getAddr(), pkt->getSize(), (pkt->isRead()?"R":(pkt->isWrite()?"W":"Unknown Op")));
    DPRINTF(SimpleGem5Mem, "Current simplemem buffer occupancy %d\n",smem.buffer_occupancy());

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
        DPRINTF(SimpleGem5Mem, "Already have a req that needs to be retried, "
                               "so not accepting this req\n");
        return false;
    }
    // This variable will be set to true if SimpleMem accepts the request. The
    // only reason it might not accept the request is if ran out of space in the
    // buffer
    bool enqueue_success = false;
    if (pkt->isRead()) {
        // Generate SimpleMem READ request and try to send to memory system
        // Create the request (id, addr, callback)
        simple_mem::Req req(req_id, pkt->getAddr(), simple_mem::OpType::READ);
        enqueue_success =
            smem.add_req_external(req, curTick(), [this](simple_mem::Req &req) {
                DPRINTF(SimpleGem5Mem, "Read to %ld,%#lx,%s completed.\n", req.id,
                        req.addr,(req.op==simple_mem::OpType::READ?"R":"W"));
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
                DPRINTF(SimpleGem5Mem, "Removed reqid: %d from pending reads\n", req.id);

                // added counter to track requests in flight
                // --nbrOutstandingReads;
            });

        if (enqueue_success) {
            // Add this request to the pending reads structure
            panic_if(pending_reads.find(req.id) != pending_reads.end(),
                     "Req %ld already exists\n", req.id);
            auto emplace_result = pending_reads.emplace(req.id, pkt);
            panic_if(!emplace_result.second,"Placing pkt did not succeed\n");
            //Retrieve entry we just added to verify
            DPRINTF(SimpleGem5Mem,"Req ID: %d, Addr %#x added to pending reads\n",emplace_result.first->first,emplace_result.first->second->getAddr());
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
        enqueue_success =
            smem.add_req_external(req, curTick(), [this](simple_mem::Req &req) {
                DPRINTF(SimpleGem5Mem, "Write to %ld,%#lx,%s completed.\n",
                        req.id, req.addr, (req.op==simple_mem::OpType::READ?"R":"W"));
                panic_if(pending_writes.find(req.id) == pending_writes.end(),
                         "Req %ld to addr %#x not found in pending writes\n", req.id, req.addr);
                DPRINTF(SimpleGem5Mem, "B1\n");
                PacketPtr pkt = pending_writes.find(req.id)->second;
                DPRINTF(SimpleGem5Mem, "B2\n");
                // auto &pkt_q = outstandingReads.find(req.addr)->second;
                // PacketPtr pkt = pkt_q.front();
                // pkt_q.pop_front();
                // if (!pkt_q.size())
                // outstandingReads.erase(req.addr);

                // Access the packet and try to send the response back
                accessAndRespond(pkt);
                DPRINTF(SimpleGem5Mem, "B3\n");
                // Remove the entry from the pending reads structure
                pending_writes.erase(req.id);
                DPRINTF(SimpleGem5Mem, "Removed reqid: %d from pending writes\n", req.id);
                DPRINTF(SimpleGem5Mem, "B4\n");


                // added counter to track requests in flight
                // --nbrOutstandingReads;
            });

        if (enqueue_success) {
            // Add this request to the pending reads structure
            panic_if(pending_writes.find(req.id) != pending_writes.end(),
                     "Req %ld already exists\n", req.id);
            
            auto emplace_result = pending_writes.emplace(req.id, pkt);
            panic_if(!emplace_result.second,"Placing pkt did not succeed\n");
            //Retrieve entry we just added to verify
            DPRINTF(SimpleGem5Mem,"Req ID: %d, Addr %#x added to pending writes\n",emplace_result.first->first,emplace_result.first->second->getAddr());
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
        DPRINTF(SimpleGem5Mem, "Successfully added req %#x to memory\n",
                pkt->getAddr());
        req_id++;
    }
    return enqueue_success;
}

void SimpleGem5Mem::recvRespRetry() {
    DPRINTF(SimpleGem5Mem, "Retrying sending response\n");

    assert(retryResp);
    retryResp = false;
    sendResponse();
}

void SimpleGem5Mem::accessAndRespond(PacketPtr pkt) {
    DPRINTF(SimpleGem5Mem, "Access for address %#x\n", pkt->getAddr());

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

        DPRINTF(SimpleGem5Mem, "Queuing response for address %#x\n",
                pkt->getAddr());

        // queue it to be sent back
        responseQueue.push_back(pkt);

        // if we are not already waiting for a retry, or are scheduled
        // to send a response, schedule an event
        if (!retryResp && !sendResponseEvent.scheduled())
            schedule(sendResponseEvent, time);
    } else {
        // queue the packet for deletion
        DPRINTF(SimpleGem5Mem, "Deleting packet for addr %#x\n",pkt->getAddr());
        pendingDelete.reset(pkt);
    }
}

Port &SimpleGem5Mem::getPort(const std::string &if_name, PortID idx) {
    if (if_name != "port") {
        return ClockedObject::getPort(if_name, idx);
    } else {
        return port;
    }
}

DrainState SimpleGem5Mem::drain() {
    // check our outstanding reads and writes and if any they need to
    // drain
    return nbrOutstanding() != 0 ? DrainState::Draining : DrainState::Drained;
}

SimpleGem5Mem::MemorySystemPort::MemorySystemPort(const std::string &_name,
                                                  SimpleGem5Mem &mem)
    : ResponsePort(_name), mem(mem) {}

} // namespace memory
} // namespace gem5

#pragma pop_macro("warn")
