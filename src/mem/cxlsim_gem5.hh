#ifndef __MEM_CXLSIMGEM5_HH__
#define __MEM_CXLSIMGEM5_HH__

#include <functional>
#include <deque>
#include <unordered_map>

#include "cxlsim/include/CXLWrapper.h"

#include "mem/abstract_mem.hh"
#include "params/CXLSimGem5.hh"

namespace gem5
{

namespace memory
{


class CXLSimGem5 : public AbstractMemory
{
  private:
    class MemorySystemPort : public ResponsePort
    {

      private:
        CXLSimGem5& cxl_mem;

      public:
        MemorySystemPort(const std::string& _name, CXLSimGem5& _cxl_mem);

      protected:
        Tick recvAtomic(PacketPtr pkt) override { return cxl_mem.recvAtomic(pkt); };
        void recvFunctional(PacketPtr pkt) override { cxl_mem.recvFunctional(pkt); };
        bool recvTimingReq(PacketPtr pkt) override { return cxl_mem.recvTimingReq(pkt); };
        void recvRespRetry() override { cxl_mem.recvRespRetry(); };

        AddrRangeList getAddrRanges() const override
        {
          AddrRangeList ranges;
          ranges.push_back(cxl_mem.getAddrRange());
          return ranges;
        };
    };

    MemorySystemPort port;

    std::string config_path;

    // Actual memory model
    std::unique_ptr<CXL::CXLWrapper> mem_model;

    std::function<void(uint64_t req_id)> read_callback;
    std::function<void(uint64_t req_id)> write_callback;
    bool retryReq;
    bool retryResp;
    Tick startTick;
    std::unordered_map<Addr, std::deque<PacketPtr>> outstandingReads;
    std::unordered_map<Addr, std::deque<PacketPtr>> outstandingWrites;

    /**
     * Count the number of outstanding transactions so that we can
     * block any further requests until there is space in CXLSimGem5 and
     * the sending queue we need to buffer the response packets.
     */
    unsigned int nbrOutstandingReads;
    unsigned int nbrOutstandingWrites;

    /**
     * Queue to hold response packets until we can send them
     * back. This is needed as CXLSimGem5 unconditionally passes
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

  public:

    typedef CXLSimGem5Params Params;
    CXLSimGem5(const Params &p);

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

#endif // __MEM_CXLSIMGEM5_HH__