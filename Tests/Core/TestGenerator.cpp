// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
#include "ObTest.h"

#include "OpenBattlefieldCore/ObChunk.h"
#include "OpenBattlefieldCore/ObCoordinates.h"
#include "OpenBattlefieldCore/ObHash.h"
#include "OpenBattlefieldCore/ObProceduralGenerator.h"

using namespace ob;

namespace
{
WorldConfig TestConfig(uint64_t seed)
{
    WorldConfig cfg;
    cfg.seed = seed;
    cfg.chunkSize = 16;
    cfg.minChunk = ChunkCoord(-8, -8, -4);
    cfg.maxChunk = ChunkCoord(7, 7, 3);
    cfg.bedrockHeight = -40;
    return cfg;
}
} // namespace

OB_TEST(SameSeedProducesIdenticalChunks)
{
    const WorldConfig cfg = TestConfig(1234);
    ProceduralGenerator genA(cfg);
    ProceduralGenerator genB(cfg);
    for (int32_t cz = -2; cz <= 1; ++cz)
    {
        Chunk a(ChunkCoord(-3, 2, cz), 16);
        Chunk b(ChunkCoord(-3, 2, cz), 16);
        genA.GenerateChunk(a);
        genB.GenerateChunk(b);
        Hasher ha;
        Hasher hb;
        a.HashContentInto(ha);
        b.HashContentInto(hb);
        OB_CHECK_EQ(ha.Finish(), hb.Finish());
    }
}

OB_TEST(DifferentSeedsProduceDifferentTerrain)
{
    ProceduralGenerator g1(TestConfig(1));
    ProceduralGenerator g2(TestConfig(2));
    int differences = 0;
    for (int64_t x = -64; x < 64; x += 4)
        for (int64_t y = -64; y < 64; y += 4)
            differences += g1.SurfaceHeight(x, y) != g2.SurfaceHeight(x, y) ? 1 : 0;
    OB_CHECK(differences > 100);
}

OB_TEST(SurfaceHeightStaysWithinAmplitude)
{
    const WorldConfig cfg = TestConfig(99);
    ProceduralGenerator gen(cfg);
    int64_t minH = 1 << 30;
    int64_t maxH = -(1 << 30);
    for (int64_t x = -300; x <= 300; x += 3)
        for (int64_t y = -300; y <= 300; y += 7)
        {
            const int64_t h = gen.SurfaceHeight(x, y);
            minH = h < minH ? h : minH;
            maxH = h > maxH ? h : maxH;
        }
    // Sum of octave amplitudes: 24 + 12 + 6 = 42 at most in either direction.
    OB_CHECK(minH >= cfg.baseHeight - 42);
    OB_CHECK(maxH <= cfg.baseHeight + 42);
    OB_CHECK(maxH - minH > 10); // not flat
}

OB_TEST(ValueNoiseIsContinuousAcrossLatticeCells)
{
    // Adjacent samples should never jump by more than a small fraction of the amplitude.
    const WorldConfig cfg = TestConfig(7);
    ProceduralGenerator gen(cfg);
    int64_t maxStep = 0;
    for (int64_t x = -200; x < 200; ++x)
    {
        const int64_t d = gen.SurfaceHeight(x + 1, 17) - gen.SurfaceHeight(x, 17);
        maxStep = (d < 0 ? -d : d) > maxStep ? (d < 0 ? -d : d) : maxStep;
    }
    OB_CHECK(maxStep <= 3);
}

OB_TEST(MaterialAtMatchesGeneratedChunk)
{
    const WorldConfig cfg = TestConfig(555);
    ProceduralGenerator gen(cfg);
    const ChunkCoord coords[] = {ChunkCoord(0, 0, 0), ChunkCoord(-1, -1, -1), ChunkCoord(3, -5, 1),
                                 ChunkCoord(-8, 7, -3)};
    for (const ChunkCoord& cc : coords)
    {
        Chunk chunk(cc, 16);
        gen.GenerateChunk(chunk);
        for (uint32_t z = 0; z < 16; ++z)
            for (uint32_t y = 0; y < 16; ++y)
                for (uint32_t x = 0; x < 16; ++x)
                {
                    const LocalCoord l(x, y, z);
                    OB_CHECK(chunk.Get(l) == gen.MaterialAt(ToVoxel(cc, l, 16)));
                }
    }
}

OB_TEST(LayeringFollowsConfiguredDepths)
{
    const WorldConfig cfg = TestConfig(3);
    ProceduralGenerator gen(cfg);
    const int64_t h = gen.SurfaceHeight(5, 5);
    OB_CHECK(gen.MaterialForColumn(h + 1, h) == Material::Air);
    OB_CHECK(gen.MaterialForColumn(h, h) == Material::Grass);
    OB_CHECK(gen.MaterialForColumn(h - 1, h) == Material::Soil);
    OB_CHECK(gen.MaterialForColumn(h - 10, h) == Material::Soil);
    OB_CHECK(gen.MaterialForColumn(h - 11, h) == Material::Rock);
    OB_CHECK(gen.MaterialForColumn(cfg.bedrockHeight, h) == Material::Bedrock);
    OB_CHECK(gen.MaterialForColumn(cfg.bedrockHeight + 1, h) == Material::Rock);
}

OB_TEST(GenerationVersionChangesOutput)
{
    WorldConfig a = TestConfig(10);
    WorldConfig b = a;
    b.generationVersion = 2;
    ProceduralGenerator ga(a);
    ProceduralGenerator gb(b);
    int differences = 0;
    for (int64_t x = 0; x < 128; x += 2)
        differences += ga.SurfaceHeight(x, 0) != gb.SurfaceHeight(x, 0) ? 1 : 0;
    OB_CHECK(differences > 20);
}

OB_TEST(KnownSeedHashIsPinned)
{
    // Regression pin for generationVersion 1. If this changes, generationVersion MUST be bumped and
    // this expected value updated in the same commit (see Docs/determinism.md).
    const WorldConfig cfg = TestConfig(20240601);
    ProceduralGenerator gen(cfg);
    // Pick the chunk that straddles the surface at the origin so it is neither all air nor all solid.
    const int32_t cz = static_cast<int32_t>(FloorDiv(gen.SurfaceHeight(8, 8), 16));
    Chunk chunk(ChunkCoord(0, 0, cz), 16);
    gen.GenerateChunk(chunk);
    Hasher h;
    chunk.HashContentInto(h);
    const StateHash hash = h.Finish();
    std::printf("  pinned chunk hash = 0x%016llx\n", static_cast<unsigned long long>(hash));
    OB_CHECK(chunk.SolidCount() > 0 && chunk.SolidCount() < chunk.VoxelCount());
    OB_CHECK_EQ(hash, StateHash(0x2a131f30b9159342ull));
}

OB_TEST_MAIN()
