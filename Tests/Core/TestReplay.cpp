// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
#include "ObTest.h"

#include "OpenBattlefieldCore/ObHash.h"
#include "OpenBattlefieldCore/ObSerialization.h"
#include "OpenBattlefieldCore/ObTerrainWorld.h"

using namespace ob;

namespace
{
WorldConfig Cfg()
{
    WorldConfig cfg;
    cfg.seed = 0xC0FFEE;
    cfg.chunkSize = 16;
    cfg.minChunk = ChunkCoord(-6, -6, -3);
    cfg.maxChunk = ChunkCoord(5, 5, 2);
    cfg.maxOperationRadius = 16;
    cfg.bedrockHeight = -40;
    return cfg;
}

// Deterministic pseudo-random artillery sequence derived from Mix64.
std::vector<TerrainOperation> Artillery(const WorldConfig& cfg, size_t count, uint64_t seed)
{
    std::vector<TerrainOperation> ops;
    ProceduralGenerator gen(cfg);
    for (size_t i = 0; i < count; ++i)
    {
        const uint64_t r = Mix64(seed + i * 7919);
        TerrainOperation op;
        op.operationId = 1000 + i;
        op.sequenceNumber = i + 1;
        op.worldId = cfg.worldId;
        const int64_t x = static_cast<int64_t>(r % 80) - 40;
        const int64_t y = static_cast<int64_t>((r >> 8) % 80) - 40;
        const int64_t h = gen.SurfaceHeight(x, y);
        op.center = VoxelCoord(x, y, h + static_cast<int64_t>((r >> 16) % 5) - 2);
        op.radius = 2 + static_cast<uint32_t>((r >> 24) % 7);
        if ((r >> 40) % 4 == 0)
        {
            op.type = OperationType::AddSphere;
            op.material = Material::Rock;
        }
        else
            op.type = OperationType::SubtractSphere;
        ops.push_back(op);
    }
    return ops;
}
} // namespace

OB_TEST(ReplayFromSeedAndJournalReproducesStateHash)
{
    const WorldConfig cfg = Cfg();
    const std::vector<TerrainOperation> ops = Artillery(cfg, 200, 1);

    TerrainWorld live(cfg);
    OB_REQUIRE(Succeeded(live.ReplayJournal(ops)));
    OB_CHECK(live.ModifiedChunkCount() > 1);
    const StateHash liveHash = live.ComputeStateHash();

    // Replay 1: same ops into a fresh world.
    TerrainWorld replay(cfg);
    size_t applied = 0;
    OB_REQUIRE(Succeeded(replay.ReplayJournal(live.Journal(), &applied)));
    OB_CHECK_EQ(applied, ops.size());
    OB_CHECK_EQ(replay.ComputeStateHash(), liveHash);
    OB_CHECK_EQ(replay.ComputeJournalHash(), live.ComputeJournalHash());
    OB_CHECK_EQ(replay.Version(), live.Version());
    OB_CHECK_EQ(replay.LastSequence(), live.LastSequence());

    // Replay 2: pre-load unrelated chunks first; load order must not matter.
    TerrainWorld shuffled(cfg);
    Chunk* c = nullptr;
    for (int32_t z = 2; z >= -3; --z)
        for (int32_t x = 5; x >= -6; x -= 3)
            OB_REQUIRE(Succeeded(shuffled.EnsureChunk(ChunkCoord(x, -x, z), c)));
    OB_REQUIRE(Succeeded(shuffled.ReplayJournal(ops)));
    OB_CHECK_EQ(shuffled.ComputeStateHash(), liveHash);

    // Replay 3: a different seed must not match.
    WorldConfig other = cfg;
    other.seed += 1;
    TerrainWorld different(other);
    std::vector<TerrainOperation> otherOps = ops; // same ops, different foundation
    OB_REQUIRE(Succeeded(different.ReplayJournal(otherOps)));
    OB_CHECK(different.ComputeStateHash() != liveHash);

    // Replay 4: dropping one operation changes the result.
    std::vector<TerrainOperation> missing(ops.begin(), ops.end() - 1);
    TerrainWorld partial(cfg);
    OB_REQUIRE(Succeeded(partial.ReplayJournal(missing)));
    OB_CHECK(partial.ComputeStateHash() != liveHash);
}

