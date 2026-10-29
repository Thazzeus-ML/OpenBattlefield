// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
//
// Headless visual snapshot of the terrain pipeline: no Unreal Engine, no dependencies.
//
//   ob_snapshot --out FILE.png [--ops N] [--seed S] [--width W] [--height H]
//
// Builds a small walled diorama world, applies a deterministic artillery barrage through the
// authoritative path (ApplyOperation), and keeps one mesh per chunk the way a renderer does: every
// chunk is meshed once, then after each flush only the chunks TerrainWorld reports dirty are
// rebuilt. The cached meshes are rasterised in an isometric view with flat shading, alternate chunks
// tinted so boundaries are visible. If dirty tracking misses a chunk, its stale faces show up as
// holes or floating faces: every cached mesh is compared with a fresh rebuild, stale chunks are drawn
// red, their count is printed, and the exit code is 3.

#include "OpenBattlefieldCore/OpenBattlefieldCore.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace ob;

namespace
{

// --- PNG (stored deflate blocks, no compression library) -------------------------------------------

uint32_t Crc32(const uint8_t* data, size_t n, uint32_t crc = 0)
{
    static uint32_t table[256];
    static bool init = false;
    if (!init)
    {
        for (uint32_t i = 0; i < 256; ++i)
        {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < n; ++i)
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

void PutU32(std::vector<uint8_t>& out, uint32_t v)
{
    out.push_back(static_cast<uint8_t>(v >> 24));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

void PutChunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data)
{
    PutU32(out, static_cast<uint32_t>(data.size()));
    std::vector<uint8_t> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    out.insert(out.end(), body.begin(), body.end());
    PutU32(out, Crc32(body.data(), body.size()));
}

bool WritePng(const std::string& path, int w, int h, const std::vector<uint8_t>& rgb)
{
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(h) * (static_cast<size_t>(w) * 3 + 1));
    for (int y = 0; y < h; ++y)
    {
        raw.push_back(0); // filter: none
        const uint8_t* row = &rgb[static_cast<size_t>(y) * static_cast<size_t>(w) * 3];
        raw.insert(raw.end(), row, row + static_cast<size_t>(w) * 3);
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    uint32_t a = 1, b = 0;
    for (uint8_t v : raw)
    {
        a = (a + v) % 65521u;
        b = (b + a) % 65521u;
    }
    for (size_t off = 0; off < raw.size() || off == 0; off += 65535)
    {
        const size_t len = std::min<size_t>(65535, raw.size() - off);
        z.push_back(off + len >= raw.size() ? 1 : 0);
        z.push_back(static_cast<uint8_t>(len));
        z.push_back(static_cast<uint8_t>(len >> 8));
        z.push_back(static_cast<uint8_t>(~len));
        z.push_back(static_cast<uint8_t>(~len >> 8));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(off),
                 raw.begin() + static_cast<std::ptrdiff_t>(off + len));
        if (raw.empty())
            break;
    }
    PutU32(z, (b << 16) | a);

    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> ihdr;
    PutU32(ihdr, static_cast<uint32_t>(w));
    PutU32(ihdr, static_cast<uint32_t>(h));
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0}); // 8-bit RGB
    PutChunk(png, "IHDR", ihdr);
    PutChunk(png, "IDAT", z);
    PutChunk(png, "IEND", {});
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f)
        return false;
    const bool ok = std::fwrite(png.data(), 1, png.size(), f) == png.size();
    return std::fclose(f) == 0 && ok;
}

// --- Rasteriser -------------------------------------------------------------------------------------

struct Vec3
{
    float x, y, z;
};

struct Image
{
    int w, h;
    std::vector<uint8_t> rgb;
    std::vector<float> depth;
};

// Isometric camera on the (+x, +y, +z) diagonal: larger x + y + z is nearer.
void Project(const Vec3& p, float& sx, float& sy, float& d)
{
    sx = (p.x - p.y) * 0.8660254f;
    sy = (p.x + p.y) * 0.5f - p.z;
    d = p.x + p.y + p.z;
}

void Shade(const uint8_t base[3], float k, uint8_t out[3])
{
    for (int i = 0; i < 3; ++i)
        out[i] = static_cast<uint8_t>(std::min(255.0f, static_cast<float>(base[i]) * k));
}

