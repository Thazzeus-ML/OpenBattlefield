// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
#pragma once

#include "OpenBattlefieldCore/ObApi.h"

#include <cstddef>
#include <cstdint>

// Fundamental, engine-agnostic value types used throughout the simulation core.
//
// Coordinate convention (see Docs/coordinates.md):
//   * World space is a regular voxel lattice. A voxel is addressed by an integer VoxelCoord.
//   * X and Y are horizontal, Z is up. This matches Unreal's Z-up convention so the adapter only scales.
//   * Negative coordinates are first-class; chunk mapping uses floor division, never truncation.
//   * Authoritative terrain operations are expressed exclusively in integer voxel units so that
//     application is bitwise deterministic across platforms and compilers.
namespace ob
{

using WorldId = uint64_t;
using OperationId = uint64_t;
using SequenceNumber = uint64_t;
using ChunkVersion = uint64_t;
using WorldVersion = uint64_t;
using StateHash = uint64_t;

/// Integer position of a single voxel in world space.
struct VoxelCoord
{
    int64_t x = 0;
    int64_t y = 0;
    int64_t z = 0;

    constexpr VoxelCoord() = default;
    constexpr VoxelCoord(int64_t inX, int64_t inY, int64_t inZ) : x(inX), y(inY), z(inZ) {}

    constexpr bool operator==(const VoxelCoord& o) const { return x == o.x && y == o.y && z == o.z; }
    constexpr bool operator!=(const VoxelCoord& o) const { return !(*this == o); }
};

/// Integer position of a chunk in chunk space. Strict weak ordering is defined so chunks can be stored
/// and iterated in a canonical (z, y, x) order independent of load order.
struct ChunkCoord
{
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;

    constexpr ChunkCoord() = default;
    constexpr ChunkCoord(int32_t inX, int32_t inY, int32_t inZ) : x(inX), y(inY), z(inZ) {}

    constexpr bool operator==(const ChunkCoord& o) const { return x == o.x && y == o.y && z == o.z; }
    constexpr bool operator!=(const ChunkCoord& o) const { return !(*this == o); }
    constexpr bool operator<(const ChunkCoord& o) const
    {
        if (z != o.z)
            return z < o.z;
        if (y != o.y)
            return y < o.y;
        return x < o.x;
    }
};

/// Position of a voxel inside a chunk. Always in [0, chunkSize) on each axis.
struct LocalCoord
{
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t z = 0;

    constexpr LocalCoord() = default;
    constexpr LocalCoord(uint32_t inX, uint32_t inY, uint32_t inZ) : x(inX), y(inY), z(inZ) {}

    constexpr bool operator==(const LocalCoord& o) const { return x == o.x && y == o.y && z == o.z; }
};

/// Inclusive axis-aligned box of voxels.
struct VoxelBox
{
    VoxelCoord min;
    VoxelCoord max;

    constexpr bool IsValid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }
};

/// Terrain material identifiers. Values are part of the serialized format and must never be renumbered.
/// New materials are appended before Count.
enum class Material : uint8_t
{
    Air = 0,
    Soil = 1,
    Grass = 2,
    Rock = 3,
    /// Bedrock is never removed by terrain operations. It is the deterministic lower bound of the world.
    Bedrock = 4,

    Count
};

constexpr bool IsSolid(Material m)
{
    return m != Material::Air;
}
constexpr bool IsValidMaterial(uint8_t raw)
{
    return raw < static_cast<uint8_t>(Material::Count);
}
/// Materials that terrain operations are not permitted to modify.
constexpr bool IsIndestructible(Material m)
{
    return m == Material::Bedrock;
}

/// Result codes for every fallible operation in the core. The core never throws.
enum class ResultCode : uint16_t
{
    Ok = 0,

    // Configuration
    InvalidConfig = 100,
    InvalidChunkSize = 101,
    InvalidWorldBounds = 102,

    // Operation validation
    InvalidOperationType = 200,
    OperationSchemaVersionMismatch = 201,
    WorldIdMismatch = 202,
    InvalidRadius = 203,
    RadiusTooLarge = 204,
    CenterOutOfBounds = 205,
    ArithmeticOverflow = 206,
    InvalidMaterial = 207,
    StaleSequence = 208,
    DuplicateOperation = 209,
    InvalidOperationId = 210,

    // Chunk / world access
    ChunkOutOfBounds = 300,
    ChunkNotLoaded = 301,
    ChunkModified = 302,

    // Serialization
    SerializationTruncated = 400,
    SerializationCorrupt = 401,
    SerializationVersionMismatch = 402,
    SerializationBadMagic = 403,
    SerializationLimitExceeded = 404,

    // World / replay
    WorldNotInitialized = 500,
    ReplayDiverged = 501,
};

OBCORE_API const char* ToString(ResultCode code);
OBCORE_API const char* ToString(Material material);

constexpr bool Succeeded(ResultCode code)
{
    return code == ResultCode::Ok;
}
constexpr bool Failed(ResultCode code)
{
    return code != ResultCode::Ok;
}

} // namespace ob