OB_TEST(ReplayStopsAtFirstRejection)
{
    const WorldConfig cfg = Cfg();
    std::vector<TerrainOperation> ops = Artillery(cfg, 10, 2);
    ops[4].radius = 999; // invalid
    TerrainWorld world(cfg);
    size_t applied = 0;
    OB_CHECK_EQ(world.ReplayJournal(ops, &applied), ResultCode::RadiusTooLarge);
    OB_CHECK_EQ(applied, size_t(4));
    OB_CHECK_EQ(world.Journal().size(), size_t(4));
}

OB_TEST(SnapshotRoundTripPreservesStateAndAcceptsFurtherOperations)
{
    const WorldConfig cfg = Cfg();
    const std::vector<TerrainOperation> ops = Artillery(cfg, 120, 3);
    TerrainWorld live(cfg);
    OB_REQUIRE(Succeeded(live.ReplayJournal(ops)));

    ByteWriter w;
    live.SerializeSnapshot(w);
    std::printf("  snapshot bytes = %zu for %zu modified chunks\n", w.Size(), live.ModifiedChunkCount());

    std::unique_ptr<TerrainWorld> loaded;
    ByteReader r(w.Buffer());
    OB_REQUIRE(Succeeded(TerrainWorld::DeserializeSnapshot(r, loaded)));
    OB_REQUIRE(loaded != nullptr);
    OB_CHECK(r.AtEnd());
    OB_CHECK(loaded->Config() == cfg);
    OB_CHECK_EQ(loaded->ComputeStateHash(), live.ComputeStateHash());
    OB_CHECK_EQ(loaded->ComputeJournalHash(), live.ComputeJournalHash());
    OB_CHECK_EQ(loaded->Version(), live.Version());
    OB_CHECK_EQ(loaded->LastSequence(), live.LastSequence());
    OB_CHECK_EQ(loaded->ModifiedChunkCount(), live.ModifiedChunkCount());
    OB_CHECK_EQ(loaded->Stats().chunksDeserialized, static_cast<uint64_t>(live.ModifiedChunkCount()));

    // Per-chunk versions survive.
    live.ForEachChunk(
        [&](const Chunk& chunk)
        {
            if (!chunk.IsModified())
                return;
            const Chunk* other = loaded->FindChunk(chunk.Coord());
            OB_REQUIRE(other != nullptr);
            OB_CHECK_EQ(other->Version(), chunk.Version());
        });

    // Duplicate detection state is restored from the journal tail.
    OB_CHECK_EQ(loaded->ApplyOperation(ops.back()).code, ResultCode::StaleSequence);
    TerrainOperation reused = ops.back();
    reused.sequenceNumber += 1;
    OB_CHECK_EQ(loaded->ApplyOperation(reused).code, ResultCode::DuplicateOperation);

    // Both worlds accept the same next operation and stay in lockstep.
    TerrainOperation next = ops.back();
    next.operationId = 999999;
    next.sequenceNumber += 1;
    OB_REQUIRE(live.ApplyOperation(next).Accepted());
    OB_REQUIRE(loaded->ApplyOperation(next).Accepted());
    OB_CHECK_EQ(loaded->ComputeStateHash(), live.ComputeStateHash());
}

