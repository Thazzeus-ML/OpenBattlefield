// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
#include "ObTest.h"

#include "OpenBattlefieldCore/ObCoordinates.h"
#include "OpenBattlefieldCore/ObTerrainOperation.h"
#include "OpenBattlefieldCore/ObWorldConfig.h"

#include <limits>

using namespace ob;

namespace
{
WorldConfig Cfg()
{
    WorldConfig cfg;
    cfg.chunkSize = 16;
    cfg.minChunk = ChunkCoord(-4, -4, -2);
    cfg.maxChunk = ChunkCoord(3, 3, 1);
    cfg.maxOperationRadius = 20;
    return cfg;
}

TerrainOperation Op(OperationType type, VoxelCoord center, uint32_t radius, Material m = Material::Air)
{
    TerrainOperation op;
    op.operationId = 1;
    op.type = type;
    op.worldId = 1;
    op.sequenceNumber = 1;
    op.center = center;
    op.radius = radius;
    op.material = m;
    return op;
}
} // namespace

OB_TEST(ValidOperationsPass)
{
    const WorldConfig cfg = Cfg();
    OB_CHECK_EQ(ValidateOperation(Op(OperationType::SubtractSphere, VoxelCoord(0, 0, 0), 5), cfg),
                ResultCode::Ok);
    OB_CHECK_EQ(
        ValidateOperation(Op(OperationType::AddSphere, VoxelCoord(-64, 63, -32), 20, Material::Rock), cfg),
        ResultCode::Ok);
}

OB_TEST(InvalidParametersAreRejectedWithSpecificCodes)
{
    const WorldConfig cfg = Cfg();
    TerrainOperation op = Op(OperationType::SubtractSphere, VoxelCoord(0, 0, 0), 5);

    op.radius = 0;
    OB_CHECK_EQ(ValidateOperation(op, cfg), ResultCode::InvalidRadius);
    op.radius = 21;
    OB_CHECK_EQ(ValidateOperation(op, cfg), ResultCode::RadiusTooLarge);
    op.radius = 5;

    op.schemaVersion = 99;
    OB_CHECK_EQ(ValidateOperation(op, cfg), ResultCode::OperationSchemaVersionMismatch);
    op.schemaVersion = TerrainOperation::kSchemaVersion;

    op.operationId = 0;
    OB_CHECK_EQ(ValidateOperation(op, cfg), ResultCode::InvalidOperationId);
    op.operationId = 1;

    op.type = OperationType::None;
    OB_CHECK_EQ(ValidateOperation(op, cfg), ResultCode::InvalidOperationType);
    op.type = static_cast<OperationType>(200);
    OB_CHECK_EQ(ValidateOperation(op, cfg), ResultCode::InvalidOperationType);
    op.type = OperationType::SubtractSphere;

    op.worldId = 2;
    OB_CHECK_EQ(ValidateOperation(op, cfg), ResultCode::WorldIdMismatch);
    op.worldId = 1;

    op.material = Material::Soil; // subtract must carry Air
    OB_CHECK_EQ(ValidateOperation(op, cfg), ResultCode::InvalidMaterial);
    op.material = Material::Air;

    op.center = VoxelCoord(64, 0, 0); // maxChunk.x = 3 -> max voxel 63
    OB_CHECK_EQ(ValidateOperation(op, cfg), ResultCode::CenterOutOfBounds);
    op.center = VoxelCoord(-65, 0, 0);
    OB_CHECK_EQ(ValidateOperation(op, cfg), ResultCode::CenterOutOfBounds);
    op.center = VoxelCoord(0, 0, 32);
    OB_CHECK_EQ(ValidateOperation(op, cfg), ResultCode::CenterOutOfBounds);
    op.center = VoxelCoord(63, -64, -32);
    OB_CHECK_EQ(ValidateOperation(op, cfg), ResultCode::Ok);

    TerrainOperation add = Op(OperationType::AddSphere, VoxelCoord(0, 0, 0), 3, Material::Air);
    OB_CHECK_EQ(ValidateOperation(add, cfg), ResultCode::InvalidMaterial);
    add.material = Material::Bedrock;
    OB_CHECK_EQ(ValidateOperation(add, cfg), ResultCode::InvalidMaterial);
    add.material = static_cast<Material>(77);
    OB_CHECK_EQ(ValidateOperation(add, cfg), ResultCode::InvalidMaterial);
}

