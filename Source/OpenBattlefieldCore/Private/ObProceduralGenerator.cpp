// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
#include "OpenBattlefieldCore/ObProceduralGenerator.h"

#include "OpenBattlefieldCore/ObChunk.h"
#include "OpenBattlefieldCore/ObCoordinates.h"
#include "OpenBattlefieldCore/ObHash.h"

namespace ob
{

namespace
{
constexpr int64_t kOne = 1ll << 16; // Q16 fixed point one

// Floor shift that does not rely on implementation-defined right shifts of negative values.
constexpr int64_t FloorShift16(int64_t v)
{
    return FloorDiv(v, kOne);
}
} // namespace

ProceduralGenerator::ProceduralGenerator(const WorldConfig& config) : config_(config) {}

uint32_t ProceduralGenerator::LatticeValue(uint32_t octave, int64_t ix, int64_t iy) const
{
    uint64_t h = HashCombine(config_.seed, static_cast<uint64_t>(config_.generationVersion));
    h = HashCombine(h, octave);
    h = HashCombine(h, static_cast<uint64_t>(ix));
    h = HashCombine(h, static_cast<uint64_t>(iy));
    return static_cast<uint32_t>(h >> 48); // 16 bits
}

int64_t ProceduralGenerator::NoiseOctave(uint32_t octave, int64_t x, int64_t y) const
{
    const uint32_t spacingLog2 = config_.noiseScaleLog2 > octave ? config_.noiseScaleLog2 - octave : 0u;
    const int64_t spacing = 1ll << spacingLog2;

    const int64_t ix = FloorDiv(x, spacing);
    const int64_t iy = FloorDiv(y, spacing);
    const int64_t fx = FloorMod(x, spacing);
    const int64_t fy = FloorMod(y, spacing);

    // Fractional position in Q16, then smoothstep t*t*(3 - 2t) in Q16.
    const int64_t tx = (fx * kOne) / spacing;
    const int64_t ty = (fy * kOne) / spacing;
    const int64_t sx = (tx * tx * (3 * kOne - 2 * tx)) >> 32; // non-negative, plain shift is exact
    const int64_t sy = (ty * ty * (3 * kOne - 2 * ty)) >> 32;

    const int64_t v00 = LatticeValue(octave, ix, iy);
    const int64_t v10 = LatticeValue(octave, ix + 1, iy);
    const int64_t v01 = LatticeValue(octave, ix, iy + 1);
    const int64_t v11 = LatticeValue(octave, ix + 1, iy + 1);

    const int64_t a = v00 + FloorShift16((v10 - v00) * sx);
    const int64_t b = v01 + FloorShift16((v11 - v01) * sx);
    return a + FloorShift16((b - a) * sy); // in [0, 65535]
}

int64_t ProceduralGenerator::SurfaceHeight(int64_t x, int64_t y) const
{
    int64_t height = config_.baseHeight;
    for (uint32_t o = 0; o < config_.noiseOctaves; ++o)
    {
        const int64_t amplitude = static_cast<int64_t>(config_.heightAmplitude) >> o;
        if (amplitude == 0)
            break;
        const int64_t n = NoiseOctave(o, x, y) - 32768; // [-32768, 32767]
        height += FloorDiv(n * amplitude, 32768);
    }
    return height;
}

Material ProceduralGenerator::MaterialForColumn(int64_t z, int64_t surfaceHeight) const
{
    if (z <= config_.bedrockHeight)
        return Material::Bedrock;
    if (z > surfaceHeight)
        return Material::Air;
    const int64_t topsoilBottom = surfaceHeight - static_cast<int64_t>(config_.topsoilDepth);
    if (z > topsoilBottom)
        return Material::Grass;
    const int64_t soilBottom = topsoilBottom - static_cast<int64_t>(config_.soilDepth);
    if (z > soilBottom)
        return Material::Soil;
    return Material::Rock;
}

Material ProceduralGenerator::MaterialAt(const VoxelCoord& voxel) const
{
    return MaterialForColumn(voxel.z, SurfaceHeight(voxel.x, voxel.y));
}

void ProceduralGenerator::GenerateChunk(Chunk& chunk) const
{
    const uint32_t size = chunk.Size();
    const VoxelCoord origin = ChunkOrigin(chunk.Coord(), size);
    for (uint32_t ly = 0; ly < size; ++ly)
    {
        for (uint32_t lx = 0; lx < size; ++lx)
        {
            const int64_t h =
                SurfaceHeight(origin.x + static_cast<int64_t>(lx), origin.y + static_cast<int64_t>(ly));
            for (uint32_t lz = 0; lz < size; ++lz)
            {
                const Material m = MaterialForColumn(origin.z + static_cast<int64_t>(lz), h);
                if (m != Material::Air)
                    chunk.SetRaw(LocalCoord(lx, ly, lz), m);
            }
        }
    }
    chunk.RecountSolids();
}

} // namespace ob
