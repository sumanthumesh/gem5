#include "mem/dummymem.hh"

#include "base/callback.hh"
#include "base/trace.hh"
#include "debug/DummyMem.hh"
#include "debug/Drain.hh"
#include "sim/system.hh"

// spdlog collides with gem5...
#pragma push_macro("warn")
#undef warn

namespace gem5
{

namespace memory
{

DummyMem::DummyMem(const Params &p) :
    AbstractMemory(p),
    port(name() + ".port", *this),
    retryReq(false), retryResp(false),
    sendResponseEvent([this]{ sendResponse(); }, name()),
    req_id(0),record(p.record), num_reads(0), num_writes(0), record_file("record_dummymem.dat")
{
    DPRINTF(DummyMem, "Instantiated DummyMem \n");

    // Record data written to and read from memory
    if (record)
        record_file_ptr.open(record_file);

    registerExitCallback([this]() {
        std::cout<<"Finished DummyMem simulation\n";
        std::cout<<"NUM READS : "<<num_reads<<"\n";
        std::cout<<"NUM WRITES: "<<num_writes<<"\n";
        // Close the record file
        if (record)
            record_file_ptr.close();
        panic_if(nbrOutstanding()!=0, "All requests haven't been fulfilled\n");
    });
}

void
DummyMem::init()
{
    AbstractMemory::init();

    if (!port.isConnected()) {
        fatal("DummyMem %s is unconnected!\n", name());
    } else {
        port.sendRangeChange();
    }
}

void
DummyMem::startup()
{
}

void
DummyMem::resetStats() {
    // wrapper.resetStats();
}

void
DummyMem::sendResponse()
{
    assert(!retryResp);
    assert(!responseQueue.empty());

    DPRINTF(DummyMem, "Attempting to send response\n");

    bool success = port.sendTimingResp(responseQueue.front());
    if (success) {
        responseQueue.pop_front();

        DPRINTF(DummyMem, "Have %d responses outstanding\n", responseQueue.size());

        if (!responseQueue.empty() && !sendResponseEvent.scheduled())
            schedule(sendResponseEvent, curTick());

        if (nbrOutstanding() == 0)
            signalDrainDone();
    } else {
        retryResp = true;

        DPRINTF(DummyMem, "Waiting for response retry\n");

        assert(!sendResponseEvent.scheduled());
    }
}

unsigned int
DummyMem::nbrOutstanding() const
{
    return responseQueue.size();
}

Tick
DummyMem::recvAtomic(PacketPtr pkt)
{
    panic("Dummymem only supports timing mode, not atomic\n");
}

void
DummyMem::recvFunctional(PacketPtr pkt)
{
    pkt->pushLabel(name());
    functionalAccess(pkt);

    for (auto i = responseQueue.begin(); i != responseQueue.end(); ++i)
        pkt->trySatisfyFunctional(*i);

    pkt->popLabel();
}

bool
DummyMem::recvTimingReq(PacketPtr pkt)
{
    DPRINTF(DummyMem, "recvTimingReq: request %s addr %#x size %d\n",
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

    if (record) {
        record_file_ptr << std::dec << curTick() << " " << std::hex << (pkt->req->hasVaddr() ? pkt->req->getVaddr() : pkt->getAddr()) << " " << (pkt->isRead() ? "R" : "W") << std::endl;
    }

    // Check if pkt has is_llc_prefetch set
    if(pkt->is_llc_prefetch)
    {
        DPRINTF(DummyMem, "Prefetch pkt from LLC\n");
        std::cout<<"Prefetch pkt at mem "<<pkt->id<<std::endl;
    }

    bool enqueue_success = false;
    if (pkt->isRead())
    {
        accessAndRespond(pkt);
        req_id++;
    }
    else if (pkt->isWrite()) {
        accessAndRespond(pkt);
        req_id++;
    } else {
        panic("Shouldn't reach here\n");
        return true;
    }

    return true;
}

void
DummyMem::recvRespRetry()
{
    DPRINTF(DummyMem, "Retrying\n");

    assert(retryResp);
    retryResp = false;
    sendResponse();
}

void
DummyMem::accessAndRespond(PacketPtr pkt)
{
    DPRINTF(DummyMem, "Access for address %lld\n", pkt->getAddr());

    bool needsResponse = pkt->needsResponse();

    if(pkt->cmd.isPrefetch())
        std::cout<<"Prefetch"<<"\n";
    
    access(pkt);


    // turn packet around to go back to requestor if response expected
    if (needsResponse) {
        // access already turned the packet into a response
        assert(pkt->isResponse());

        // Assume latency = 0
        DPRINTF(DummyMem, "Queuing response for address %lld\n",
                pkt->getAddr());

        // queue it to be sent back
        responseQueue.push_back(pkt);

        // if we are not already waiting for a retry, or are scheduled
        // to send a response, schedule an event
        if (!retryResp && !sendResponseEvent.scheduled())
            schedule(sendResponseEvent, curTick());
    } else {
        // queue the packet for deletion
        pendingDelete.reset(pkt);
    }
}


Port&
DummyMem::getPort(const std::string &if_name, PortID idx)
{
    if (if_name != "port") {
        return ClockedObject::getPort(if_name, idx);
    } else {
        return port;
    }
}

DrainState
DummyMem::drain()
{
    // check our outstanding reads and writes and if any they need to
    // drain
    return nbrOutstanding() != 0 ? DrainState::Draining : DrainState::Drained;
}

DummyMem::MemorySystemPort::MemorySystemPort(const std::string& _name,
                                 DummyMem& mem)
    : ResponsePort(_name), smem(mem)
{ }


} // namespace memory
} // namespace gem5

#pragma pop_macro("warn")