void FillTriangle(Image& img, const float x[3], const float y[3], const float d[3], const uint8_t c[3])
{
    const float area = (x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0]);
    if (std::fabs(area) < 1e-6f)
        return;
    const int x0 = std::max(0, static_cast<int>(std::floor(std::min({x[0], x[1], x[2]}))));
    const int x1 = std::min(img.w - 1, static_cast<int>(std::ceil(std::max({x[0], x[1], x[2]}))));
    const int y0 = std::max(0, static_cast<int>(std::floor(std::min({y[0], y[1], y[2]}))));
    const int y1 = std::min(img.h - 1, static_cast<int>(std::ceil(std::max({y[0], y[1], y[2]}))));
    for (int py = y0; py <= y1; ++py)
        for (int px = x0; px <= x1; ++px)
        {
            const float fx = static_cast<float>(px) + 0.5f, fy = static_cast<float>(py) + 0.5f;
            const float w0 = ((x[1] - fx) * (y[2] - fy) - (x[2] - fx) * (y[1] - fy)) / area;
            const float w1 = ((x[2] - fx) * (y[0] - fy) - (x[0] - fx) * (y[2] - fy)) / area;
            const float w2 = 1.0f - w0 - w1;
            if (w0 < -1e-4f || w1 < -1e-4f || w2 < -1e-4f)
                continue;
            const float depth = w0 * d[0] + w1 * d[1] + w2 * d[2];
            const size_t i = static_cast<size_t>(py) * static_cast<size_t>(img.w) + static_cast<size_t>(px);
            if (depth <= img.depth[i])
                continue;
            img.depth[i] = depth;
            std::memcpy(&img.rgb[i * 3], c, 3);
        }
}

const uint8_t* MaterialColour(uint8_t m)
{
    static const uint8_t colours[5][3] = {
        {0, 0, 0}, {126, 88, 58}, {92, 148, 64}, {128, 128, 134}, {58, 56, 62}};
    return colours[m < 5 ? m : 0];
}

// --- Scene ------------------------------------------------------------------------------------------

struct Options
{
    std::string out = "snapshot.png";
    uint32_t ops = 160;
    uint64_t seed = 20240601;
    int width = 1280;
    int height = 860;
};

bool SameMesh(const MeshData& a, const MeshData& b)
{
    return a.positions == b.positions && a.indices == b.indices && a.vertexMaterial == b.vertexMaterial;
}

} // namespace

