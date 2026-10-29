// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
#include "OpenBattlefieldCore/ObWorldConfig.h"

#include "OpenBattlefieldCore/ObCoordinates.h"
#include "OpenBattlefieldCore/ObHash.h"
#include "OpenBattlefieldCore/ObSerialization.h"

namespace ob
{

namespace
{
constexpr uint32_t kMaxOperationRadiusCap = 4096;
constexpr uint32_t kMaxNoiseOctaves = 8;
constexpr uint32_t kMaxNoiseScaleLog2 = 20;
} // namespace

ResultCode WorldConfig::Validate() const
{
    if (worldId == 0)
        return ResultCode::InvalidConfig;
    if (!IsPowerOfTwo(chunkSize) || chunkSize < kMinChunkSize || chunkSize > kMaxChunkSize)
        return ResultCode::InvalidChunkSize;
    if (voxelSizeMm == 0)
        return ResultCode::InvalidConfig;
    if (minChunk.x > maxChunk.x || minChunk.y > maxChunk.y || minChunk.z > maxChunk.z)
        return ResultCode::InvalidWorldBounds;
    const int32_t lim = kMaxAbsChunkCoord;
    if (minChunk.x < -lim || minChunk.y < -lim || minChunk.z < -lim || maxChunk.x > lim || maxChunk.y > lim ||
        maxChunk.z > lim)
        return ResultCode::InvalidWorldBounds;
    if (maxOperationRadius == 0 || maxOperationRadius > kMaxOperationRadiusCap)
        return ResultCode::InvalidConfig;
    if (heightAmplitude < 0)
        return ResultCode::InvalidConfig;
    if (noiseOctaves == 0 || noiseOctaves > kMaxNoiseOctaves || noiseScaleLog2 > kMaxNoiseScaleLog2)
        return ResultCode::InvalidConfig;
    return ResultCode::Ok;
}

bool WorldConfig::ContainsChunk(const ChunkCoord& c) const
{
    return c.x >= minChunk.x && c.x <= maxChunk.x && c.y >= minChunk.y && c.y <= maxChunk.y &&
           c.z >= minChunk.z && c.z <= maxChunk.z;
}

VoxelBox WorldConfig::VoxelBounds() const
{
    VoxelBox b;
    b.min = ChunkOrigin(minChunk, chunkSize);
    const VoxelCoord maxOrigin = ChunkOrigin(maxChunk, chunkSize);
    const int64_t s = static_cast<int64_t>(chunkSize);
    b.max = VoxelCoord(maxOrigin.x + s - 1, maxOrigin.y + s - 1, maxOrigin.z + s - 1);
    return b;
}

bool WorldConfig::ContainsVoxel(const VoxelCoord& v) const
{
    const VoxelBox b = VoxelBounds();
    return v.x >= b.min.x && v.x <= b.max.x && v.y >= b.min.y && v.y <= b.max.y && v.z >= b.min.z &&
           v.z <= b.max.z;
}

void WorldConfig::Serialize(ByteWriter& w) const
{
    w.U16(kSchemaVersion);
    w.U64(worldId);
    w.U64(seed);
    w.U32(generationVersion);
    w.U32(chunkSize);
    w.U32(voxelSizeMm);
    w.Chunk(minChunk);
    w.Chunk(maxChunk);
    w.U32(maxOperationRadius);
    w.I64(baseHeight);
    w.I32(heightAmplitude);
    w.U32(noiseScaleLog2);
    w.U32(noiseOctaves);
    w.U32(topsoilDepth);
    w.U32(soilDepth);
    w.I64(bedrockHeight);
}

ResultCode WorldConfig::Deserialize(ByteReader& r)
{
    uint16_t schema = 0;
    if (!r.U16(schema))
        return ResultCode::SerializationTruncated;
    if (schema != kSchemaVersion)
        return ResultCode::SerializationVersionMismatch;
    const bool ok = r.U64(worldId) && r.U64(seed) && r.U32(generationVersion) && r.U32(chunkSize) &&
                    r.U32(voxelSizeMm) && r.Chunk(minChunk) && r.Chunk(maxChunk) &&
                    r.U32(maxOperationRadius) && r.I64(baseHeight) && r.I32(heightAmplitude) &&
                    r.U32(noiseScaleLog2) && r.U32(noiseOctaves) && r.U32(topsoilDepth) && r.U32(soilDepth) &&
                    r.I64(bedrockHeight);
    if (!ok)
        return ResultCode::SerializationTruncated;
    const ResultCode valid = Validate();
    if (Failed(valid))
        return ResultCode::SerializationCorrupt;
    return ResultCode::Ok;
}

void WorldConfig::HashInto(Hasher& h) const
{
    ByteWriter w;
    Serialize(w);
    h.Bytes(w.Buffer().data(), w.Size());
}

bool WorldConfig::operator==(const WorldConfig& o) const
{
    return worldId == o.worldId && seed == o.seed && generationVersion == o.generationVersion &&
           chunkSize == o.chunkSize && voxelSizeMm == o.voxelSizeMm && minChunk == o.minChunk &&
           maxChunk == o.maxChunk && maxOperationRadius == o.maxOperationRadius &&
           baseHeight == o.baseHeight && heightAmplitude == o.heightAmplitude &&
           noiseScaleLog2 == o.noiseScaleLog2 && noiseOctaves == o.noiseOctaves &&
           topsoilDepth == o.topsoilDepth && soilDepth == o.soilDepth && bedrockHeight == o.bedrockHeight;
}

} // namespace ob
