// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
#include "OpenBattlefieldCore/ObTerrainOperation.h"

#include "OpenBattlefieldCore/ObCoordinates.h"
#include "OpenBattlefieldCore/ObHash.h"
#include "OpenBattlefieldCore/ObSerialization.h"
#include "OpenBattlefieldCore/ObWorldConfig.h"

namespace ob
{

const char* ToString(OperationType type)
{
    switch (type)
    {
    case OperationType::None:
        return "None";
    case OperationType::SubtractSphere:
        return "SubtractSphere";
    case OperationType::AddSphere:
        return "AddSphere";
    case OperationType::Count:
        break;
    }
    return "UnknownOperationType";
}

void TerrainOperation::Serialize(ByteWriter& w) const
{
    w.U64(operationId);
    w.U16(schemaVersion);
    w.U8(static_cast<uint8_t>(type));
    w.U64(worldId);
    w.U64(sequenceNumber);
    w.Voxel(center);
    w.U32(radius);
    w.U8(static_cast<uint8_t>(material));
}

ResultCode TerrainOperation::Deserialize(ByteReader& r)
{
    uint8_t rawType = 0;
    uint8_t rawMaterial = 0;
    const bool ok = r.U64(operationId) && r.U16(schemaVersion) && r.U8(rawType) && r.U64(worldId) &&
                    r.U64(sequenceNumber) && r.Voxel(center) && r.U32(radius) && r.U8(rawMaterial);
    if (!ok)
        return ResultCode::SerializationTruncated;
    if (rawType >= static_cast<uint8_t>(OperationType::Count) || !IsValidMaterial(rawMaterial))
        return ResultCode::SerializationCorrupt;
    type = static_cast<OperationType>(rawType);
    material = static_cast<Material>(rawMaterial);
    return ResultCode::Ok;
}

void TerrainOperation::HashInto(Hasher& h) const
{
    ByteWriter w;
    Serialize(w);
    h.Bytes(w.Buffer().data(), w.Size());
}

bool TerrainOperation::operator==(const TerrainOperation& o) const
{
    return operationId == o.operationId && schemaVersion == o.schemaVersion && type == o.type &&
           worldId == o.worldId && sequenceNumber == o.sequenceNumber && center == o.center &&
           radius == o.radius && material == o.material;
}

ResultCode ValidateOperation(const TerrainOperation& op, const WorldConfig& config)
{
    if (op.schemaVersion != TerrainOperation::kSchemaVersion)
        return ResultCode::OperationSchemaVersionMismatch;
    if (op.operationId == 0)
        return ResultCode::InvalidOperationId;
    if (op.type == OperationType::None || op.type >= OperationType::Count)
        return ResultCode::InvalidOperationType;
    if (op.worldId != config.worldId)
        return ResultCode::WorldIdMismatch;
    if (op.radius == 0)
        return ResultCode::InvalidRadius;
    if (op.radius > config.maxOperationRadius)
        return ResultCode::RadiusTooLarge;
    if (!IsValidMaterial(static_cast<uint8_t>(op.material)))
        return ResultCode::InvalidMaterial;
    if (op.type == OperationType::SubtractSphere && op.material != Material::Air)
        return ResultCode::InvalidMaterial;
    if (op.type == OperationType::AddSphere && (!IsSolid(op.material) || IsIndestructible(op.material)))
        return ResultCode::InvalidMaterial;
    if (!config.ContainsVoxel(op.center))
        return ResultCode::CenterOutOfBounds;

    // Overflow is impossible given bounded chunk coordinates and radius, but we never rely on that.
    const int64_t r = static_cast<int64_t>(op.radius);
    int64_t tmp = 0;
    if (!CheckedAdd(op.center.x, r, tmp) || !CheckedSub(op.center.x, r, tmp) ||
        !CheckedAdd(op.center.y, r, tmp) || !CheckedSub(op.center.y, r, tmp) ||
        !CheckedAdd(op.center.z, r, tmp) || !CheckedSub(op.center.z, r, tmp))
        return ResultCode::ArithmeticOverflow;
    int64_t r2 = 0;
    if (!CheckedMul(r, r, r2))
        return ResultCode::ArithmeticOverflow;
    return ResultCode::Ok;
}

ResultCode ComputeAffectedBox(const TerrainOperation& op, const WorldConfig& config, VoxelBox& outBox)
{
    const int64_t r = static_cast<int64_t>(op.radius);
    VoxelBox box;
    if (!CheckedSub(op.center.x, r, box.min.x) || !CheckedSub(op.center.y, r, box.min.y) ||
        !CheckedSub(op.center.z, r, box.min.z) || !CheckedAdd(op.center.x, r, box.max.x) ||
        !CheckedAdd(op.center.y, r, box.max.y) || !CheckedAdd(op.center.z, r, box.max.z))
        return ResultCode::ArithmeticOverflow;

    const VoxelBox bounds = config.VoxelBounds();
    if (box.min.x < bounds.min.x)
        box.min.x = bounds.min.x;
    if (box.min.y < bounds.min.y)
        box.min.y = bounds.min.y;
    if (box.min.z < bounds.min.z)
        box.min.z = bounds.min.z;
    if (box.max.x > bounds.max.x)
        box.max.x = bounds.max.x;
    if (box.max.y > bounds.max.y)
        box.max.y = bounds.max.y;
    if (box.max.z > bounds.max.z)
        box.max.z = bounds.max.z;
    if (!box.IsValid())
        return ResultCode::CenterOutOfBounds;
    outBox = box;
    return ResultCode::Ok;
}

ResultCode ComputeAffectedChunks(const TerrainOperation& op, const WorldConfig& config,
                                 std::vector<ChunkCoord>& outChunks)
{
    outChunks.clear();
    VoxelBox box;
    const ResultCode code = ComputeAffectedBox(op, config, box);
    if (Failed(code))
        return code;
    ChunkCoord lo;
    ChunkCoord hi;
    if (!TryChunkOf(box.min, config.chunkSize, lo) || !TryChunkOf(box.max, config.chunkSize, hi))
        return ResultCode::ChunkOutOfBounds;
    // z-major, then y, then x: this matches ChunkCoord::operator< so the output is sorted.
    for (int32_t z = lo.z; z <= hi.z; ++z)
        for (int32_t y = lo.y; y <= hi.y; ++y)
            for (int32_t x = lo.x; x <= hi.x; ++x)
                outChunks.push_back(ChunkCoord(x, y, z));
    return ResultCode::Ok;
}

} // namespace ob
