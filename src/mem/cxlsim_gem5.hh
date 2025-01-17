#ifndef __MEM_CXLSIMGEM5_HH__
#define __MEM_CXLSIMGEM5_HH__

#include <deque>
#include <functional>
#include <unordered_map>

#include "mem/abstract_mem.hh"
#include "params/CXLSimGem5.hh"
#include "simple_mem/simple_mem.hh"

// Forward declare SimpleMem
// namespace simple_mem {

// class SimpleMem;
// class Req;
// class OpType;

// }

namespace gem5 {

namespace memory {

class CXLSimGem5 : public AbstractMemory {
  private:
    class MemorySystemPort : public ResponsePort {

      private:
        CXLSimGem5 &mem;

      public:
        MemorySystemPort(const std::string &_name, CXLSimGem5 &mem);

      protected:
        Tick recvAtomic(PacketPtr pkt) override { return mem.recvAtomic(pkt); };
        void recvFunctional(PacketPtr pkt) override {
            mem.recvFunctional(pkt);
        };
        bool recvTimingReq(PacketPtr pkt) override {
            return mem.recvTimingReq(pkt);
        };
        void recvRespRetry() override { mem.recvRespRetry(); };

        AddrRangeList getAddrRanges() const override {
            AddrRangeList ranges;
            ranges.push_back(mem.getAddrRange());
            return ranges;
        };
    };

    MemorySystemPort port;

    std::string config_path;
    simple_mem::SimpleMem smem;

    // std::function<void(Ramulator::Request&)> read_callback;
    // std::function<void(Ramulator::Request&)> write_callback;
    bool retryReq;
    bool retryResp;
    Tick startTick;
    std::unordered_map<Addr, std::deque<PacketPtr>> outstandingReads;
    std::unordered_map<Addr, std::deque<PacketPtr>> outstandingWrites;
    //Simple maps to hold outstanding requests as a req_id:pktptr pair
    std::unordered_map<uint64_t,PacketPtr> pending_reads;
    std::unordered_map<uint64_t,PacketPtr> pending_writes;

    /**
     * Count the number of outstanding transactions so that we can
     * block any further requests until there is space in Ramulator2 and
     * the sending queue we need to buffer the response packets.
     */
    unsigned int nbrOutstandingReads;
    unsigned int nbrOutstandingWrites;

    /**
     * Queue to hold response packets until we can send them
     * back. This is needed as Ramulator2 unconditionally passes
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

	//To hold request ID
	uint64_t req_id;

    /**
     * List of special address regions
     */
    std::map<uint64_t,std::pair<uint64_t, size_t>> *addr_regions;

    //Number of accesses to said regions
    std::map<size_t,size_t> region_counts;

  public:
    PARAMS(CXLSimGem5);
    // typedef CXLSimGem5Params Params;
    CXLSimGem5(const Params &p);

    DrainState drain() override;

    virtual Port &getPort(const std::string &if_name,
                          PortID idx = InvalidPortID) override;

    void init() override;
    void startup() override;

    void resetStats() override;

  protected:
    Tick recvAtomic(PacketPtr pkt);
    void recvFunctional(PacketPtr pkt);
    bool recvTimingReq(PacketPtr pkt);
    void recvRespRetry();

    std::vector<size_t> find_special_addr_region(uint64_t addr, size_t size);
};

} // namespace memory
} // namespace gem5

#endif // __MEM_CXLSIMGEM5_HH__
