// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
#include "ObTest.h"

#include "OpenBattlefieldCore/ObCoordinates.h"
#include "OpenBattlefieldCore/ObTerrainWorld.h"

using namespace ob;

namespace
{
WorldConfig Cfg(uint32_t chunkSize = 16)
{
    WorldConfig cfg;
    cfg.seed = 777;
    cfg.chunkSize = chunkSize;
    cfg.minChunk = ChunkCoord(-4, -4, -3);
    cfg.maxChunk = ChunkCoord(3, 3, 2);
    cfg.maxOperationRadius = 24;
    cfg.bedrockHeight = -40;
    return cfg;
}

TerrainOperation Subtract(OperationId id, SequenceNumber seq, VoxelCoord center, uint32_t radius)
{
    TerrainOperation op;
    op.operationId = id;
    op.type = OperationType::SubtractSphere;
    op.worldId = 1;
    op.sequenceNumber = seq;
    op.center = center;
    op.radius = radius;
    return op;
}

TerrainOperation Add(OperationId id, SequenceNumber seq, VoxelCoord center, uint32_t radius, Material m)
{
    TerrainOperation op = Subtract(id, seq, center, radius);
    op.type = OperationType::AddSphere;
    op.material = m;
    return op;
}

// Brute-force oracle: expected material after applying ops in order, computed per voxel from the
// generator without any chunk structure at all.
Material Oracle(const TerrainWorld& fresh, const std::vector<TerrainOperation>& ops, const VoxelCoord& v)
{
    Material m = fresh.Generator().MaterialAt(v);
    for (const TerrainOperation& op : ops)
    {
        if (!InsideSphere(v.x - op.center.x, v.y - op.center.y, v.z - op.center.z, op.radius))
            continue;
        if (op.type == OperationType::SubtractSphere)
        {
            if (IsSolid(m) && !IsIndestructible(m))
                m = Material::Air;
        }
        else if (m == Material::Air)
            m = op.material;
    }
    return m;
}

struct CountingSink : InstrumentationSink
{
    int generated = 0;
    int accepted = 0;
    int rejected = 0;
    void OnEvent(const EventData& e) override
    {
        if (e.kind == EventKind::ChunkGenerated)
            ++generated;
        else if (e.kind == EventKind::OperationAccepted)
            ++accepted;
        else if (e.kind == EventKind::OperationRejected)
            ++rejected;
    }
};
} // namespace

OB_TEST(InvalidConfigMakesWorldRefuseWork)
{
    WorldConfig cfg = Cfg();
    cfg.chunkSize = 3;
    TerrainWorld world(cfg);
    OB_CHECK_EQ(world.Status(), ResultCode::InvalidChunkSize);
    Chunk* c = nullptr;
    OB_CHECK_EQ(world.EnsureChunk(ChunkCoord(), c), ResultCode::WorldNotInitialized);
    OB_CHECK_EQ(world.ApplyOperation(Subtract(1, 1, VoxelCoord(), 2)).code, ResultCode::WorldNotInitialized);
}

OB_TEST(SphericalSubtractionMatchesOracleInsideOneChunk)
{
    TerrainWorld world(Cfg());
    const TerrainWorld fresh(Cfg());
    const int64_t h = world.Generator().SurfaceHeight(8, 8);
    const TerrainOperation op = Subtract(1, 1, VoxelCoord(8, 8, h), 4);
    const OperationResult r = world.ApplyOperation(op);
    OB_REQUIRE(r.Accepted());
    OB_CHECK(r.changedVoxels > 0);
    OB_CHECK_EQ(r.candidateChunks.size(), size_t(1));
    OB_CHECK_EQ(r.modifiedChunks.size(), size_t(1));

    const std::vector<TerrainOperation> ops = {op};
    for (int64_t z = h - 6; z <= h + 6; ++z)
        for (int64_t y = 2; y <= 14; ++y)
            for (int64_t x = 2; x <= 14; ++x)
            {
                const VoxelCoord v(x, y, z);
                OB_CHECK(world.SampleMaterial(v) == Oracle(fresh, ops, v));
            }
}

