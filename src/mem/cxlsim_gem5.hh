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

class PageRegion {
  private:
    size_t page_size; // Size of each page in bytes
    size_t max_size;  // Number of max pages that can be accomodated
    // Set containing all the unique pages for the region
    std::unordered_set<uint64_t> page_store;
  
  public:
    PageRegion(size_t page_size, size_t num_max_pages)
        : page_size(page_size), max_size(num_max_pages) {}
    /**
     * Align address to page coundary
     */
    uint64_t align(uint64_t addr) {
      int bits = (int)std::ceil(std::log2(page_size));
      return (addr >> bits) << bits;
    }
    /**
     * Set/Get max size
     */
    void setMaxSize(size_t s) {max_size = s;}
    size_t getMaxSize() {return max_size;}
    // Get current size
    size_t getSize() {return page_store.size();}
    /**
     * Try adding a page to this region. Return true if added, false if not
     */
    typedef enum InsertStatus {
      EXISTS,  // The page already exists, don't need to add it
      SUCCESS, // The page wasn't there before, added it successfully
      FAILED   // There isn't any more space in the region, cannot add it
    } InsertStatus;
    InsertStatus insert(uint64_t addr) {
      // Align address
      addr = align(addr);
      // Check if page exists
      if (page_store.find(addr) != page_store.end()) {
        // Page already exists
        return InsertStatus::EXISTS;
      } else {
        // Page does not exist in region
        // If the page store is full, cannot add anymore
        if (page_store.size() >= max_size)
          return InsertStatus::FAILED;
        else {
          // Add the page to page_store
          page_store.insert(addr);
          return InsertStatus::SUCCESS;
        }
      }
    }
};

class PageManager
{
  private:
    std::unique_ptr<PageRegion> dam_temp;
    size_t page_size; //In Bytes
    size_t dam_size; //In number of pages
    size_t num_mapped_pages;
  public:
    PageManager(size_t page_size,size_t dam_size) : 
    page_size(page_size), dam_size(dam_size)
    {
        // Warmup
        // dam_table = std::make_unique<PageRegion>(page_size,dam_table_size);
        dam_temp = std::make_unique<PageRegion>(page_size,dam_size);
        // cxl_table = std::make_unique<PageRegion>(page_size,cxl_table_size);
        // cxl_temp = std::make_unique<PageRegion>(page_size,cxl_table_size);
    }
    void warmup(std::unordered_set<uint64_t> *mapped_regions,std::map<uint64_t,std::pair<uint64_t,size_t>> *special_addr_regions)
    {
        //Calculate the size of the tables mapped to DAM in number of pages
        num_mapped_pages = 0;
        //Go through each region, check if it is within the mapped regions, if it is then add its size
        for(auto &x:*special_addr_regions)
        {
            size_t region_id = x.second.second;
            if(mapped_regions->find(region_id)!=mapped_regions->end())
            {
                //It is a mapped region
                uint64_t num_pages = (uint64_t)std::ceil((x.second.first - x.first)/page_size);
                num_mapped_pages += num_pages;
            }
        }
        //Reset the dam temp sizes
        panic_if(dam_size<num_mapped_pages,"DAM size (%lu) is less than number of mapped pages (%lu)",dam_size,num_mapped_pages);
        dam_temp->setMaxSize(dam_size-num_mapped_pages);
        //Number of mapped pages 
        std::cout<<"Added "<<num_mapped_pages<<" DAM table pages"<<std::endl;
        //Number of mapped pages 
        std::cout<<"MaxSize of DAM temp table "<<dam_temp->getMaxSize()<<std::endl;
    }
    bool isCXLBound(uint64_t addr)
    {
        //Return true to say that this access goes to CXL, false means it goes to DAM

        panic_if(dam_temp->getSize()+num_mapped_pages>dam_size,"Exceeded DAM size");

        //For now we only bother about non table pages
        //So we assume any address reaching here is non table
        //Check if address already in dam
        PageRegion::InsertStatus s = dam_temp->insert(addr);
        switch(s)
        {
            case PageRegion::InsertStatus::EXISTS:
            {
                //Page already exists in dam. send it there
                return false;
            }
            case PageRegion::InsertStatus::SUCCESS:
            {
                //This means page wasn't found in DAM, but was added to it
                //Direct this access to CXL
                return false;
            }
            case PageRegion::InsertStatus::FAILED:
            {
                //The page wasn't found in DAM, and adding it wasn't successful
                //So this page will be on CXL
                //Send it there
                return true;
            }
            default:
              panic("Shouldn't have reached here");
        }
    }
    std::string print()
    {
        std::stringstream oss;
        oss<<"Page Size: "<<page_size<<" Bytes\n"
           <<"DAM Pages: "<<dam_size<<" Pages\n"
           <<"DAM Tables: "<<num_mapped_pages<<" Pages\n"
           <<"DAM temp: "<<dam_temp->getSize()<<" Pages\n";
          return oss.str();
    }
};
  
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
    std::unordered_map<uint64_t, PacketPtr> pendingRequests;

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
    uint64_t cxl_accesses, dam_accesses;
    uint64_t cxl_accesses_roi, dam_accesses_roi;
    /**
     * Custom req id counter
     */
    uint64_t req_id;
    /**
     * Flags to set if we want all accesses to goto DAM or if all accesses should goto CXL
     */
    bool all_dam = false, all_cxl = false;

    /**
     * Simple greey page manager to divide page allocation between CXL and DAM
     */
    std::unique_ptr<PageManager> page_mgr;
    size_t page_size; //In bytes
    size_t dam_size; //In number of pages

  public:

    typedef CXLSimGem5Params Params;
    CXLSimGem5(const Params &p);

    DrainState drain() override;

    virtual Port& getPort(const std::string& if_name,
                          PortID idx = InvalidPortID) override;

    void init() override;
    void warmUp() override;
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