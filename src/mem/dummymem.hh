#ifndef __MEM_DUMMYMEM_HH__
#define __MEM_DUMMYMEM_HH__

#include <functional>
#include <deque>
#include <unordered_map>

#include "mem/abstract_mem.hh"
#include "params/DummyMem.hh"
#include "simple_mem/simple_mem.hh"



namespace gem5
{

namespace memory
{


class DummyMem : public AbstractMemory
{
  private:
    class MemorySystemPort : public ResponsePort
    {

      private:
        DummyMem& smem;

      public:
        MemorySystemPort(const std::string& _name, DummyMem& _mem);

      protected:
        Tick recvAtomic(PacketPtr pkt) override { return smem.recvAtomic(pkt); };
        void recvFunctional(PacketPtr pkt) override { smem.recvFunctional(pkt); };
        bool recvTimingReq(PacketPtr pkt) override { return smem.recvTimingReq(pkt); };
        void recvRespRetry() override { smem.recvRespRetry(); };

        AddrRangeList getAddrRanges() const override
        {
          AddrRangeList ranges;
          // ranges.push_back(ramulator2.getAddrRange());
          ranges.push_back(smem.getAddrRange());
          return ranges;
        };
    };

    MemorySystemPort port;

    bool retryReq;
    bool retryResp;
    std::unordered_map<Addr, std::deque<PacketPtr>> outstandingReads;
    std::unordered_map<Addr, std::deque<PacketPtr>> outstandingWrites;

    /**
     * Queue to hold response packets until we can send them
     * back. This is needed as DummyMem unconditionally passes
     * responses back without any flow control.
     */
    std::deque<PacketPtr> responseQueue;


    unsigned int nbrOutstanding() const;

    /**
     * When a packet is ready, use the "access()" method in
     * AbstractMemory to actually create the response packet, and send
     * it back to the outside world requestor.
     *
     * @param pkt The packet from the outside world
     */
    void accessAndRespond(PacketPtr pkt);

    void sendResponse();

    /**
     * Event to schedule sending of responses
     */
    EventFunctionWrapper sendResponseEvent;

    /**
     * Upstream caches need this packet until true is returned, so
     * hold it for deletion until a subsequent call
     */
    std::unique_ptr<Packet> pendingDelete;

    /**
     * Unique ID to keep track of requests
     */
    uint64_t req_id = 0;

    /**
     * Bool telling us whether to record data from reads or writes
     * Also the filename where to write these values
     */
    bool record;
    std::string record_file;
    std::ofstream record_file_ptr;

    /**
     * Counters to count number of reads and writes
     */
    uint64_t num_reads, num_writes;

  public:

    typedef DummyMemParams Params;
    DummyMem(const Params &p);

    DrainState drain() override;

    virtual Port& getPort(const std::string& if_name,
                          PortID idx = InvalidPortID) override;

    void init() override;
    void startup() override;

    void resetStats() override;

  protected:

    Tick recvAtomic(PacketPtr pkt);
    void recvFunctional(PacketPtr pkt);
    bool recvTimingReq(PacketPtr pkt);
    void recvRespRetry();

};

} // namespace memory
} // namespace gem5

#endif // __MEM_DUMMYMEM_HH__