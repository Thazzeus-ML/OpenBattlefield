// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
#include "OpenBattlefieldCore/ObRemeshScheduler.h"

namespace ob
{

void RemeshScheduler::Request(ChunkCoord coord)
{
    if (m_inFlight.count(coord) != 0)
    {
        m_requeue.insert(coord);
        return;
    }
    if (m_queued.insert(coord).second)
    {
        m_queue.push_back(coord);
    }
}

bool RemeshScheduler::TryStart(ChunkCoord& outCoord, uint64_t& outTicket)
{
    if (m_queue.empty())
    {
        return false;
    }
    outCoord = m_queue.front();
    m_queue.pop_front();
    m_queued.erase(outCoord);
    outTicket = m_nextTicket++;
    m_inFlight[outCoord] = outTicket;
    return true;
}

bool RemeshScheduler::Complete(ChunkCoord coord, uint64_t ticket)
{
    const auto it = m_inFlight.find(coord);
    if (it == m_inFlight.end() || it->second != ticket)
    {
        return false;
    }
    m_inFlight.erase(it);
    if (m_requeue.erase(coord) != 0 && m_queued.insert(coord).second)
    {
        m_queue.push_back(coord);
    }
    return true;
}

void RemeshScheduler::Reset()
{
    m_queue.clear();
    m_queued.clear();
    m_inFlight.clear();
    m_requeue.clear();
}

} // namespace ob