OB_TEST(BoundaryCrossingOperationIsConsistentAcrossChunks)
{
    // Centre on the corner shared by eight chunks, radius large enough to reach well into each.
    // Raise the surface so the whole sphere is solid and all eight chunks change.
    WorldConfig cfg = Cfg();
    cfg.baseHeight = 60;
    TerrainWorld world(cfg);
    const TerrainWorld fresh(cfg);
    const TerrainOperation op = Subtract(5, 1, VoxelCoord(0, 0, 0), 7);
    const OperationResult r = world.ApplyOperation(op);
    OB_REQUIRE(r.Accepted());
    OB_CHECK_EQ(r.candidateChunks.size(), size_t(8));
    OB_CHECK_EQ(r.modifiedChunks.size(), size_t(8));

    const std::vector<TerrainOperation> ops = {op};
    uint64_t expectedChanged = 0;
    for (int64_t z = -9; z <= 9; ++z)
        for (int64_t y = -9; y <= 9; ++y)
            for (int64_t x = -9; x <= 9; ++x)
            {
                const VoxelCoord v(x, y, z);
                const Material expected = Oracle(fresh, ops, v);
                OB_CHECK(world.SampleMaterial(v) == expected);
                if (expected != fresh.Generator().MaterialAt(v))
                    ++expectedChanged;
            }
    OB_CHECK_EQ(r.changedVoxels, expectedChanged);

    // Every modified chunk has version 1 and a dirty region touching its shared corner.
    for (const ChunkCoord& cc : r.modifiedChunks)
    {
        const Chunk* chunk = world.FindChunk(cc);
        OB_REQUIRE(chunk != nullptr);
        OB_CHECK_EQ(chunk->Version(), ChunkVersion(1));
        OB_CHECK(chunk->HasDirtyRegion());
    }
}

OB_TEST(ChunkSizeDoesNotChangeResult)
{
    // The same ordered operations on worlds with different chunk sizes must produce identical voxels.
    TerrainWorld w16(Cfg(16));
    TerrainWorld w32(Cfg(32));
    const TerrainOperation ops[] = {
        Subtract(1, 1, VoxelCoord(3, -2, 1), 9), Add(2, 2, VoxelCoord(-5, 4, 6), 5, Material::Rock),
        Subtract(3, 3, VoxelCoord(-15, 17, -3), 12), Subtract(4, 4, VoxelCoord(31, -33, 0), 6)};
    for (const TerrainOperation& op : ops)
    {
        OB_REQUIRE(w16.ApplyOperation(op).Accepted());
        OB_REQUIRE(w32.ApplyOperation(op).Accepted());
    }
    for (int64_t z = -20; z <= 20; z += 1)
        for (int64_t y = -48; y <= 48; y += 1)
            for (int64_t x = -48; x <= 48; x += 1)
                OB_CHECK(w16.SampleMaterial(VoxelCoord(x, y, z)) == w32.SampleMaterial(VoxelCoord(x, y, z)));
}

OB_TEST(BedrockIsIndestructible)
{
    WorldConfig cfg = Cfg();
    cfg.bedrockHeight = -10;
    TerrainWorld world(cfg);
    const OperationResult r = world.ApplyOperation(Subtract(1, 1, VoxelCoord(0, 0, -10), 4));
    OB_REQUIRE(r.Accepted());
    for (int64_t z = -14; z <= -10; ++z)
        OB_CHECK(world.SampleMaterial(VoxelCoord(0, 0, z)) == Material::Bedrock);
    OB_CHECK(world.SampleMaterial(VoxelCoord(0, 0, -9)) == Material::Air);
}

OB_TEST(AddSphereOnlyFillsAir)
{
    TerrainWorld world(Cfg());
    const int64_t h = world.Generator().SurfaceHeight(0, 0);
    const OperationResult r = world.ApplyOperation(Add(1, 1, VoxelCoord(0, 0, h), 3, Material::Rock));
    OB_REQUIRE(r.Accepted());
    OB_CHECK(world.SampleMaterial(VoxelCoord(0, 0, h)) == Material::Grass); // untouched existing solid
    OB_CHECK(world.SampleMaterial(VoxelCoord(0, 0, h + 1)) == Material::Rock);
    OB_CHECK(world.SampleMaterial(VoxelCoord(0, 0, h + 3)) == Material::Rock);
    OB_CHECK(world.SampleMaterial(VoxelCoord(0, 0, h + 4)) == Material::Air);
}

