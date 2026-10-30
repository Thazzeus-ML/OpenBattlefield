// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
#include "ObTest.h"

#include "OpenBattlefieldCore/ObRemeshScheduler.h"

using namespace ob;

OB_TEST(RequestsStartInFifoOrderAndCollapseDuplicates)
{
    RemeshScheduler s;
    s.Request(ChunkCoord(1, 0, 0));
    s.Request(ChunkCoord(0, 0, 0));
    s.Request(ChunkCoord(1, 0, 0));
    OB_CHECK_EQ(s.PendingCount(), size_t(2));

    ChunkCoord c;
    uint64_t a = 0, b = 0;
    OB_CHECK(s.TryStart(c, a));
    OB_CHECK(c == ChunkCoord(1, 0, 0));
    OB_CHECK(s.TryStart(c, b));
    OB_CHECK(c == ChunkCoord(0, 0, 0));
    OB_CHECK(b > a);
    OB_CHECK(!s.TryStart(c, b));
    OB_CHECK_EQ(s.InFlightCount(), size_t(2));
    OB_CHECK_EQ(s.PendingCount(), size_t(2));
}

OB_TEST(CompletionPublishesOnlyTheInFlightTicket)
{
    RemeshScheduler s;
    const ChunkCoord k(2, 3, 4);
    s.Request(k);
    ChunkCoord c;
    uint64_t t = 0;
    OB_CHECK(s.TryStart(c, t));
    OB_CHECK(!s.Complete(k, t + 1));               // not this chunk's ticket
    OB_CHECK(!s.Complete(ChunkCoord(0, 0, 0), t)); // not this chunk
    OB_CHECK(s.IsInFlight(k));
    OB_CHECK(s.Complete(k, t));
    OB_CHECK(!s.Complete(k, t)); // already completed
    OB_CHECK_EQ(s.PendingCount(), size_t(0));
}

OB_TEST(RequestWhileInFlightStartsAgainAfterCompletion)
{
    RemeshScheduler s;
    const ChunkCoord k(0, 0, 0);
    s.Request(k);
    ChunkCoord c;
    uint64_t first = 0;
    OB_CHECK(s.TryStart(c, first));

    // The chunk changes while its job runs: no second concurrent job...
    s.Request(k);
    s.Request(k);
    uint64_t second = 0;
    OB_CHECK(!s.TryStart(c, second));
    OB_CHECK_EQ(s.PendingCount(), size_t(1));

    // ...the first result is still publishable (it is newer than what is on screen), and the chunk is
    // queued once more so its newest state gets meshed.
    OB_CHECK(s.Complete(k, first));
    OB_CHECK_EQ(s.PendingCount(), size_t(1));
    OB_CHECK(s.TryStart(c, second));
    OB_CHECK(c == k);
    OB_CHECK(second > first);
    OB_CHECK(s.Complete(k, second));
    OB_CHECK(!s.TryStart(c, second));
    OB_CHECK_EQ(s.PendingCount(), size_t(0));
}

OB_TEST(ResetDropsQueuedAndInFlightResults)
{
    RemeshScheduler s;
    const ChunkCoord k(0, 0, 1);
    s.Request(k);
    s.Request(ChunkCoord(5, 5, 5));
    ChunkCoord c;
    uint64_t before = 0;
    OB_CHECK(s.TryStart(c, before));
    s.Request(k); // requeue flag must not survive the reset either
    s.Reset();
    OB_CHECK_EQ(s.PendingCount(), size_t(0));
    OB_CHECK(!s.TryStart(c, before));

    // A job started after the reset never shares a ticket with one from before it.
    s.Request(k);
    uint64_t after = 0;
    OB_CHECK(s.TryStart(c, after));
    OB_CHECK(after > before);
    OB_CHECK(!s.Complete(k, before));
    OB_CHECK(s.Complete(k, after));
    OB_CHECK_EQ(s.PendingCount(), size_t(0));
}

OB_TEST_MAIN()
