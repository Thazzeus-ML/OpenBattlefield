// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
#include "ObTest.h"

#include "OpenBattlefieldCore/ObCoordinates.h"

#include <limits>

using namespace ob;

OB_TEST(FloorDivRoundsTowardNegativeInfinity)
{
    OB_CHECK_EQ(FloorDiv(0, 32), 0);
    OB_CHECK_EQ(FloorDiv(31, 32), 0);
    OB_CHECK_EQ(FloorDiv(32, 32), 1);
    OB_CHECK_EQ(FloorDiv(-1, 32), -1);
    OB_CHECK_EQ(FloorDiv(-32, 32), -1);
    OB_CHECK_EQ(FloorDiv(-33, 32), -2);
    OB_CHECK_EQ(FloorMod(-1, 32), 31);
    OB_CHECK_EQ(FloorMod(-32, 32), 0);
    OB_CHECK_EQ(FloorMod(-33, 32), 31);
    OB_CHECK_EQ(FloorMod(33, 32), 1);
}

OB_TEST(ChunkMappingHandlesNegativeCoordinates)
{
    ChunkCoord c;
    OB_REQUIRE(TryChunkOf(VoxelCoord(-1, -1, -1), 32, c));
    OB_CHECK(c == ChunkCoord(-1, -1, -1));
    LocalCoord l = LocalOf(VoxelCoord(-1, -1, -1), 32);
    OB_CHECK(l == LocalCoord(31, 31, 31));

    OB_REQUIRE(TryChunkOf(VoxelCoord(-32, 0, 63), 32, c));
    OB_CHECK(c == ChunkCoord(-1, 0, 1));
    l = LocalOf(VoxelCoord(-32, 0, 63), 32);
    OB_CHECK(l == LocalCoord(0, 0, 31));

    // Round trip across a sweep of values including negatives.
    for (int64_t v = -200; v <= 200; ++v)
    {
        const VoxelCoord voxel(v, -v, v * 3);
        OB_REQUIRE(TryChunkOf(voxel, 16, c));
        const LocalCoord local = LocalOf(voxel, 16);
        OB_CHECK(local.x < 16 && local.y < 16 && local.z < 16);
        OB_CHECK(ToVoxel(c, local, 16) == voxel);
    }
}

OB_TEST(ChunkMappingRejectsOutOfRange)
{
    ChunkCoord c;
    const int64_t huge = static_cast<int64_t>(kMaxAbsChunkCoord + 1) * 32;
    OB_CHECK(!TryChunkOf(VoxelCoord(huge, 0, 0), 32, c));
    OB_CHECK(!TryChunkOf(VoxelCoord(0, -huge - 32, 0), 32, c));
    OB_CHECK(TryChunkOf(VoxelCoord(huge - 1, 0, 0), 32, c));
    OB_CHECK(!TryChunkOf(VoxelCoord(std::numeric_limits<int64_t>::max(), 0, 0), 32, c));
    OB_CHECK(!TryChunkOf(VoxelCoord(std::numeric_limits<int64_t>::min(), 0, 0), 32, c));
}

OB_TEST(ChunkKeyPackingIsStableAndInvertible)
{
    const ChunkCoord samples[] = {ChunkCoord(0, 0, 0), ChunkCoord(-1, -1, -1),
                                  ChunkCoord(kMaxAbsChunkCoord, -kMaxAbsChunkCoord, 12345),
                                  ChunkCoord(-kMaxAbsChunkCoord, kMaxAbsChunkCoord, -54321)};
    for (const ChunkCoord& s : samples)
    {
        uint64_t key = 0;
        OB_REQUIRE(TryPackChunkKey(s, key));
        OB_CHECK(UnpackChunkKey(key) == s);
    }
    // Known value pin: the key for the origin must never change (persistent identifier).
    uint64_t originKey = 0;
    OB_REQUIRE(TryPackChunkKey(ChunkCoord(0, 0, 0), originKey));
    OB_CHECK_EQ(originKey, (1ull << 20) | (1ull << 41) | (1ull << 62));
    uint64_t key = 0;
    OB_CHECK(!TryPackChunkKey(ChunkCoord(kMaxAbsChunkCoord + 1, 0, 0), key));
    OB_CHECK(!TryPackChunkKey(ChunkCoord(0, 0, -kMaxAbsChunkCoord - 1), key));
}

OB_TEST(CheckedArithmeticDetectsOverflow)
{
    int64_t out = 0;
    const int64_t maxv = std::numeric_limits<int64_t>::max();
    const int64_t minv = std::numeric_limits<int64_t>::min();
    OB_CHECK(CheckedAdd(1, 2, out) && out == 3);
    OB_CHECK(!CheckedAdd(maxv, 1, out));
    OB_CHECK(!CheckedAdd(minv, -1, out));
    OB_CHECK(CheckedSub(-5, 5, out) && out == -10);
    OB_CHECK(!CheckedSub(minv, 1, out));
    OB_CHECK(!CheckedSub(maxv, -1, out));
    OB_CHECK(CheckedMul(1 << 20, 1 << 20, out) && out == (1ll << 40));
    OB_CHECK(!CheckedMul(maxv, 2, out));
    OB_CHECK(!CheckedMul(minv, -1, out));
}

OB_TEST(LocalIndexIsXFastest)
{
    OB_CHECK_EQ(LocalIndex(LocalCoord(0, 0, 0), 32), size_t(0));
    OB_CHECK_EQ(LocalIndex(LocalCoord(1, 0, 0), 32), size_t(1));
    OB_CHECK_EQ(LocalIndex(LocalCoord(0, 1, 0), 32), size_t(32));
    OB_CHECK_EQ(LocalIndex(LocalCoord(0, 0, 1), 32), size_t(1024));
    OB_CHECK_EQ(LocalIndex(LocalCoord(31, 31, 31), 32), size_t(32767));
}

OB_TEST_MAIN()