OB_TEST(VersionsDirtyRegionsAndJournalAreMaintained)
{
    TerrainWorld world(Cfg());
    OB_CHECK_EQ(world.Version(), WorldVersion(0));
    const int64_t h = world.Generator().SurfaceHeight(8, 8);

    OperationResult r = world.ApplyOperation(Subtract(1, 1, VoxelCoord(8, 8, h), 2));
    OB_REQUIRE(r.Accepted());
    OB_CHECK_EQ(world.Version(), WorldVersion(1));
    OB_CHECK_EQ(world.Journal().size(), size_t(1));
    OB_CHECK_EQ(world.PendingDirtyChunkCount(), size_t(1));

    Chunk* chunk = world.FindChunk(r.modifiedChunks[0]);
    OB_REQUIRE(chunk != nullptr);
    OB_CHECK_EQ(chunk->Version(), ChunkVersion(1));
    const DirtyRegion d = chunk->ConsumeDirtyRegion();
    OB_CHECK(d.any);
    OB_CHECK(d.max.x - d.min.x <= 4 && d.max.y - d.min.y <= 4 && d.max.z - d.min.z <= 4);
    OB_CHECK(!chunk->HasDirtyRegion());

    std::vector<ChunkCoord> dirty;
    world.TakeDirtyChunks(dirty);
    OB_CHECK_EQ(dirty.size(), size_t(1));
    OB_CHECK_EQ(world.PendingDirtyChunkCount(), size_t(0));

    // Same place again changes nothing but is still journaled (validated no-op) and bumps the world
    // version; the chunk version is unchanged because its content did not change.
    r = world.ApplyOperation(Subtract(2, 2, VoxelCoord(8, 8, h), 2));
    OB_REQUIRE(r.Accepted());
    OB_CHECK_EQ(r.changedVoxels, uint64_t(0));
    OB_CHECK_EQ(r.modifiedChunks.size(), size_t(0));
    OB_CHECK_EQ(world.Version(), WorldVersion(2));
    OB_CHECK_EQ(chunk->Version(), ChunkVersion(1));
    OB_CHECK_EQ(world.Stats().operationsNoOp, uint64_t(1));

    // An operation in Air only is accepted and journaled but modifies no chunk.
    r = world.ApplyOperation(Subtract(3, 3, VoxelCoord(8, 8, h + 20), 2));
    OB_REQUIRE(r.Accepted());
    OB_CHECK_EQ(r.changedVoxels, uint64_t(0));
    OB_CHECK_EQ(world.Journal().size(), size_t(3));
}

OB_TEST(DuplicatesAndStaleSequencesAreRejected)
{
    CountingSink sink;
    TerrainWorld world(Cfg(), &sink);
    const int64_t h = world.Generator().SurfaceHeight(0, 0);
    OB_REQUIRE(world.ApplyOperation(Subtract(10, 5, VoxelCoord(0, 0, h), 3)).Accepted());
    const StateHash before = world.ComputeStateHash();

    // Exact retransmit: same id and sequence.
    OB_CHECK_EQ(world.ApplyOperation(Subtract(10, 5, VoxelCoord(0, 0, h), 3)).code,
                ResultCode::StaleSequence);
    // Old sequence with a new id.
    OB_CHECK_EQ(world.ApplyOperation(Subtract(11, 4, VoxelCoord(0, 0, h), 3)).code,
                ResultCode::StaleSequence);
    // Reused id with a fresh sequence.
    OB_CHECK_EQ(world.ApplyOperation(Subtract(10, 6, VoxelCoord(0, 0, h), 3)).code,
                ResultCode::DuplicateOperation);
    OB_CHECK_EQ(world.ComputeStateHash(), before);
    OB_CHECK_EQ(world.Version(), WorldVersion(1));
    OB_CHECK_EQ(world.Journal().size(), size_t(1));
    OB_CHECK_EQ(world.Stats().operationsRejected, uint64_t(3));
    OB_CHECK_EQ(world.Stats().staleOperations, uint64_t(2));
    OB_CHECK_EQ(world.Stats().duplicateOperations, uint64_t(1));
    OB_CHECK_EQ(sink.rejected, 3);
    OB_CHECK_EQ(sink.accepted, 1);

    // Gaps in the sequence are allowed (ordering is what matters, not density).
    OB_CHECK(world.ApplyOperation(Subtract(12, 100, VoxelCoord(0, 0, h), 3)).Accepted());
    OB_CHECK_EQ(world.LastSequence(), SequenceNumber(100));
}