int main(int argc, char** argv)
{
    Options opt;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        const char* val = i + 1 < argc ? argv[i + 1] : nullptr;
        if (arg == "--out" && val)
            opt.out = argv[++i];
        else if (arg == "--ops" && val)
            opt.ops = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        else if (arg == "--seed" && val)
            opt.seed = std::strtoull(argv[++i], nullptr, 10);
        else if (arg == "--width" && val)
            opt.width = std::atoi(argv[++i]);
        else if (arg == "--height" && val)
            opt.height = std::atoi(argv[++i]);
        else
        {
            std::fprintf(stderr,
                         "usage: ob_snapshot --out FILE.png [--ops N] [--seed S] [--width W] [--height H]\n");
            return 2;
        }
    }
    if (opt.width < 64 || opt.height < 64 || opt.width > 8192 || opt.height > 8192)
    {
        std::fprintf(stderr, "image size out of range\n");
        return 2;
    }

    // A 4 x 4 x 2 chunk diorama: the world bounds are the region, so its sides are walls of Air.
    WorldConfig cfg;
    cfg.seed = opt.seed;
    cfg.chunkSize = 32;
    cfg.minChunk = ChunkCoord(-2, -2, -1);
    cfg.maxChunk = ChunkCoord(1, 1, 0);
    cfg.baseHeight = 4;
    cfg.heightAmplitude = 14;
    cfg.bedrockHeight = -30;
    cfg.maxOperationRadius = 16;
    TerrainWorld world(cfg);
    if (Failed(world.Status()))
    {
        std::fprintf(stderr, "invalid world config: %s\n", ToString(world.Status()));
        return 1;
    }
    const int32_t n = static_cast<int32_t>(cfg.chunkSize);
    for (int32_t z = cfg.minChunk.z; z <= cfg.maxChunk.z; ++z)
        for (int32_t y = cfg.minChunk.y; y <= cfg.maxChunk.y; ++y)
            for (int32_t x = cfg.minChunk.x; x <= cfg.maxChunk.x; ++x)
            {
                Chunk* c = nullptr;
                world.EnsureChunk(ChunkCoord(x, y, z), c);
            }

    std::map<ChunkCoord, MeshData> meshes;
    ChunkMeshInput input;
    world.ForEachChunk(
        [&](const Chunk& c)
        {
            if (Succeeded(world.BuildMeshInput(c.Coord(), input)))
                BuildChunkMesh(input, meshes[c.Coord()]);
        });

    // Artillery: deterministic craters and rubble, many landing on or near chunk boundaries.
    uint64_t rng = opt.seed;
    auto next = [&rng](uint64_t bound)
    {
        rng = rng * 6364136223846793005ull + 1442695040888963407ull;
        return (rng >> 33) % bound;
    };
    uint64_t accepted = 0, changed = 0, remeshes = 0;
    std::vector<ChunkCoord> dirty;
    for (uint32_t i = 0; i < opt.ops; ++i)
    {
        TerrainOperation op;
        op.operationId = 1 + i;
        op.sequenceNumber = 1 + i;
        op.worldId = cfg.worldId;
        int64_t x = static_cast<int64_t>(next(120)) - 60;
        int64_t y = static_cast<int64_t>(next(120)) - 60;
        if (next(3) == 0) // snap to a chunk boundary line
            x = (x / n) * n + (next(2) == 0 ? -2 : 2);
        op.radius = 3 + static_cast<uint32_t>(next(7));
        const int64_t h = world.Generator().SurfaceHeight(x, y);
        if (next(5) == 0)
        {
            op.type = OperationType::AddSphere; // rubble mound
            op.material = Material::Rock;
            op.radius = 2 + static_cast<uint32_t>(next(3));
            op.center = VoxelCoord(x, y, h);
        }
        else
        {
            op.type = OperationType::SubtractSphere;
            op.center = VoxelCoord(x, y, h - static_cast<int64_t>(op.radius) / 3);
        }
        const OperationResult r = world.ApplyOperation(op);
        accepted += r.Accepted() ? 1u : 0u;
        changed += r.changedVoxels;
        if ((i + 1) % 8 == 0 || i + 1 == opt.ops)
        {
            // Renderer-style flush: rebuild only what TerrainWorld reports as dirty.
            world.TakeDirtyChunks(dirty);
            for (const ChunkCoord& c : dirty)
            {
                if (Chunk* chunk = world.FindChunk(c))
                    chunk->ConsumeDirtyRegion();
                if (Succeeded(world.BuildMeshInput(c, input)))
                {
                    BuildChunkMesh(input, meshes[c]);
                    ++remeshes;
                }
            }
        }
    }

    // How many cached meshes differ from a fresh rebuild: anything above zero is a dirty-tracking bug.
    size_t faces = 0;
    std::map<ChunkCoord, bool> staleChunks;
    world.ForEachChunk(
        [&](const Chunk& c)
        {
            MeshData fresh;
            if (Succeeded(world.BuildMeshInput(c.Coord(), input)))
                faces += BuildChunkMesh(input, fresh);
            if (!SameMesh(fresh, meshes[c.Coord()]))
                staleChunks[c.Coord()] = true;
        });
    const size_t stale = staleChunks.size();

    // Fit the projected world box to the image.
    const VoxelBox bounds = cfg.VoxelBounds();
    float minX = 1e30f, maxX = -1e30f, minY = 1e30f, maxY = -1e30f;
    for (int corner = 0; corner < 8; ++corner)
    {
        const Vec3 p{static_cast<float>((corner & 1) ? bounds.max.x + 1 : bounds.min.x),
                     static_cast<float>((corner & 2) ? bounds.max.y + 1 : bounds.min.y),
                     static_cast<float>((corner & 4) ? bounds.max.z + 1 : -20)};
        float sx, sy, d;
        Project(p, sx, sy, d);
        minX = std::min(minX, sx);
        maxX = std::max(maxX, sx);
        minY = std::min(minY, sy);
        maxY = std::max(maxY, sy);
    }
    const float margin = 24.0f;
    const float scale = std::min((static_cast<float>(opt.width) - 2 * margin) / (maxX - minX),
                                 (static_cast<float>(opt.height) - 2 * margin) / (maxY - minY));
    const float ox = (static_cast<float>(opt.width) - (maxX - minX) * scale) * 0.5f - minX * scale;
    const float oy = (static_cast<float>(opt.height) - (maxY - minY) * scale) * 0.5f - minY * scale;

    Image img{opt.width, opt.height, {}, {}};
    img.rgb.resize(static_cast<size_t>(opt.width) * static_cast<size_t>(opt.height) * 3);
    img.depth.assign(static_cast<size_t>(opt.width) * static_cast<size_t>(opt.height), -1e30f);
    for (int py = 0; py < opt.height; ++py)
        for (int px = 0; px < opt.width; ++px)
        {
            const float t = static_cast<float>(py) / static_cast<float>(opt.height);
            uint8_t* p = &img.rgb[(static_cast<size_t>(py) * static_cast<size_t>(opt.width) +
                                   static_cast<size_t>(px)) *
                                  3];
            p[0] = static_cast<uint8_t>(206 - 40 * t);
            p[1] = static_cast<uint8_t>(220 - 36 * t);
            p[2] = static_cast<uint8_t>(232 - 30 * t);
        }

    const Vec3 light{0.45f, 0.25f, 0.86f};
    for (const auto& kv : meshes)
    {
        const MeshData& m = kv.second;
        const VoxelCoord o = ChunkOrigin(kv.first, cfg.chunkSize);
        const bool odd = ((kv.first.x + kv.first.y + kv.first.z) & 1) != 0;
        const bool isStale = staleChunks.count(kv.first) != 0; // drawn red so it can be found
        for (size_t t = 0; t < m.TriangleCount(); ++t)
        {
            const uint32_t i0 = m.indices[t * 3];
            const Vec3 nrm{m.normals[i0 * 3], m.normals[i0 * 3 + 1], m.normals[i0 * 3 + 2]};
            if (nrm.x + nrm.y + nrm.z <= 0.0f)
                continue; // faces pointing away from the camera
            float sx[3], sy[3], sd[3];
            for (int v = 0; v < 3; ++v)
            {
                const uint32_t vi = m.indices[t * 3 + static_cast<size_t>(v)];
                const Vec3 p{m.positions[vi * 3] + static_cast<float>(o.x),
                             m.positions[vi * 3 + 1] + static_cast<float>(o.y),
                             m.positions[vi * 3 + 2] + static_cast<float>(o.z)};
                Project(p, sx[v], sy[v], sd[v]);
                sx[v] = sx[v] * scale + ox;
                sy[v] = sy[v] * scale + oy;
            }
            const float lambert = std::max(0.0f, nrm.x * light.x + nrm.y * light.y + nrm.z * light.z);
            const float k = (0.42f + 0.62f * lambert) * (odd ? 0.9f : 1.0f);
            uint8_t c[3];
            static const uint8_t kStale[3] = {214, 52, 44};
            Shade(isStale ? kStale : MaterialColour(m.vertexMaterial[i0]), k, c);
            FillTriangle(img, sx, sy, sd, c);
        }
    }

    if (!WritePng(opt.out, opt.width, opt.height, img.rgb))
    {
        std::fprintf(stderr, "could not write %s\n", opt.out.c_str());
        return 1;
    }
    std::printf("{\"operations\": %u, \"accepted\": %llu, \"changed_voxels\": %llu, \"chunks\": %zu, "
                "\"chunk_remeshes\": %llu, \"faces\": %zu, \"stale_chunks\": %zu, \"state_hash\": "
                "\"0x%016llx\", \"out\": \"%s\"}\n",
                opt.ops, static_cast<unsigned long long>(accepted), static_cast<unsigned long long>(changed),
                world.LoadedChunkCount(), static_cast<unsigned long long>(remeshes), faces, stale,
                static_cast<unsigned long long>(world.ComputeStateHash()), opt.out.c_str());
    return stale == 0 ? 0 : 3;
}