OB_TEST(SnapshotDeserializationRejectsCorruptInput)
{
    const WorldConfig cfg = Cfg();
    TerrainWorld live(cfg);
    OB_REQUIRE(Succeeded(live.ReplayJournal(Artillery(cfg, 20, 4))));
    ByteWriter w;
    live.SerializeSnapshot(w);
    const std::vector<uint8_t>& good = w.Buffer();

    std::unique_ptr<TerrainWorld> out;
    {
        std::vector<uint8_t> bad = good;
        bad[0] = 'X';
        ByteReader r(bad);
        OB_CHECK_EQ(TerrainWorld::DeserializeSnapshot(r, out), ResultCode::SerializationBadMagic);
        OB_CHECK(out == nullptr);
    }
    {
        std::vector<uint8_t> bad = good;
        bad[4] = 0x7F; // schema version
        ByteReader r(bad);
        OB_CHECK_EQ(TerrainWorld::DeserializeSnapshot(r, out), ResultCode::SerializationVersionMismatch);
    }
    for (size_t cut : {size_t(3), size_t(10), size_t(40), good.size() / 2, good.size() - 1})
    {
        std::vector<uint8_t> bad(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(cut));
        ByteReader r(bad);
        const ResultCode code = TerrainWorld::DeserializeSnapshot(r, out);
        OB_CHECK(Failed(code));
        OB_CHECK(out == nullptr);
    }
    {
        // Flip every byte of the journal/chunk payload one at a time at a few offsets: must never
        // succeed silently with a different state hash, and must never crash.
        for (size_t off = 6; off < good.size(); off += 97)
        {
            std::vector<uint8_t> bad = good;
            bad[off] ^= 0xFF;
            ByteReader r(bad);
            const ResultCode code = TerrainWorld::DeserializeSnapshot(r, out);
            if (Succeeded(code))
            {
                // A flip that still parses must be an inconsequential field (none exist in this
                // format) or must produce a different hash, never a silently identical world.
                OB_CHECK(out->ComputeStateHash() != live.ComputeStateHash() ||
                         out->ComputeJournalHash() != live.ComputeJournalHash());
            }
        }
    }
}

OB_TEST(SnapshotWithRepeatedOperationIdIsRejected)
{
    const WorldConfig cfg = Cfg();
    TerrainWorld live(cfg);
    OB_REQUIRE(Succeeded(live.ReplayJournal(Artillery(cfg, 2, 6))));
    ByteWriter w;
    live.SerializeSnapshot(w);

    // Locate the second operation: magic(4) + schema(2) + config + worldVersion(8) + lastSequence(8)
    // + opCount(4) + one serialized operation.
    ByteWriter cfgBytes;
    cfg.Serialize(cfgBytes);
    ByteWriter opBytes;
    live.Journal()[0].Serialize(opBytes);
    const size_t secondOp = 4 + 2 + cfgBytes.Size() + 8 + 8 + 4 + opBytes.Size();
    std::vector<uint8_t> bad = w.Buffer();
    OB_REQUIRE(bad.size() > secondOp + 8);
    // Overwrite its id (first 8 bytes of the operation) with the first operation's id.
    for (size_t i = 0; i < 8; ++i)
        bad[secondOp + i] = w.Buffer()[4 + 2 + cfgBytes.Size() + 8 + 8 + 4 + i];

    std::unique_ptr<TerrainWorld> out;
    ByteReader r(bad);
    OB_CHECK_EQ(TerrainWorld::DeserializeSnapshot(r, out), ResultCode::SerializationCorrupt);
    OB_CHECK(out == nullptr);

    // Sanity: the unmodified bytes still load.
    ByteReader good(w.Buffer());
    OB_CHECK_EQ(TerrainWorld::DeserializeSnapshot(good, out), ResultCode::Ok);
}

OB_TEST(JournalSerializationRoundTripsAndRebuilds)
{
    const WorldConfig cfg = Cfg();
    TerrainWorld live(cfg);
    OB_REQUIRE(Succeeded(live.ReplayJournal(Artillery(cfg, 60, 5))));
    ByteWriter w;
    live.SerializeJournal(w);

    WorldConfig loadedCfg;
    std::vector<TerrainOperation> loadedOps;
    ByteReader r(w.Buffer());
    OB_REQUIRE(Succeeded(TerrainWorld::DeserializeJournal(r, loadedCfg, loadedOps)));
    OB_CHECK(loadedCfg == cfg);
    OB_CHECK_EQ(loadedOps.size(), live.Journal().size());
    TerrainWorld rebuilt(loadedCfg);
    OB_REQUIRE(Succeeded(rebuilt.ReplayJournal(loadedOps)));
    OB_CHECK_EQ(rebuilt.ComputeStateHash(), live.ComputeStateHash());
}

OB_TEST_MAIN()
