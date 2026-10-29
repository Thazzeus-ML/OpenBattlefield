// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
#pragma once

#include "OpenBattlefieldCore/ObTypes.h"

#include <vector>

namespace ob
{

struct WorldConfig;
class ByteWriter;
class ByteReader;
class Hasher;

/// Kinds of authoritative terrain modification. Values are serialized; never renumber.
enum class OperationType : uint8_t
{
    None = 0,
    /// Replace every destructible solid voxel within the sphere with Air.
    SubtractSphere = 1,
    /// Replace every Air voxel within the sphere with `material`.
    AddSphere = 2,

    Count
};

OBCORE_API const char* ToString(OperationType type);

/// Authoritative terrain modification command (see Docs/terrain-operations.md).
///
/// All geometry is integer voxel space. The affected chunk set is deliberately NOT part of the schema:
/// it is derived by the server from center/radius and never trusted from a client.
struct OBCORE_API TerrainOperation
{
    static constexpr uint16_t kSchemaVersion = 1;

    /// Globally unique id chosen by the issuer. Used for duplicate suppression. Zero is invalid.
    OperationId operationId = 0;
    /// Schema version the issuer used. Must equal kSchemaVersion.
    uint16_t schemaVersion = kSchemaVersion;
    OperationType type = OperationType::None;
    /// World this operation targets. Must equal WorldConfig::worldId.
    WorldId worldId = 0;
    /// Strictly increasing per world. Defines authoritative application order.
    SequenceNumber sequenceNumber = 0;
    /// Sphere center in world voxel coordinates.
    VoxelCoord center;
    /// Sphere radius in voxels. Must be in [1, WorldConfig::maxOperationRadius].
    uint32_t radius = 0;
    /// Material for AddSphere. Ignored (must be Air) for SubtractSphere.
    Material material = Material::Air;

    void Serialize(ByteWriter& w) const;
    ResultCode Deserialize(ByteReader& r);
    void HashInto(Hasher& h) const;

    bool operator==(const TerrainOperation& o) const;
};

/// Stateless validation against a world configuration. Does not consult world state (sequence and
/// duplicate checks live in TerrainWorld::ApplyOperation).
OBCORE_API ResultCode ValidateOperation(const TerrainOperation& op, const WorldConfig& config);

/// Computes the inclusive voxel bounding box of the operation, clipped to the world bounds.
/// Requires a validated operation. Returns ArithmeticOverflow if the box cannot be formed.
OBCORE_API ResultCode ComputeAffectedBox(const TerrainOperation& op, const WorldConfig& config,
                                         VoxelBox& outBox);

/// Computes the sorted set of chunk coordinates whose bounding boxes intersect the operation's
/// bounding box. This is the server-side derived affected chunk set.
OBCORE_API ResultCode ComputeAffectedChunks(const TerrainOperation& op, const WorldConfig& config,
                                            std::vector<ChunkCoord>& outChunks);

/// Integer sphere containment test shared by application and oracle checks.
constexpr bool InsideSphere(int64_t dx, int64_t dy, int64_t dz, uint32_t radius)
{
    const int64_t r = static_cast<int64_t>(radius);
    return dx * dx + dy * dy + dz * dz <= r * r;
}

/// Outcome of applying one operation.
struct OperationResult
{
    ResultCode code = ResultCode::Ok;
    /// Number of voxels whose material changed.
    uint64_t changedVoxels = 0;
    /// Chunks intersected by the operation's bounding box (sorted).
    std::vector<ChunkCoord> candidateChunks;
    /// Chunks in which at least one voxel changed (sorted, subset of candidateChunks).
    std::vector<ChunkCoord> modifiedChunks;

    bool Accepted() const { return code == ResultCode::Ok; }
};

} // namespace ob
