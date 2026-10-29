// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
#include "ObTest.h"

#include "OpenBattlefieldCore/ObChunkMesher.h"
#include "OpenBattlefieldCore/ObTerrainWorld.h"

#include <cmath>

using namespace ob;

namespace
{
ChunkMeshInput EmptyInput(uint32_t size)
{
    ChunkMeshInput in;
    in.size = size;
    in.padded.assign(static_cast<size_t>(size + 2) * (size + 2) * (size + 2), 0);
    return in;
}

void Set(ChunkMeshInput& in, int32_t x, int32_t y, int32_t z, Material m)
{
    in.padded[in.PaddedIndex(x, y, z)] = static_cast<uint8_t>(m);
}

// Verifies cross(v1 - v0, v2 - v0) points along the stored normal for every triangle.
void CheckWinding(const MeshData& mesh)
{
    for (size_t t = 0; t < mesh.TriangleCount(); ++t)
    {
        const uint32_t i0 = mesh.indices[t * 3 + 0];
        const uint32_t i1 = mesh.indices[t * 3 + 1];
        const uint32_t i2 = mesh.indices[t * 3 + 2];
        const float* p0 = &mesh.positions[i0 * 3];
        const float* p1 = &mesh.positions[i1 * 3];
        const float* p2 = &mesh.positions[i2 * 3];
        const float ax = p1[0] - p0[0], ay = p1[1] - p0[1], az = p1[2] - p0[2];
        const float bx = p2[0] - p0[0], by = p2[1] - p0[1], bz = p2[2] - p0[2];
        const float cx = ay * bz - az * by;
        const float cy = az * bx - ax * bz;
        const float cz = ax * by - ay * bx;
        const float* n = &mesh.normals[i0 * 3];
        const float dot = cx * n[0] + cy * n[1] + cz * n[2];
        OB_CHECK(dot > 0.0f);
        OB_CHECK(mesh.normals[i1 * 3] == n[0] && mesh.normals[i2 * 3 + 2] == n[2]);
    }
}
} // namespace

OB_TEST(SingleVoxelProducesSixFaces)
{
    ChunkMeshInput in = EmptyInput(8);
    Set(in, 3, 3, 3, Material::Soil);
    MeshData mesh;
    OB_CHECK_EQ(BuildChunkMesh(in, mesh), size_t(6));
    OB_CHECK_EQ(mesh.VertexCount(), size_t(24));
    OB_CHECK_EQ(mesh.TriangleCount(), size_t(12));
    OB_CHECK_EQ(mesh.vertexMaterial.size(), size_t(24));
    OB_CHECK_EQ(mesh.vertexMaterial[0], static_cast<uint8_t>(Material::Soil));
    CheckWinding(mesh);
    // Every position lies on the unit cube at [3,4]^3.
    for (size_t i = 0; i < mesh.positions.size(); ++i)
        OB_CHECK(mesh.positions[i] >= 3.0f && mesh.positions[i] <= 4.0f);
}

OB_TEST(InteriorFacesAreCulled)
{
    ChunkMeshInput in = EmptyInput(8);
    Set(in, 3, 3, 3, Material::Soil);
    Set(in, 4, 3, 3, Material::Rock);
    MeshData mesh;
    OB_CHECK_EQ(BuildChunkMesh(in, mesh), size_t(10));
    CheckWinding(mesh);
}

OB_TEST(BoundaryFacesUseNeighbourBorder)
{
    ChunkMeshInput in = EmptyInput(4);
    Set(in, 0, 0, 0, Material::Soil);
    MeshData mesh;
    OB_CHECK_EQ(BuildChunkMesh(in, mesh), size_t(6));
    // A solid neighbour in the border (chunk to the -X) hides that face.
    Set(in, -1, 0, 0, Material::Rock);
    OB_CHECK_EQ(BuildChunkMesh(in, mesh), size_t(5));
}

OB_TEST(FullChunkMeshesOnlyItsShell)
{
    ChunkMeshInput in = EmptyInput(8);
    for (int32_t z = 0; z < 8; ++z)
        for (int32_t y = 0; y < 8; ++y)
            for (int32_t x = 0; x < 8; ++x)
                Set(in, x, y, z, Material::Rock);
    MeshData mesh;
    OB_CHECK_EQ(BuildChunkMesh(in, mesh), size_t(6 * 64));
    CheckWinding(mesh);
}

OB_TEST(MesherIsDeterministicAndRejectsBadInput)
{
    ChunkMeshInput in = EmptyInput(8);
    Set(in, 1, 2, 3, Material::Grass);
    Set(in, 5, 5, 5, Material::Soil);
    MeshData a;
    MeshData b;
    BuildChunkMesh(in, a);
    BuildChunkMesh(in, b);
    OB_CHECK(a.positions == b.positions && a.indices == b.indices && a.normals == b.normals);

    ChunkMeshInput bad = in;
    bad.padded.pop_back();
    MeshData m;
    OB_CHECK_EQ(BuildChunkMesh(bad, m), size_t(0));
    OB_CHECK_EQ(m.VertexCount(), size_t(0));
}

// Counts border voxels of a mesh input that differ from per-voxel world sampling (the reference
// definition of the border), including edges and corners.
size_t BorderMismatches(const TerrainWorld& world, const ChunkMeshInput& in)
{
    const int32_t n = static_cast<int32_t>(in.size);
    const VoxelCoord origin = ChunkOrigin(in.coord, in.size);
    size_t mismatches = 0;
    for (int32_t z = -1; z <= n; ++z)
        for (int32_t y = -1; y <= n; ++y)
            for (int32_t x = -1; x <= n; ++x)
            {
                if (x >= 0 && x < n && y >= 0 && y < n && z >= 0 && z < n)
                    continue;
                const VoxelCoord v(origin.x + x, origin.y + y, origin.z + z);
                mismatches += in.At(x, y, z) != world.SampleMaterial(v) ? 1u : 0u;
            }
    return mismatches;
}

