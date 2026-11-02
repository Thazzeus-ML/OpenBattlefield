// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
#pragma once

#include "OpenBattlefieldCore/ObTypes.h"

#include <deque>
#include <map>
#include <set>

namespace ob
{

/// Bookkeeping for remeshing chunks off the owning thread (Docs/concurrency.md). Holds no geometry and
/// starts no threads: the owner asks it which chunk to snapshot next, hands the snapshot to a worker,
/// and asks it again when the worker's result comes back whether that result may be published.
///
/// Rules it enforces:
/// * At most one job per chunk is in flight. A chunk requested again while its job runs is started
///   again after that job completes, so its newest state is always meshed eventually.
/// * Every job has a ticket that grows monotonically for the scheduler's lifetime. A result is
///   publishable only if its ticket is the chunk's in-flight ticket, so results from before Reset()
///   or from an abandoned job are dropped.
///
/// Not thread safe: only the owning thread calls it. Workers only carry their ticket.
class OBCORE_API RemeshScheduler
{
public:
    /// Marks a chunk as needing a remesh. Duplicate requests collapse into one.
    void Request(ChunkCoord coord);

    /// Takes the oldest requested chunk that has no job in flight, marks it in flight and returns its
    /// ticket. Returns false when nothing can start.
    bool TryStart(ChunkCoord& outCoord, uint64_t& outTicket);

    /// Reports a finished job. Returns true if its result should be published: the ticket is the
    /// chunk's in-flight ticket. Either way the chunk is no longer in flight, and if it was requested
    /// meanwhile it is queued again.
    bool Complete(ChunkCoord coord, uint64_t ticket);

    /// True if the ticket is the chunk's in-flight ticket, i.e. Complete() would accept it. Lets the owner
    /// keep a chunk in flight past the worker job, for example while its collision is cooked.
    bool IsCurrent(ChunkCoord coord, uint64_t ticket) const
    {
        const auto it = m_inFlight.find(coord);
        return it != m_inFlight.end() && it->second == ticket;
    }

    /// Drops every queued request and every in-flight job; their results will not be publishable.
    /// Tickets keep growing so no result from before the reset can match a later job.
    void Reset();

    /// Chunks queued or in flight: zero means everything requested so far has been published or
    /// dropped.
    size_t PendingCount() const { return m_queue.size() + m_inFlight.size(); }
    size_t InFlightCount() const { return m_inFlight.size(); }
    bool IsInFlight(ChunkCoord coord) const { return m_inFlight.count(coord) != 0; }

private:
    std::deque<ChunkCoord> m_queue;            // FIFO of chunks ready to start
    std::set<ChunkCoord> m_queued;             // members of m_queue
    std::map<ChunkCoord, uint64_t> m_inFlight; // chunk -> ticket of its running job
    std::set<ChunkCoord> m_requeue;            // requested while in flight
    uint64_t m_nextTicket = 1;
};

} // namespace ob
