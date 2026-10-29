// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
#include "OpenBattlefieldCore/ObChunkMesher.h"

namespace ob
{

namespace
{

struct FaceDef
{
    int32_t dx, dy, dz;  // neighbour direction / outward normal
    float corners[4][3]; // CCW when viewed from outside (right-handed), relative to voxel min corner
};

// For every face cross(c1 - c0, c2 - c0) equals the outward normal; verified by the mesher unit test.
constexpr FaceDef kFaces[6] = {
    {+1, 0, 0, {{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}}},
    {-1, 0, 0, {{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}},
    {0, +1, 0, {{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}}},
    {0, -1, 0, {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}},
    {0, 0, +1, {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}},
    {0, 0, -1, {{0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}}},
};

} // namespace

size_t BuildChunkMesh(const ChunkMeshInput& input, MeshData& out)
{
    out.Clear();
    const int32_t n = static_cast<int32_t>(input.size);
    if (n <= 0 || input.padded.size() !=
                      static_cast<size_t>(input.PaddedSize()) * input.PaddedSize() * input.PaddedSize())
        return 0;

    size_t faces = 0;
    for (int32_t z = 0; z < n; ++z)
    {
        for (int32_t y = 0; y < n; ++y)
        {
            for (int32_t x = 0; x < n; ++x)
            {
                const Material m = input.At(x, y, z);
                if (!IsSolid(m))
                    continue;
                for (const FaceDef& f : kFaces)
                {
                    if (IsSolid(input.At(x + f.dx, y + f.dy, z + f.dz)))
                        continue;
                    const uint32_t base = static_cast<uint32_t>(out.VertexCount());
                    for (int c = 0; c < 4; ++c)
                    {
                        out.positions.push_back(static_cast<float>(x) + f.corners[c][0]);
                        out.positions.push_back(static_cast<float>(y) + f.corners[c][1]);
                        out.positions.push_back(static_cast<float>(z) + f.corners[c][2]);
                        out.normals.push_back(static_cast<float>(f.dx));
                        out.normals.push_back(static_cast<float>(f.dy));
                        out.normals.push_back(static_cast<float>(f.dz));
                        out.vertexMaterial.push_back(static_cast<uint8_t>(m));
                    }
                    out.indices.push_back(base + 0);
                    out.indices.push_back(base + 1);
                    out.indices.push_back(base + 2);
                    out.indices.push_back(base + 0);
                    out.indices.push_back(base + 2);
                    out.indices.push_back(base + 3);
                    ++faces;
                }
            }
        }
    }
    return faces;
}

namespace
{

bool IsValidInput(const ChunkMeshInput& input)
{
    const size_t p = input.PaddedSize();
    return input.size > 0 && input.size <= 256 && input.padded.size() == p * p * p;
}

// Appends the faces of voxels x0..x1 of row (y, z) in mesher order (x, then face table order).
void ScanRow(const ChunkMeshInput& input, int32_t y, int32_t z, int32_t x0, int32_t x1,
             std::vector<MeshFace>& out)
{
    for (int32_t x = x0; x <= x1; ++x)
    {
        const Material m = input.At(x, y, z);
        if (!IsSolid(m))
            continue;
        for (uint8_t f = 0; f < 6; ++f)
        {
            const FaceDef& d = kFaces[f];
            if (IsSolid(input.At(x + d.dx, y + d.dy, z + d.dz)))
                continue;
            MeshFace face;
            face.x = static_cast<uint8_t>(x);
            face.face = f;
            face.material = static_cast<uint8_t>(m);
            out.push_back(face);
        }
    }
}

} // namespace

size_t BuildChunkFaces(const ChunkMeshInput& input, ChunkFaces& out)
{
    out.faces.clear();
    out.rowStart.clear();
    out.size = 0;
    if (!IsValidInput(input))
        return 0;
    const int32_t n = static_cast<int32_t>(input.size);
    out.coord = input.coord;
    out.size = input.size;
    out.version = input.version;
    out.rowStart.resize(static_cast<size_t>(n) * static_cast<size_t>(n) + 1);
    size_t row = 0;
    for (int32_t z = 0; z < n; ++z)
        for (int32_t y = 0; y < n; ++y)
        {
            out.rowStart[row++] = static_cast<uint32_t>(out.faces.size());
            ScanRow(input, y, z, 0, n - 1, out.faces);
        }
    out.rowStart[row] = static_cast<uint32_t>(out.faces.size());
    return out.faces.size();
}

size_t UpdateChunkFaces(const ChunkMeshInput& input, const LocalCoord& regionMin, const LocalCoord& regionMax,
                        ChunkFaces& faces)
{
    if (!IsValidInput(input) || !faces.IsBuiltFor(input.coord, input.size))
        return BuildChunkFaces(input, faces);
    const int32_t n = static_cast<int32_t>(input.size);
    const uint32_t last = input.size - 1;
    if (regionMin.x > regionMax.x || regionMin.y > regionMax.y || regionMin.z > regionMax.z ||
        regionMax.x > last || regionMax.y > last || regionMax.z > last)
        return BuildChunkFaces(input, faces); // not a region TerrainWorld produces: rebuild safely
    faces.version = input.version;

    // A voxel's faces depend on itself and its six face neighbours, so the voxels whose faces can have
    // changed are the region expanded by one, clipped to the chunk.
    const auto lo = [](uint32_t v) { return v == 0 ? 0 : static_cast<int32_t>(v) - 1; };
    const auto hi = [last](uint32_t v) { return static_cast<int32_t>(v < last ? v + 1 : last); };
    const int32_t x0 = lo(regionMin.x), y0 = lo(regionMin.y), z0 = lo(regionMin.z);
    const int32_t x1 = hi(regionMax.x), y1 = hi(regionMax.y), z1 = hi(regionMax.z);

    std::vector<MeshFace> next;
    next.reserve(faces.faces.size() + 64);
    std::vector<uint32_t> nextStart(faces.rowStart.size());
    const MeshFace* old = faces.faces.data();
    size_t row = 0;
    for (int32_t z = 0; z < n; ++z)
        for (int32_t y = 0; y < n; ++y, ++row)
        {
            nextStart[row] = static_cast<uint32_t>(next.size());
            const uint32_t begin = faces.rowStart[row];
            const uint32_t end = faces.rowStart[row + 1];
            if (z < z0 || z > z1 || y < y0 || y > y1)
            {
                next.insert(next.end(), old + begin, old + end);
                continue;
            }
            // Faces within a row are ordered by x: keep those left of the rescanned span, rescan the
            // span, keep those right of it.
            uint32_t i = begin;
            while (i < end && old[i].x < x0)
                next.push_back(old[i++]);
            while (i < end && old[i].x <= x1)
                ++i;
            ScanRow(input, y, z, x0, x1, next);
            next.insert(next.end(), old + i, old + end);
        }
    nextStart[row] = static_cast<uint32_t>(next.size());
    faces.faces.swap(next);
    faces.rowStart.swap(nextStart);
    return faces.faces.size();
}

void EmitChunkMesh(const ChunkFaces& faces, MeshData& out)
{
    out.Clear();
    if (faces.rowStart.size() != static_cast<size_t>(faces.size) * faces.size + 1)
        return;
    out.positions.reserve(faces.faces.size() * 12);
    out.normals.reserve(faces.faces.size() * 12);
    out.vertexMaterial.reserve(faces.faces.size() * 4);
    out.indices.reserve(faces.faces.size() * 6);
    const int32_t n = static_cast<int32_t>(faces.size);
    size_t row = 0;
    for (int32_t z = 0; z < n; ++z)
        for (int32_t y = 0; y < n; ++y, ++row)
            for (uint32_t i = faces.rowStart[row]; i < faces.rowStart[row + 1]; ++i)
            {
                // Same arithmetic and order as BuildChunkMesh, so the bytes are identical.
                const MeshFace& mf = faces.faces[i];
                const FaceDef& f = kFaces[mf.face];
                const int32_t x = mf.x;
                const uint32_t base = static_cast<uint32_t>(out.VertexCount());
                for (int c = 0; c < 4; ++c)
                {
                    out.positions.push_back(static_cast<float>(x) + f.corners[c][0]);
                    out.positions.push_back(static_cast<float>(y) + f.corners[c][1]);
                    out.positions.push_back(static_cast<float>(z) + f.corners[c][2]);
                    out.normals.push_back(static_cast<float>(f.dx));
                    out.normals.push_back(static_cast<float>(f.dy));
                    out.normals.push_back(static_cast<float>(f.dz));
                    out.vertexMaterial.push_back(mf.material);
                }
                out.indices.push_back(base + 0);
                out.indices.push_back(base + 1);
                out.indices.push_back(base + 2);
                out.indices.push_back(base + 0);
                out.indices.push_back(base + 2);
                out.indices.push_back(base + 3);
            }
}

} // namespace ob
