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
    retryReq(false), retryResp(false), startTick(0),
    nbrOutstandingReads(0), nbrOutstandingWrites(0),
    sendResponseEvent([this]{ sendResponse(); }, name()),
    tickEvent([this]{ tick(); }, name()), req_id(0),record(p.record), num_reads(0), num_writes(0), record_file("record_dummymem.dat")
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
}

unsigned int
DummyMem::nbrOutstanding() const
{
}

void
DummyMem::tick()
{
}

Tick
DummyMem::recvAtomic(PacketPtr pkt)
{
    panic_if(pkt->cacheResponding(), "Should not see packets where cache "
             "is responding");

    access(pkt);
    return 0;   // Arbitary latency of 50ns
}

void
DummyMem::recvFunctional(PacketPtr pkt)
{
    panic("DummyMem only supports atomic mode, not functional\n");
}

bool
DummyMem::recvTimingReq(PacketPtr pkt)
{
    panic("DummyMem only supports atomic mode, not functional\n");
    return false;
}

void
DummyMem::recvRespRetry()
{
}

void
DummyMem::accessAndRespond(PacketPtr pkt)
{
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
}

DummyMem::MemorySystemPort::MemorySystemPort(const std::string& _name,
                                 DummyMem& mem)
    : ResponsePort(_name), smem(mem)
{ }


} // namespace memory
} // namespace gem5

#pragma pop_macro("warn")