OB_TEST(RejectedOperationsLeaveWorldUntouched)
{
    TerrainWorld world(Cfg());
    const StateHash before = world.ComputeStateHash();
    OB_CHECK_EQ(world.ApplyOperation(Subtract(1, 1, VoxelCoord(0, 0, 0), 25)).code,
                ResultCode::RadiusTooLarge);
    OB_CHECK_EQ(world.ApplyOperation(Subtract(1, 1, VoxelCoord(1000, 0, 0), 2)).code,
                ResultCode::CenterOutOfBounds);
    OB_CHECK_EQ(world.ComputeStateHash(), before);
    OB_CHECK_EQ(world.LoadedChunkCount(), size_t(0));
    OB_CHECK_EQ(world.Version(), WorldVersion(0));
}

OB_TEST(SampleMaterialFallsBackToGeneratorAndClipsToBounds)
{
    TerrainWorld world(Cfg());
    const VoxelCoord v(5, -7, -3);
    OB_CHECK(world.SampleMaterial(v) == world.Generator().MaterialAt(v));
    OB_CHECK_EQ(world.LoadedChunkCount(), size_t(0));
    OB_CHECK(world.SampleMaterial(VoxelCoord(10000, 0, 0)) == Material::Air);
    OB_CHECK(world.SampleMaterial(VoxelCoord(0, 0, -1000)) == Material::Air);
}

OB_TEST(ChunkLifecycleAndStats)
{
    CountingSink sink;
    TerrainWorld world(Cfg(), &sink);
    Chunk* c = nullptr;
    OB_CHECK_EQ(world.EnsureChunk(ChunkCoord(100, 0, 0), c), ResultCode::ChunkOutOfBounds);
    OB_REQUIRE(Succeeded(world.EnsureChunk(ChunkCoord(-4, 3, -3), c)));
    OB_REQUIRE(c != nullptr);
    OB_CHECK(c->Coord() == ChunkCoord(-4, 3, -3));
    OB_CHECK_EQ(world.Stats().chunksGenerated, uint64_t(1));
    OB_CHECK_EQ(sink.generated, 1);
    Chunk* again = nullptr;
    OB_REQUIRE(Succeeded(world.EnsureChunk(ChunkCoord(-4, 3, -3), again)));
    OB_CHECK(again == c);
    OB_CHECK_EQ(world.Stats().chunksGenerated, uint64_t(1));

    OB_CHECK_EQ(world.UnloadChunk(ChunkCoord(-4, 3, -3)), ResultCode::Ok);
    OB_CHECK_EQ(world.UnloadChunk(ChunkCoord(-4, 3, -3)), ResultCode::ChunkNotLoaded);

    const int64_t h = world.Generator().SurfaceHeight(8, 8);
    const OperationResult r = world.ApplyOperation(Subtract(1, 1, VoxelCoord(8, 8, h), 2));
    OB_REQUIRE(r.Accepted() && !r.modifiedChunks.empty());
    OB_CHECK_EQ(world.UnloadChunk(r.modifiedChunks[0]), ResultCode::ChunkModified);
    OB_CHECK_EQ(world.UnloadChunk(r.modifiedChunks[0], true), ResultCode::Ok);
    OB_CHECK(world.EstimateMemoryBytes() > 0);
}

OB_TEST(StateHashIsIndependentOfLoadedUnmodifiedChunks)
{
    TerrainWorld a(Cfg());
    TerrainWorld b(Cfg());
    const int64_t h = a.Generator().SurfaceHeight(0, 0);
    const TerrainOperation op = Subtract(1, 1, VoxelCoord(0, 0, h), 3);
    OB_REQUIRE(a.ApplyOperation(op).Accepted());
    Chunk* extra = nullptr;
    OB_REQUIRE(Succeeded(b.EnsureChunk(ChunkCoord(3, 3, 2), extra)));
    OB_REQUIRE(Succeeded(b.EnsureChunk(ChunkCoord(-4, -4, -3), extra)));
    OB_REQUIRE(b.ApplyOperation(op).Accepted());
    OB_CHECK_EQ(a.ComputeStateHash(), b.ComputeStateHash());
    OB_CHECK(a.ComputeLoadedChunkHash() != b.ComputeLoadedChunkHash());
    OB_CHECK(a.LoadedChunkCount() < b.LoadedChunkCount());
}

OB_TEST_MAIN()
