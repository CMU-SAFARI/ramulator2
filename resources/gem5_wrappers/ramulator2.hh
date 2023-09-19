#ifndef __MEM_RAMULATOR2_HH__
#define __MEM_RAMULATOR2_HH__

#include <functional>
#include <deque>
#include <unordered_map>

#include "mem/abstract_mem.hh"
#include "params/Ramulator2.hh"

// Forward declare Ramulator2 top-level components
namespace Ramulator
{
  class IFrontEnd;
  class IMemorySystem;
}


namespace gem5
{

namespace memory
{


class Ramulator2 : public AbstractMemory
{
  private:
    class MemorySystemPort : public ResponsePort
    {

      private:
        Ramulator2& ramulator2;

      public:
        MemorySystemPort(const std::string& _name, Ramulator2& _ramulator2);

      protected:
        Tick recvAtomic(PacketPtr pkt) override { return ramulator2.recvAtomic(pkt); };
        void recvFunctional(PacketPtr pkt) override { ramulator2.recvFunctional(pkt); };
        bool recvTimingReq(PacketPtr pkt) override { return ramulator2.recvTimingReq(pkt); };
        void recvRespRetry() override { ramulator2.recvRespRetry(); };

        AddrRangeList getAddrRanges() const override
        {
          AddrRangeList ranges;
          ranges.push_back(ramulator2.getAddrRange());
          return ranges;
        };
    };

    MemorySystemPort port;

    std::string config_path;
    Ramulator::IFrontEnd* ramulator2_frontend;
    Ramulator::IMemorySystem* ramulator2_memorysystem;

    // std::function<void(Ramulator::Request&)> read_callback;
    // std::function<void(Ramulator::Request&)> write_callback;
    bool retryReq;
    bool retryResp;
    Tick startTick;
    std::unordered_map<Addr, std::deque<PacketPtr>> outstandingReads;
    std::unordered_map<Addr, std::deque<PacketPtr>> outstandingWrites;

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

  public:

    typedef Ramulator2Params Params;
    Ramulator2(const Params &p);

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

#endif // __MEM_RAMULATOR2_HH__
