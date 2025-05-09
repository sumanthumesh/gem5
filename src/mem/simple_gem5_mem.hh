#ifndef __MEM_SIMPLEGEM5MEM_HH__
#define __MEM_SIMPLEGEM5MEM_HH__

#include <functional>
#include <deque>
#include <unordered_map>

#include "mem/abstract_mem.hh"
#include "params/SimpleGem5Mem.hh"
#include "simple_mem/simple_mem.hh"



namespace gem5
{

namespace memory
{


class SimpleGem5Mem : public AbstractMemory
{
  private:
    class MemorySystemPort : public ResponsePort
    {

      private:
        SimpleGem5Mem& smem;

      public:
        MemorySystemPort(const std::string& _name, SimpleGem5Mem& _mem);

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

    // std::string config_path;
    // Ramulator::IFrontEnd* ramulator2_frontend;
    // Ramulator::IMemorySystem* ramulator2_memorysystem;

    // std::function<void(Ramulator::Request&)> read_callback;
    // std::function<void(Ramulator::Request&)> write_callback;

    /**
     * Actual memory model
     */
    std::unique_ptr<simple_mem::SimpleMem> mem_model;

    bool retryReq;
    bool retryResp;
    Tick startTick;
    std::unordered_map<Addr, std::deque<PacketPtr>> outstandingReads;
    std::unordered_map<Addr, std::deque<PacketPtr>> outstandingWrites;

    /**
     * Count the number of outstanding transactions so that we can
     * block any further requests until there is space in SimpleGem5Mem and
     * the sending queue we need to buffer the response packets.
     */
    unsigned int nbrOutstandingReads;
    unsigned int nbrOutstandingWrites;

    /**
     * Queue to hold response packets until we can send them
     * back. This is needed as SimpleGem5Mem unconditionally passes
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
     * Progress the controller one clock cycle.
     */
    void tick();

    /**
     * Event to schedule clock ticks
     */
    EventFunctionWrapper tickEvent;

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

    typedef SimpleGem5MemParams Params;
    SimpleGem5Mem(const Params &p);

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

#endif // __MEM_SIMPLEGEM5MEM_HH__