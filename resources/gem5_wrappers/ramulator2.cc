#include "mem/ramulator2.hh"

#include "base/callback.hh"
#include "base/trace.hh"
#include "debug/Ramulator2.hh"
#include "debug/Drain.hh"
#include "sim/system.hh"

// spdlog collides with gem5...
#pragma push_macro("warn")
#undef warn

#include "ramulator2/src/base/base.h"
#include "ramulator2/src/base/request.h"
#include "ramulator2/src/base/config.h"
#include "ramulator2/src/frontend/frontend.h"
#include "ramulator2/src/memory_system/memory_system.h"

namespace gem5
{

namespace memory
{

Ramulator2::Ramulator2(const Params &p) :
    AbstractMemory(p),
    port(name() + ".port", *this),
    config_path(p.config_path),
    retryReq(false), retryResp(false), startTick(0),
    nbrOutstandingReads(0), nbrOutstandingWrites(0),
    sendResponseEvent([this]{ sendResponse(); }, name()),
    tickEvent([this]{ tick(); }, name())
{
    DPRINTF(Ramulator2, "Instantiated Ramulator2 \n");

    registerExitCallback([this]() { 
        ramulator2_frontend->finalize();
        ramulator2_memorysystem->finalize();
    });
}

void
Ramulator2::init()
{
    AbstractMemory::init();

    if (!port.isConnected()) {
        fatal("Ramulator2 %s is unconnected!\n", name());
    } else {
        port.sendRangeChange();
    }

    YAML::Node config = Ramulator::Config::parse_config_file(config_path, {});
    ramulator2_frontend = Ramulator::Factory::create_frontend(config);
    ramulator2_memorysystem = Ramulator::Factory::create_memory_system(config);

    ramulator2_frontend->connect_memory_system(ramulator2_memorysystem);
    ramulator2_memorysystem->connect_frontend(ramulator2_frontend);

    // if (system()->cacheLineSize() != wrapper.burstSize())
    //     fatal("Ramulator2 burst size %d does not match cache line size %d\n",
    //           wrapper.burstSize(), system()->cacheLineSize());
}

void
Ramulator2::startup()
{
    startTick = curTick();

    // kick off the clock ticks
    schedule(tickEvent, clockEdge());
}

void
Ramulator2::resetStats() {
    // wrapper.resetStats();
}

void
Ramulator2::sendResponse()
{
    assert(!retryResp);
    assert(!responseQueue.empty());

    DPRINTF(Ramulator2, "Attempting to send response\n");

    bool success = port.sendTimingResp(responseQueue.front());
    if (success) {
        responseQueue.pop_front();

        DPRINTF(Ramulator2, "Have %d read, %d write, %d responses outstanding\n",
                nbrOutstandingReads, nbrOutstandingWrites,
                responseQueue.size());

        if (!responseQueue.empty() && !sendResponseEvent.scheduled())
            schedule(sendResponseEvent, curTick());

        if (nbrOutstanding() == 0)
            signalDrainDone();
    } else {
        retryResp = true;

        DPRINTF(Ramulator2, "Waiting for response retry\n");

        assert(!sendResponseEvent.scheduled());
    }
}

unsigned int
Ramulator2::nbrOutstanding() const
{
    return nbrOutstandingReads + nbrOutstandingWrites + responseQueue.size();
}

void
Ramulator2::tick()
{
    // Only tick when it's timing mode
    if (system()->isTimingMode()) {
        ramulator2_memorysystem->tick();

        // is the connected port waiting for a retry, if so check the
        // state and send a retry if conditions have changed
        if (retryReq) {
            retryReq = false;
            port.sendRetryReq();
        }
    }

    schedule(tickEvent,
        curTick() + ramulator2_memorysystem->get_tCK() * sim_clock::as_float::ns);
}

Tick
Ramulator2::recvAtomic(PacketPtr pkt)
{
    panic_if(pkt->cacheResponding(), "Should not see packets where cache "
             "is responding");

    access(pkt);
    return 50000;   // Arbitary latency of 50ns
}

void
Ramulator2::recvFunctional(PacketPtr pkt)
{
    pkt->pushLabel(name());
    functionalAccess(pkt);

    for (auto i = responseQueue.begin(); i != responseQueue.end(); ++i)
        pkt->trySatisfyFunctional(*i);

    pkt->popLabel();
}

bool
Ramulator2::recvTimingReq(PacketPtr pkt)
{
    DPRINTF(Ramulator2, "recvTimingReq: request %s addr %#x size %d\n",
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

    bool enqueue_success = false;
    if (pkt->isRead()) 
    {
        // Generate ramulator READ request and try to send to ramulator's memory system
        enqueue_success = ramulator2_frontend->
            receive_external_requests(0, pkt->getAddr(), 0, 
            [this](Ramulator::Request& req) {
                DPRINTF(Ramulator2, "Read to %ld completed.\n", req.addr);
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
        } 
        else 
        {
            retryReq = true;
        }
    } else if (pkt->isWrite()) {
        // Generate ramulator WRITE request and try to send to ramulator's memory system
        enqueue_success = ramulator2_frontend->
            receive_external_requests(1, pkt->getAddr(), 0, 
            [this](Ramulator::Request& req) {
                DPRINTF(Ramulator2, "Write to %ld completed.\n", req.addr);
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
            accessAndRespond(pkt);
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

    return enqueue_success;
}

void
Ramulator2::recvRespRetry()
{
    DPRINTF(Ramulator2, "Retrying\n");

    assert(retryResp);
    retryResp = false;
    sendResponse();
}

void
Ramulator2::accessAndRespond(PacketPtr pkt)
{
    DPRINTF(Ramulator2, "Access for address %lld\n", pkt->getAddr());

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

        DPRINTF(Ramulator2, "Queuing response for address %lld\n",
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
Ramulator2::getPort(const std::string &if_name, PortID idx)
{
    if (if_name != "port") {
        return ClockedObject::getPort(if_name, idx);
    } else {
        return port;
    }
}

DrainState
Ramulator2::drain()
{
    // check our outstanding reads and writes and if any they need to
    // drain
    return nbrOutstanding() != 0 ? DrainState::Draining : DrainState::Drained;
}

Ramulator2::MemorySystemPort::MemorySystemPort(const std::string& _name,
                                 Ramulator2& _ramulator2)
    : ResponsePort(_name), ramulator2(_ramulator2)
{ }


} // namespace memory
} // namespace gem5

#pragma pop_macro("warn")
