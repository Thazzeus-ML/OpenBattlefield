# Terrain representation (baseline)

## Choice

The first implementation is a **dense voxel material grid**: one `uint8` material id per voxel,
stored per chunk in X-fastest order (`LocalIndex`). It was chosen because it is simple, trivially
correct, fully deterministic, and gives every later representation a reference to be compared
against.

A chunk whose voxels all hold one material (all `Air` above the surface, all `Rock` deep below) is
stored as that single value with no voxel array (`Chunk::IsUniform`). The first change that breaks
the uniformity allocates the dense array; `Compact()` frees it again and runs after generation and
deserialization. Reads, hashes and serialized bytes are identical in both forms, so this is a storage
detail, not a second representation.

What it supports today:

* Seeded procedural initialization (`ProceduralGenerator`).
* Empty (`Air`) and solid materials (`Soil`, `Grass`, `Rock`, `Bedrock`).
* Deterministic spherical subtraction and addition.
* Modifications spanning any number of neighbouring chunks.
* Dirty-region identification (per-chunk inclusive AABB).
* Chunk version increments on content change.
* Reconstruction from seed plus journal.
* True 3D occupancy: craters can undercut, tunnels and overhangs are representable because the
  grid is volumetric, not a heightfield. The *generator* currently produces a heightfield foundation,
  so underground features only appear through modifications.

What it does not do (yet):

* Smooth surfaces. Geometry is cubic. A signed-distance or density layer is a planned alternative
  (see "Future representations").
* Memory efficiency. A 32^3 chunk costs 32 KiB regardless of content. Measured: 144 chunks
  (4.7 M voxels) use about 4.7 MB; see `benchmarking.md`. Run-length encoding is used only for
  serialization.
* Material blending or per-voxel damage state.

## Materials

| Id | Name | Solid | Destructible | Generated at |
|---|---|---|---|---|
| 0 | Air | no | – | above surface |
| 1 | Soil | yes | yes | `soilDepth` voxels below the topsoil |
| 2 | Grass | yes | yes | top `topsoilDepth` voxels |
| 3 | Rock | yes | yes | everything below soil |
| 4 | Bedrock | yes | **no** | `z <= bedrockHeight` |

Ids are serialized and must never be renumbered. New materials are appended.

## Procedural foundation (generation version 1)

Surface height per column is base height plus octaves of integer value noise:

* Lattice values: `SplitMix64(seed, generationVersion, octave, ix, iy) >> 48` (16 bits).
* Interpolation: fixed-point Q16 smoothstep, floor shifts implemented as floor division so no
  implementation-defined behaviour is involved.
* Octave `o` has lattice spacing `2^(noiseScaleLog2 - o)` and amplitude `heightAmplitude >> o`.

`MaterialAt(voxel)` and `GenerateChunk` share `SurfaceHeight` and `MaterialForColumn`, so sampling a
voxel without loading its chunk returns exactly what the chunk would contain
(`MaterialAtMatchesGeneratedChunk`). Any change to this algorithm must bump
`ProceduralGenerator::kGenerationVersion` and the pinned hash in `TestGenerator.cpp`.

## Chunk lifecycle

* Chunks are created lazily by `TerrainWorld::EnsureChunk`, either when an operation touches them or
  on explicit request. `SampleMaterial` does **not** load chunks.
* `UnloadChunk` refuses modified chunks unless forced, because their state would have to be
  recovered by snapshot or replay.
* Chunks are stored in `std::map<ChunkCoord, ...>`: every iteration is in canonical `(z, y, x)`
  order, so hashing and serialization never depend on load order.
* Chunks belong to exactly one `TerrainWorld` and are never shared across threads. Workers get
  `ChunkMeshInput` snapshots.

## Future representations

The interfaces that other representations must satisfy to be comparable:

1. `Material SampleMaterial(VoxelCoord)` for a loaded or unloaded location.
2. `ApplyOperation` with identical acceptance rules and identical resulting `SampleMaterial` over
   the affected box (the oracle in `TestWorld.cpp` and the boundary benchmark already check this).
3. `ComputeStateHash` producing the same value as the dense representation for the same world
   (canonical serialization of content, not of the data structure).
4. Dirty-region and version semantics.

Candidates to evaluate against the baseline: sparse voxel bricks, run-length columns, sparse SDF
bricks, octrees, hybrid solid/surface. Comparisons must use the benchmark harness with identical
workloads and must pass the same correctness checks.
