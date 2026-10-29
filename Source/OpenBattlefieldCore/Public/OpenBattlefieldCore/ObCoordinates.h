// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
#pragma once

#include "OpenBattlefieldCore/ObTypes.h"

namespace ob
{

// ---------------------------------------------------------------------------------------------------
// Checked integer arithmetic. The core never relies on signed overflow.
// ---------------------------------------------------------------------------------------------------

/// Adds two signed 64-bit values. Returns false (and leaves out untouched) on overflow.
OBCORE_API bool CheckedAdd(int64_t a, int64_t b, int64_t& out);
/// Subtracts b from a. Returns false (and leaves out untouched) on overflow.
OBCORE_API bool CheckedSub(int64_t a, int64_t b, int64_t& out);
/// Multiplies two signed 64-bit values. Returns false (and leaves out untouched) on overflow.
OBCORE_API bool CheckedMul(int64_t a, int64_t b, int64_t& out);

/// Floor division for b > 0: result is the largest integer q such that q * b <= a.
/// Unlike C++ '/', this rounds toward negative infinity, which is what chunk mapping needs.
constexpr int64_t FloorDiv(int64_t a, int64_t b)
{
    const int64_t q = a / b;
    const int64_t r = a % b;
    return (r != 0 && ((r < 0) != (b < 0))) ? q - 1 : q;
}

/// Floor modulo for b > 0: always in [0, b).
constexpr int64_t FloorMod(int64_t a, int64_t b)
{
    const int64_t r = a % b;
    return (r != 0 && ((r < 0) != (b < 0))) ? r + b : r;
}

/// Returns true if v is a power of two (and non-zero).
constexpr bool IsPowerOfTwo(uint32_t v)
{
    return v != 0 && (v & (v - 1)) == 0;
}

/// Returns log2(v) for a power-of-two v.
constexpr uint32_t Log2PowerOfTwo(uint32_t v)
{
    uint32_t r = 0;
    while (v > 1)
    {
        v >>= 1;
        ++r;
    }
    return r;
}

// ---------------------------------------------------------------------------------------------------
// Chunk <-> voxel mapping.
// ---------------------------------------------------------------------------------------------------

/// Maximum absolute chunk coordinate on any axis. Chosen so that chunk coordinates pack into a 64-bit
/// key (21 bits per axis) and voxel coordinates (chunk * chunkSize) stay far from int64 limits.
constexpr int32_t kMaxAbsChunkCoord = (1 << 20) - 1;

/// Minimum and maximum chunk edge length in voxels. Dense chunks of 128^3 are already 2 MiB.
constexpr uint32_t kMinChunkSize = 8;
constexpr uint32_t kMaxChunkSize = 128;

/// Computes the chunk containing a voxel. Returns false if the result does not fit the chunk range.
OBCORE_API bool TryChunkOf(const VoxelCoord& voxel, uint32_t chunkSize, ChunkCoord& outChunk);

/// Computes the position of a voxel inside its chunk.
OBCORE_API LocalCoord LocalOf(const VoxelCoord& voxel, uint32_t chunkSize);

/// Computes the world voxel coordinate of a chunk's minimum corner.
OBCORE_API VoxelCoord ChunkOrigin(const ChunkCoord& chunk, uint32_t chunkSize);

/// Composes a world voxel coordinate from a chunk coordinate and a local coordinate.
OBCORE_API VoxelCoord ToVoxel(const ChunkCoord& chunk, const LocalCoord& local, uint32_t chunkSize);

/// Packs a chunk coordinate into a stable 64-bit key (21 bits per axis plus sign handling).
/// Returns false if any component exceeds kMaxAbsChunkCoord. The key is stable across platforms and
/// suitable for persistent identification, hashing and network addressing.
OBCORE_API bool TryPackChunkKey(const ChunkCoord& chunk, uint64_t& outKey);

/// Inverse of TryPackChunkKey. Only the low 63 bits are interpreted.
OBCORE_API ChunkCoord UnpackChunkKey(uint64_t key);

/// Index of a local coordinate inside a dense chunk array, X fastest then Y then Z.
constexpr size_t LocalIndex(const LocalCoord& local, uint32_t chunkSize)
{
    return static_cast<size_t>(local.x) +
           static_cast<size_t>(chunkSize) *
               (static_cast<size_t>(local.y) + static_cast<size_t>(chunkSize) * static_cast<size_t>(local.z));
}

} // namespace ob