OB_TEST(WorldBuildsVersionedMeshInputWithNeighbourSamples)
{
    WorldConfig cfg;
    cfg.chunkSize = 16;
    cfg.minChunk = ChunkCoord(-2, -2, -2);
    cfg.maxChunk = ChunkCoord(1, 1, 1);
    cfg.baseHeight = 8; // surface inside chunk z = 0
    cfg.heightAmplitude = 4;
    cfg.bedrockHeight = -20;
    TerrainWorld world(cfg);

    ChunkMeshInput in;
    OB_CHECK_EQ(world.BuildMeshInput(ChunkCoord(0, 0, 0), in), ResultCode::ChunkNotLoaded);
    OB_CHECK_EQ(world.BuildMeshInput(ChunkCoord(9, 0, 0), in), ResultCode::ChunkOutOfBounds);
    Chunk* chunk = nullptr;
    OB_REQUIRE(Succeeded(world.EnsureChunk(ChunkCoord(0, 0, 0), chunk)));
    OB_REQUIRE(Succeeded(world.BuildMeshInput(ChunkCoord(0, 0, 0), in)));
    OB_CHECK_EQ(in.version, ChunkVersion(0));
    OB_CHECK_EQ(in.size, 16u);

    // Interior matches the chunk, border matches the procedural neighbours (not loaded).
    for (int32_t z = 0; z < 16; ++z)
        for (int32_t y = 0; y < 16; ++y)
            for (int32_t x = 0; x < 16; ++x)
                OB_CHECK(in.At(x, y, z) == chunk->Get(LocalCoord(uint32_t(x), uint32_t(y), uint32_t(z))));
    for (int32_t y = 0; y < 16; ++y)
        OB_CHECK(in.At(-1, y, 5) == world.Generator().MaterialAt(VoxelCoord(-1, y, 5)));
    OB_CHECK_EQ(BorderMismatches(world, in), size_t(0));
    OB_CHECK_EQ(world.LoadedChunkCount(), size_t(1)); // snapshot did not load neighbours

    MeshData mesh;
    const size_t faces = BuildChunkMesh(in, mesh);
    OB_CHECK(faces >= 16 * 16); // at least the top surface
    CheckWinding(mesh);

    // A modification bumps the version so a stale snapshot can be detected by a publisher.
    TerrainOperation op;
    op.operationId = 1;
    op.sequenceNumber = 1;
    op.worldId = 1;
    op.type = OperationType::SubtractSphere;
    op.center = VoxelCoord(8, 8, 8);
    op.radius = 3;
    OB_REQUIRE(world.ApplyOperation(op).Accepted());
    ChunkMeshInput after;
    OB_REQUIRE(Succeeded(world.BuildMeshInput(ChunkCoord(0, 0, 0), after)));
    OB_CHECK_EQ(after.version, ChunkVersion(1));
    OB_CHECK(after.version != in.version);

    // Loaded neighbours that differ from the procedural foundation: craters straddling a face (+x)
    // and the (-x, -y) edge, plus a plain loaded neighbour below. The border must come from the
    // loaded chunks, not the generator.
    OB_REQUIRE(Succeeded(world.EnsureChunk(ChunkCoord(0, 0, -1), chunk)));
    op.operationId = 2;
    op.sequenceNumber = 2;
    op.center = VoxelCoord(16, 8, 8);
    OB_REQUIRE(world.ApplyOperation(op).Accepted());
    op.operationId = 3;
    op.sequenceNumber = 3;
    op.type = OperationType::AddSphere;
    op.material = Material::Rock;
    op.center = VoxelCoord(-1, -1, 10);
    OB_REQUIRE(world.ApplyOperation(op).Accepted());
    const Chunk* plusX = world.FindChunk(ChunkCoord(1, 0, 0));
    OB_REQUIRE(plusX != nullptr);
    OB_CHECK(plusX->IsModified());
    ChunkMeshInput mixed;
    OB_REQUIRE(Succeeded(world.BuildMeshInput(ChunkCoord(0, 0, 0), mixed)));
    OB_CHECK(mixed.At(16, 8, 8) == Material::Air);    // crater carved into the +x neighbour
    OB_CHECK(mixed.At(-1, -1, 10) == Material::Rock); // added sphere in the (-1, -1, 0) edge chunk
    OB_CHECK_EQ(BorderMismatches(world, mixed), size_t(0));

    // A chunk on the world's max corner: borders beyond the bounds are Air.
    OB_REQUIRE(Succeeded(world.EnsureChunk(ChunkCoord(1, 1, 1), chunk)));
    ChunkMeshInput corner;
    OB_REQUIRE(Succeeded(world.BuildMeshInput(ChunkCoord(1, 1, 1), corner)));
    OB_CHECK(corner.At(16, 3, 3) == Material::Air);
    OB_CHECK_EQ(BorderMismatches(world, corner), size_t(0));
    // And on the min corner, below the surface so out-of-bounds Air differs from the generator.
    OB_REQUIRE(Succeeded(world.EnsureChunk(ChunkCoord(-2, -2, -1), chunk)));
    ChunkMeshInput low;
    OB_REQUIRE(Succeeded(world.BuildMeshInput(ChunkCoord(-2, -2, -1), low)));
    OB_CHECK(low.At(-1, 3, 3) == Material::Air);
    OB_CHECK_EQ(BorderMismatches(world, low), size_t(0));
}

OB_TEST_MAIN()
