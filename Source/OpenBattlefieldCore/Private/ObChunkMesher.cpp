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

} // namespace ob
