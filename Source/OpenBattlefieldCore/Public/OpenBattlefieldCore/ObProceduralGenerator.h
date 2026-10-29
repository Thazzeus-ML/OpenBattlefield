// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
#pragma once

#include "OpenBattlefieldCore/ObTypes.h"
#include "OpenBattlefieldCore/ObWorldConfig.h"

namespace ob
{

class Chunk;

/// Deterministic procedural foundation, generation version 1.
///
/// The surface is a heightfield built from integer value noise (SplitMix64 lattice hashes, fixed-point
/// smoothstep interpolation). No floating point is used anywhere, so the result is bitwise identical
/// on every platform and compiler. The same column height is produced whether a chunk is generated
/// wholesale or a single voxel is sampled, which keeps MaterialAt consistent with GenerateChunk.
///
/// Layering (top to bottom): Grass (topsoilDepth) -> Soil (soilDepth) -> Rock -> Bedrock at and below
/// bedrockHeight. Bedrock is indestructible.
class OBCORE_API ProceduralGenerator
{
public:
    /// Generation rule version implemented by this class.
    static constexpr uint32_t kGenerationVersion = 1;

    explicit ProceduralGenerator(const WorldConfig& config);

    /// Surface height (highest solid voxel z) of the column at (x, y).
    int64_t SurfaceHeight(int64_t x, int64_t y) const;

    /// Material of a column voxel given a precomputed surface height.
    Material MaterialForColumn(int64_t z, int64_t surfaceHeight) const;

    /// Material of a single voxel. Equivalent to generating the containing chunk and reading it.
    Material MaterialAt(const VoxelCoord& voxel) const;

    /// Fills a chunk with the procedural foundation. The chunk's version is left at zero.
    void GenerateChunk(Chunk& chunk) const;

    /// Raw lattice noise sample in [0, 65535] for one octave. Exposed for tests.
    uint32_t LatticeValue(uint32_t octave, int64_t ix, int64_t iy) const;

private:
    int64_t NoiseOctave(uint32_t octave, int64_t x, int64_t y) const;

    WorldConfig config_;
};

} // namespace ob
