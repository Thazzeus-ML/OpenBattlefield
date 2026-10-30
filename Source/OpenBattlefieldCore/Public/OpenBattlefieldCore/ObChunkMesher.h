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

/// One visible voxel face. The voxel's y and z are implied by the row that holds it.
struct MeshFace
{
    uint8_t x = 0;        // local x (chunk sizes are at most 128)
    uint8_t face = 0;     // index into the mesher's face table (+x, -x, +y, -y, +z, -z)
    uint8_t material = 0; // material of the solid voxel

    bool operator==(const MeshFace& o) const { return x == o.x && face == o.face && material == o.material; }
    bool operator!=(const MeshFace& o) const { return !(*this == o); }
};

/// The visible faces of one chunk, grouped into (y, z) rows in mesher order, so that a change can be
/// remeshed by rescanning only the voxels near it and reusing every other face. Derived,
/// non-authoritative data: EmitChunkMesh turns it into exactly the MeshData BuildChunkMesh produces.
struct OBCORE_API ChunkFaces
{
    ChunkCoord coord;
    uint32_t size = 0;
    /// Version of the snapshot the faces were last built or updated from.
    ChunkVersion version = 0;
    /// size * size + 1 offsets into faces; row y + size * z holds faces [rowStart[r], rowStart[r + 1]).
    std::vector<uint32_t> rowStart;
    std::vector<MeshFace> faces;

    /// True if these faces were built for a chunk of this coordinate and size.
    bool IsBuiltFor(const ChunkCoord& c, uint32_t chunkSize) const
    {
        return size == chunkSize && coord == c && rowStart.size() == static_cast<size_t>(size) * size + 1;
    }
};

/// Builds the faces of every voxel in the snapshot. Returns the number of faces (0 for bad input,
/// which also leaves out empty).
OBCORE_API size_t BuildChunkFaces(const ChunkMeshInput& input, ChunkFaces& out);

/// Brings faces up to date with input, given the inclusive local box of the chunk's dirty region since
/// faces were last built: only voxels within the box expanded by one voxel (the voxels whose faces can
/// have changed) are rescanned, and every other face is reused. Falls back to BuildChunkFaces when
/// faces were not built for this chunk. The result equals BuildChunkFaces(input) provided the box
/// covers every voxel that changed, which TerrainWorld's dirty regions guarantee (including border
/// voxels facing a neighbour's change). Returns the number of faces.
OBCORE_API size_t UpdateChunkFaces(const ChunkMeshInput& input, const LocalCoord& regionMin,
                                   const LocalCoord& regionMax, ChunkFaces& faces);

/// Writes the mesh for faces into out: the same bytes BuildChunkMesh produces for the same snapshot.
OBCORE_API void EmitChunkMesh(const ChunkFaces& faces, MeshData& out);

/// Writes a greedy mesh for faces into out: coplanar faces with the same direction and material are
/// merged into maximal rectangles, so a flat area of any size becomes a few quads. It covers exactly
/// the surface EmitChunkMesh covers, with the same normals, materials and winding, and is
/// deterministic. Merged quads meet smaller ones with T-junctions, which renderers may show as
/// single-pixel cracks; collision is unaffected. Returns the number of quads.
OBCORE_API size_t EmitGreedyChunkMesh(const ChunkFaces& faces, MeshData& out);

} // namespace ob
