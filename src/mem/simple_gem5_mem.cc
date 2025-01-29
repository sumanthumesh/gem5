#include "mem/simple_gem5_mem.hh"

#include "base/callback.hh"
#include "base/trace.hh"
#include "debug/SimpleGem5Mem.hh"
#include "debug/Drain.hh"
#include "sim/system.hh"

// spdlog collides with gem5...
#pragma push_macro("warn")
#undef warn

namespace gem5
{

namespace memory
{

SimpleGem5Mem::SimpleGem5Mem(const Params &p) :
    AbstractMemory(p),
    port(name() + ".port", *this),
    retryReq(false), retryResp(false), startTick(0),
    nbrOutstandingReads(0), nbrOutstandingWrites(0),
    sendResponseEvent([this]{ sendResponse(); }, name()),
    tickEvent([this]{ tick(); }, name()), req_id(0),record(p.record), num_reads(0), num_writes(0), record_file("record_simplemem.dat")
{
    DPRINTF(SimpleGem5Mem, "Instantiated SimpleGem5Mem \n");

    /**
     * Instantitate actual memory model
     */
    mem_model = std::make_unique<simple_mem::SimpleMem>();
    mem_model->set_ticks_per_ns(sim_clock::as_float::ns);

    // Record data written to and read from memory
    if (record)
        record_file_ptr.open(record_file);

    registerExitCallback([this]() { 
        std::cout<<"Finished SimpleGem5Mem simulation\n";
        mem_model->finalize();
        std::cout<<"NUM READS : "<<num_reads<<"\n";
        std::cout<<"NUM WRITES: "<<num_writes<<"\n";
        // Close the record file
        if (record)
            record_file_ptr.close();
        panic_if(nbrOutstanding()!=0, "All requests haven't been fulfilled\n");
    });
}

void
SimpleGem5Mem::init()
{
    AbstractMemory::init();

    if (!port.isConnected()) {
        fatal("SimpleGem5Mem %s is unconnected!\n", name());
    } else {
        port.sendRangeChange();
    }

    // YAML::Node config = Ramulator::Config::parse_config_file(config_path, {});
    // ramulator2_frontend = Ramulator::Factory::create_frontend(config);
    // ramulator2_memorysystem = Ramulator::Factory::create_memory_system(config);

    // ramulator2_frontend->connect_memory_system(ramulator2_memorysystem);
    // ramulator2_memorysystem->connect_frontend(ramulator2_frontend);

    // if (system()->cacheLineSize() != wrapper.burstSize())
    //     fatal("SimpleGem5Mem burst size %d does not match cache line size %d\n",
    //           wrapper.burstSize(), system()->cacheLineSize());
}

void
SimpleGem5Mem::startup()
{
    startTick = curTick();

    // kick off the clock ticks
    schedule(tickEvent, clockEdge());
}

void
SimpleGem5Mem::resetStats() {
    // wrapper.resetStats();
}

void
SimpleGem5Mem::sendResponse()
{
    assert(!retryResp);
    assert(!responseQueue.empty());

    DPRINTF(SimpleGem5Mem, "Attempting to send response\n");

    bool success = port.sendTimingResp(responseQueue.front());
    if (success) {
        responseQueue.pop_front();

        DPRINTF(SimpleGem5Mem, "Have %d read, %d write, %d responses outstanding\n",
                nbrOutstandingReads, nbrOutstandingWrites,
                responseQueue.size());

        if (!responseQueue.empty() && !sendResponseEvent.scheduled())
            schedule(sendResponseEvent, curTick());

        if (nbrOutstanding() == 0)
            signalDrainDone();
    } else {
        retryResp = true;

        DPRINTF(SimpleGem5Mem, "Waiting for response retry\n");

        assert(!sendResponseEvent.scheduled());
    }
}

unsigned int
SimpleGem5Mem::nbrOutstanding() const
{
    return nbrOutstandingReads + nbrOutstandingWrites + responseQueue.size();
}

void
SimpleGem5Mem::tick()
{

    // Panic if we are not operating in timing mode
    panic_if(!system()->isTimingMode(), "SimpleGem5 mem is only designed to operate in timing mode\n");

    // Only tick when it's timing mode
    if (system()->isTimingMode()) {
        mem_model->update(curTick());

        // is the connected port waiting for a retry, if so check the
        // state and send a retry if conditions have changed
        if (retryReq) {
            retryReq = false;
            port.sendRetryReq();
        }
    }

    schedule(tickEvent,
        curTick() + mem_model->get_tclk() * sim_clock::as_float::ns);
}

Tick
SimpleGem5Mem::recvAtomic(PacketPtr pkt)
{
    panic_if(pkt->cacheResponding(), "Should not see packets where cache "
             "is responding");

    access(pkt);
    return 50000;   // Arbitary latency of 50ns
}

void
SimpleGem5Mem::recvFunctional(PacketPtr pkt)
{
    pkt->pushLabel(name());
    functionalAccess(pkt);

    for (auto i = responseQueue.begin(); i != responseQueue.end(); ++i)
        pkt->trySatisfyFunctional(*i);

    pkt->popLabel();
}

bool
SimpleGem5Mem::recvTimingReq(PacketPtr pkt)
{
    DPRINTF(SimpleGem5Mem, "recvTimingReq: request %s addr %#x size %d\n",
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

    // Create a SimpleMem request
    simple_mem::Req req(pkt->id, pkt->getAddr(), pkt->isRead() ? simple_mem::OpType::READ : simple_mem::OpType::WRITE);

    bool enqueue_success = false;
    if (pkt->isRead()) 
    {
        // Generate ramulator READ request and try to send to ramulator's memory system
        enqueue_success = mem_model->
            add_req_external(req, curTick(), [this](simple_mem::Req& req) {
                DPRINTF(SimpleGem5Mem, "Read to #%ld: %#lx completed.\n", req.id, req.addr);
                auto& pkt_q = outstandingReads.find(req.addr)->second;
                PacketPtr pkt = pkt_q.front();
                pkt_q.pop_front();
                if (!pkt_q.size())
                    outstandingReads.erase(req.addr);

                // added counter to track requests in flight
                --nbrOutstandingReads;

                accessAndRespond(pkt);
            });

        if (enqueue_success) 
        {
            outstandingReads[pkt->getAddr()].push_back(pkt);

            // we count a transaction as outstanding until it has left the
            // queue in the controller, and the response has been sent
            // back, note that this will differ for reads and writes
            ++nbrOutstandingReads;
            req_id++;
        } 
        else 
        {
            retryReq = true;
        }
    } else if (pkt->isWrite()) {
        // Generate ramulator WRITE request and try to send to ramulator's memory system
        enqueue_success = mem_model->
            add_req_external(req, curTick(), [this](simple_mem::Req& req) {
                DPRINTF(SimpleGem5Mem, "Write to #%ld: %#lx completed.\n", req.id, req.addr);
                auto& pkt_q = outstandingWrites.find(req.addr)->second;
                PacketPtr pkt = pkt_q.front();
                pkt_q.pop_front();
                if (!pkt_q.size())
                    outstandingWrites.erase(req.addr);

                // added counter to track requests in flight
                --nbrOutstandingWrites;

                accessAndRespond(pkt);
            });

        if (enqueue_success) 
        {
            outstandingWrites[pkt->getAddr()].push_back(pkt);

            ++nbrOutstandingWrites;

            // perform the access for writes
            // accessAndRespond(pkt);
            req_id++;
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
    if (enqueue_success)
    {
        DPRINTF(SimpleGem5Mem, "%s to #%ld: %#lx added\n",req.op==simple_mem::OpType::READ?"Read":"Write", req.id, req.addr);
    }

    return enqueue_success;
}

void
SimpleGem5Mem::recvRespRetry()
{
    DPRINTF(SimpleGem5Mem, "Retrying\n");

    assert(retryResp);
    retryResp = false;
    sendResponse();
}

void
SimpleGem5Mem::accessAndRespond(PacketPtr pkt)
{
    DPRINTF(SimpleGem5Mem, "Access for address %lld\n", pkt->getAddr());

    bool needsResponse = pkt->needsResponse();

    access(pkt);
    if(record)
    {
        record_file_ptr<<pkt->getAddr()<<","<<pkt->sprintData()<<"\n";
    }

    // turn packet around to go back to requestor if response expected
    if (needsResponse) {
        // access already turned the packet into a response
        assert(pkt->isResponse());

        // Assume frontend latency = 0
        Tick time = curTick() + pkt->headerDelay + pkt->payloadDelay;
        // Here we reset the timing of the packet before sending it out.
        pkt->headerDelay = pkt->payloadDelay = 0;

        DPRINTF(SimpleGem5Mem, "Queuing response for address %lld\n",
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
SimpleGem5Mem::getPort(const std::string &if_name, PortID idx)
{
    if (if_name != "port") {
        return ClockedObject::getPort(if_name, idx);
    } else {
        return port;
    }
}

DrainState
SimpleGem5Mem::drain()
{
    // check our outstanding reads and writes and if any they need to
    // drain
    return nbrOutstanding() != 0 ? DrainState::Draining : DrainState::Drained;
}

SimpleGem5Mem::MemorySystemPort::MemorySystemPort(const std::string& _name,
                                 SimpleGem5Mem& mem)
    : ResponsePort(_name), smem(mem)
{ }


} // namespace memory
} // namespace gem5

#pragma pop_macro("warn")