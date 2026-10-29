// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
#pragma once

#include "OpenBattlefieldCore/ObTypes.h"

namespace ob
{

class ByteWriter;
class ByteReader;
class Hasher;

/// Immutable description of a world's procedural foundation and addressing rules.
///
/// Two worlds with equal WorldConfig and an equal ordered operation journal reconstruct equal
/// authoritative terrain state (see Docs/determinism.md). Every field participates in the state hash.
struct OBCORE_API WorldConfig
{
    /// Serialization schema version of this struct. Bump when fields change; see Deserialize.
    static constexpr uint16_t kSchemaVersion = 1;

    /// Identifies the world instance. Operations carry the same id and are rejected otherwise.
    WorldId worldId = 1;

    /// Procedural generation seed.
    uint64_t seed = 0;

    /// Version of the procedural generation rules. Changing the generator algorithm bumps this value
    /// so that saved journals are never replayed on top of a different foundation.
    uint32_t generationVersion = 1;

    /// Voxels per chunk edge. Power of two in [kMinChunkSize, kMaxChunkSize].
    uint32_t chunkSize = 32;

    /// Physical edge length of one voxel in millimetres. Used only by adapters; the core is unitless.
    uint32_t voxelSizeMm = 250;

    /// Inclusive chunk-space bounds of the addressable world.
    ChunkCoord minChunk = ChunkCoord(-64, -64, -4);
    ChunkCoord maxChunk = ChunkCoord(63, 63, 3);

    /// Largest radius (in voxels) any single terrain operation may use.
    uint32_t maxOperationRadius = 64;

    // --- Generation parameters (generationVersion 1) --------------------------------------------
    /// Mean surface height in voxels.
    int64_t baseHeight = 0;
    /// Peak-to-mean amplitude of the surface height in voxels.
    int32_t heightAmplitude = 24;
    /// log2 of the largest noise lattice spacing in voxels.
    uint32_t noiseScaleLog2 = 6;
    /// Number of value-noise octaves (1..8).
    uint32_t noiseOctaves = 3;
    /// Depth of the topsoil (Grass) layer in voxels.
    uint32_t topsoilDepth = 1;
    /// Depth of the Soil layer below the topsoil in voxels.
    uint32_t soilDepth = 10;
    /// Everything at or below this height is indestructible Bedrock.
    int64_t bedrockHeight = -100;

    ResultCode Validate() const;

    /// Returns the total number of voxels per chunk.
    size_t VoxelsPerChunk() const
    {
        return static_cast<size_t>(chunkSize) * static_cast<size_t>(chunkSize) *
               static_cast<size_t>(chunkSize);
    }

    /// Returns true when the chunk coordinate lies inside [minChunk, maxChunk].
    bool ContainsChunk(const ChunkCoord& c) const;

    /// Returns the inclusive voxel bounds of the world.
    VoxelBox VoxelBounds() const;

    /// Returns true when the voxel lies inside the world bounds.
    bool ContainsVoxel(const VoxelCoord& v) const;

    void Serialize(ByteWriter& w) const;
    ResultCode Deserialize(ByteReader& r);
    void HashInto(Hasher& h) const;

    bool operator==(const WorldConfig& o) const;
    bool operator!=(const WorldConfig& o) const { return !(*this == o); }
};

} // namespace ob
