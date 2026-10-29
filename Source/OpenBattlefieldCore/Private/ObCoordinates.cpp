// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
#include "OpenBattlefieldCore/ObCoordinates.h"

#include <limits>

namespace ob
{

bool CheckedAdd(int64_t a, int64_t b, int64_t& out)
{
#if defined(__GNUC__) || defined(__clang__)
    int64_t r = 0;
    if (__builtin_add_overflow(a, b, &r))
        return false;
    out = r;
    return true;
#else
    if ((b > 0 && a > std::numeric_limits<int64_t>::max() - b) ||
        (b < 0 && a < std::numeric_limits<int64_t>::min() - b))
        return false;
    out = a + b;
    return true;
#endif
}

bool CheckedSub(int64_t a, int64_t b, int64_t& out)
{
#if defined(__GNUC__) || defined(__clang__)
    int64_t r = 0;
    if (__builtin_sub_overflow(a, b, &r))
        return false;
    out = r;
    return true;
#else
    if ((b < 0 && a > std::numeric_limits<int64_t>::max() + b) ||
        (b > 0 && a < std::numeric_limits<int64_t>::min() + b))
        return false;
    out = a - b;
    return true;
#endif
}

bool CheckedMul(int64_t a, int64_t b, int64_t& out)
{
#if defined(__GNUC__) || defined(__clang__)
    int64_t r = 0;
    if (__builtin_mul_overflow(a, b, &r))
        return false;
    out = r;
    return true;
#else
    if (a == 0 || b == 0)
    {
        out = 0;
        return true;
    }
    const int64_t maxv = std::numeric_limits<int64_t>::max();
    const int64_t minv = std::numeric_limits<int64_t>::min();
    if (a == -1 && b == minv)
        return false;
    if (b == -1 && a == minv)
        return false;
    if (a > 0)
    {
        if (b > 0 ? a > maxv / b : b < minv / a)
            return false;
    }
    else
    {
        if (b > 0 ? a < minv / b : (a != 0 && b < maxv / a))
            return false;
    }
    out = a * b;
    return true;
#endif
}

bool TryChunkOf(const VoxelCoord& voxel, uint32_t chunkSize, ChunkCoord& outChunk)
{
    const int64_t size = static_cast<int64_t>(chunkSize);
    const int64_t cx = FloorDiv(voxel.x, size);
    const int64_t cy = FloorDiv(voxel.y, size);
    const int64_t cz = FloorDiv(voxel.z, size);
    const int64_t lim = static_cast<int64_t>(kMaxAbsChunkCoord);
    if (cx < -lim || cx > lim || cy < -lim || cy > lim || cz < -lim || cz > lim)
        return false;
    outChunk = ChunkCoord(static_cast<int32_t>(cx), static_cast<int32_t>(cy), static_cast<int32_t>(cz));
    return true;
}

LocalCoord LocalOf(const VoxelCoord& voxel, uint32_t chunkSize)
{
    const int64_t size = static_cast<int64_t>(chunkSize);
    return LocalCoord(static_cast<uint32_t>(FloorMod(voxel.x, size)),
                      static_cast<uint32_t>(FloorMod(voxel.y, size)),
                      static_cast<uint32_t>(FloorMod(voxel.z, size)));
}

VoxelCoord ChunkOrigin(const ChunkCoord& chunk, uint32_t chunkSize)
{
    const int64_t size = static_cast<int64_t>(chunkSize);
    return VoxelCoord(static_cast<int64_t>(chunk.x) * size, static_cast<int64_t>(chunk.y) * size,
                      static_cast<int64_t>(chunk.z) * size);
}

VoxelCoord ToVoxel(const ChunkCoord& chunk, const LocalCoord& local, uint32_t chunkSize)
{
    const VoxelCoord origin = ChunkOrigin(chunk, chunkSize);
    return VoxelCoord(origin.x + static_cast<int64_t>(local.x), origin.y + static_cast<int64_t>(local.y),
                      origin.z + static_cast<int64_t>(local.z));
}

namespace
{
constexpr uint64_t kAxisBits = 21;
constexpr uint64_t kAxisMask = (1ull << kAxisBits) - 1;
constexpr int64_t kAxisBias = 1ll << (kAxisBits - 1); // maps [-2^20, 2^20) onto [0, 2^21)
} // namespace

bool TryPackChunkKey(const ChunkCoord& chunk, uint64_t& outKey)
{
    const int32_t comps[3] = {chunk.x, chunk.y, chunk.z};
    uint64_t key = 0;
    for (int i = 0; i < 3; ++i)
    {
        if (comps[i] < -kMaxAbsChunkCoord || comps[i] > kMaxAbsChunkCoord)
            return false;
        const uint64_t biased = static_cast<uint64_t>(static_cast<int64_t>(comps[i]) + kAxisBias) & kAxisMask;
        key |= biased << (kAxisBits * static_cast<uint64_t>(i));
    }
    outKey = key;
    return true;
}

ChunkCoord UnpackChunkKey(uint64_t key)
{
    int32_t comps[3] = {0, 0, 0};
    for (int i = 0; i < 3; ++i)
    {
        const uint64_t biased = (key >> (kAxisBits * static_cast<uint64_t>(i))) & kAxisMask;
        comps[i] = static_cast<int32_t>(static_cast<int64_t>(biased) - kAxisBias);
    }
    return ChunkCoord(comps[0], comps[1], comps[2]);
}

} // namespace ob