OB_TEST(ExtremeCoordinatesNeverOverflow)
{
    // A world spanning the full addressable range with the largest chunks and a max radius.
    WorldConfig cfg;
    cfg.chunkSize = kMaxChunkSize;
    cfg.minChunk = ChunkCoord(-kMaxAbsChunkCoord, -kMaxAbsChunkCoord, -kMaxAbsChunkCoord);
    cfg.maxChunk = ChunkCoord(kMaxAbsChunkCoord, kMaxAbsChunkCoord, kMaxAbsChunkCoord);
    cfg.maxOperationRadius = 4096;
    OB_REQUIRE(Succeeded(cfg.Validate()));
    const VoxelBox bounds = cfg.VoxelBounds();
    TerrainOperation op = Op(OperationType::SubtractSphere, bounds.max, 4096);
    OB_CHECK_EQ(ValidateOperation(op, cfg), ResultCode::Ok);
    VoxelBox box;
    OB_REQUIRE(Succeeded(ComputeAffectedBox(op, cfg, box)));
    OB_CHECK(box.max == bounds.max); // clipped to the world
    OB_CHECK(box.min.x == bounds.max.x - 4096);

    op.center = bounds.min;
    OB_CHECK_EQ(ValidateOperation(op, cfg), ResultCode::Ok);
    OB_REQUIRE(Succeeded(ComputeAffectedBox(op, cfg, box)));
    OB_CHECK(box.min == bounds.min);

    // Centers that are numerically extreme are rejected as out of bounds before any arithmetic.
    op.center = VoxelCoord(std::numeric_limits<int64_t>::max(), 0, 0);
    OB_CHECK_EQ(ValidateOperation(op, cfg), ResultCode::CenterOutOfBounds);
    op.center = VoxelCoord(0, std::numeric_limits<int64_t>::min(), 0);
    OB_CHECK_EQ(ValidateOperation(op, cfg), ResultCode::CenterOutOfBounds);
}

OB_TEST(AffectedChunksAreSortedAndCoverTheBox)
{
    const WorldConfig cfg = Cfg();
    // Centered exactly on the corner shared by eight chunks.
    TerrainOperation op = Op(OperationType::SubtractSphere, VoxelCoord(0, 0, 0), 3);
    std::vector<ChunkCoord> chunks;
    OB_REQUIRE(Succeeded(ComputeAffectedChunks(op, cfg, chunks)));
    OB_REQUIRE(chunks.size() == 8);
    for (size_t i = 1; i < chunks.size(); ++i)
        OB_CHECK(chunks[i - 1] < chunks[i]);
    OB_CHECK(chunks.front() == ChunkCoord(-1, -1, -1));
    OB_CHECK(chunks.back() == ChunkCoord(0, 0, 0));

    // Fully inside one chunk.
    op.center = VoxelCoord(8, 8, 8);
    OB_REQUIRE(Succeeded(ComputeAffectedChunks(op, cfg, chunks)));
    OB_CHECK_EQ(chunks.size(), size_t(1));
    OB_CHECK(chunks[0] == ChunkCoord(0, 0, 0));

    // Clipped at the world edge: only in-bounds chunks are listed.
    op.center = VoxelCoord(63, 63, 31);
    op.radius = 20;
    OB_REQUIRE(Succeeded(ComputeAffectedChunks(op, cfg, chunks)));
    for (const ChunkCoord& c : chunks)
        OB_CHECK(cfg.ContainsChunk(c));
    OB_CHECK_EQ(chunks.size(), size_t(2 * 2 * 2));
}

OB_TEST(InsideSphereUsesInclusiveIntegerTest)
{
    OB_CHECK(InsideSphere(0, 0, 0, 1));
    OB_CHECK(InsideSphere(1, 0, 0, 1));
    OB_CHECK(!InsideSphere(1, 1, 0, 1));
    OB_CHECK(InsideSphere(3, 4, 0, 5));
    OB_CHECK(!InsideSphere(3, 4, 1, 5));
}

OB_TEST_MAIN()
