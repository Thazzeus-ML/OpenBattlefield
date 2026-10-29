// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
#pragma once

#include "OpenBattlefieldCore/ObTypes.h"

namespace ob
{

/// SplitMix64 finalizer. Used as the deterministic noise and id-mixing primitive. Pure integer math.
constexpr uint64_t Mix64(uint64_t x)
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

/// Combines a running hash with an additional 64-bit value (order dependent).
constexpr uint64_t HashCombine(uint64_t seed, uint64_t value)
{
    return Mix64(seed ^ Mix64(value + 0x632BE59BD9B4E019ull));
}

/// Streaming FNV-1a (64-bit) over a canonical little-endian byte stream.
///
/// State hashes are always computed by feeding canonical serialization into this hasher, never by
/// hashing raw struct memory (which contains padding and is endian dependent).
class OBCORE_API Hasher
{
public:
    static constexpr uint64_t kOffsetBasis = 0xCBF29CE484222325ull;
    static constexpr uint64_t kPrime = 0x100000001B3ull;

    Hasher() = default;

    void Byte(uint8_t b)
    {
        state_ ^= b;
        state_ *= kPrime;
    }
    void Bytes(const void* data, size_t size);
    void U8(uint8_t v) { Byte(v); }
    void U16(uint16_t v);
    void U32(uint32_t v);
    void U64(uint64_t v);
    void I32(int32_t v) { U32(static_cast<uint32_t>(v)); }
    void I64(int64_t v) { U64(static_cast<uint64_t>(v)); }

    void Voxel(const VoxelCoord& c)
    {
        I64(c.x);
        I64(c.y);
        I64(c.z);
    }
    void Chunk(const ChunkCoord& c)
    {
        I32(c.x);
        I32(c.y);
        I32(c.z);
    }

    StateHash Finish() const { return state_; }

private:
    uint64_t state_ = kOffsetBasis;
};

/// Hashes a byte range in one call.
OBCORE_API StateHash HashBytes(const void* data, size_t size);

} // namespace ob
