// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
#pragma once

#include "OpenBattlefieldCore/ObTypes.h"

#include <vector>

namespace ob
{

/// Immutable snapshot of one chunk plus a one-voxel border of its neighbours, suitable for handing to
/// a worker thread. Created by TerrainWorld::BuildMeshInput. The version lets the publisher reject
/// results that were computed against a chunk state that has since changed.
struct OBCORE_API ChunkMeshInput
{
    ChunkCoord coord;
    uint32_t size = 0;
    ChunkVersion version = 0;
    /// (size + 2)^3 materials, X fastest. Index (x + 1, y + 1, z + 1) is local voxel (x, y, z).
    std::vector<uint8_t> padded;

    uint32_t PaddedSize() const { return size + 2; }
    size_t PaddedIndex(int32_t x, int32_t y, int32_t z) const
    {
        const size_t p = PaddedSize();
        return static_cast<size_t>(x + 1) + p * (static_cast<size_t>(y + 1) + p * static_cast<size_t>(z + 1));
    }
    Material At(int32_t x, int32_t y, int32_t z) const
    {
        return static_cast<Material>(padded[PaddedIndex(x, y, z)]);
    }
};

/// CPU-generated triangle mesh in chunk-local voxel units. Derived, non-authoritative data: floating
/// point is acceptable here because nothing hashes or replays it.
///
/// Winding: each quad is emitted so that cross(v1 - v0, v2 - v0) points along the stored normal
/// (counter-clockwise when viewed from outside in a right-handed frame). Adapters that need the
/// opposite convention flip the index order.
struct OBCORE_API MeshData
{
    std::vector<float> positions;        // 3 floats per vertex, voxel units, origin at chunk min corner
    std::vector<float> normals;          // 3 floats per vertex
    std::vector<uint8_t> vertexMaterial; // material id per vertex (flat per face)
    std::vector<uint32_t> indices;       // 3 per triangle

    size_t VertexCount() const { return positions.size() / 3; }
    size_t TriangleCount() const { return indices.size() / 3; }
    void Clear()
    {
        positions.clear();
        normals.clear();
        vertexMaterial.clear();
        indices.clear();
    }
};

/// Builds a face-culled cube mesh: one quad for every solid voxel face adjacent to Air, including
/// faces on the chunk boundary (the padded border supplies neighbour chunks). Output is deterministic
/// for a given input. Returns the number of faces emitted.
OBCORE_API size_t BuildChunkMesh(const ChunkMeshInput& input, MeshData& out);

} // namespace ob
