# Determinism and state identity

## Guarantee

Given the same `WorldConfig` (seed, generation version, chunk size, bounds, generation parameters)
and the same ordered list of accepted `TerrainOperation`s, `TerrainWorld` reconstructs identical
authoritative terrain: identical `SampleMaterial` everywhere, identical chunk versions, identical
`ComputeStateHash()`.

This holds across:

* load order of chunks (`ReplayFromSeedAndJournalReproducesStateHash`, replay 2);
* chunk size (`ChunkSizeDoesNotChangeResult`, benchmark C): the same operations on 16³ and 32³
  chunks give identical voxels;
* compilers: the default benchmark set produces identical hashes under GCC 13 and Clang 18
  (`benchmarking.md`);
* snapshot round trip and journal round trip (`TestReplay.cpp`).

It is achieved by using only integer arithmetic in generation and modification, floor division for
negative coordinates, canonical container ordering, and canonical serialization for hashing.

## What is hashed

`ComputeStateHash()` = FNV-1a 64 over:

1. the canonical serialization of `WorldConfig`;
2. the number of modified chunks;
3. for each modified chunk in canonical `(z, y, x)` order: coordinate, size, all materials, version.

Unmodified chunks are excluded (they are a pure function of the config), so the hash does not
depend on which chunks are loaded. `ComputeLoadedChunkHash()` includes everything loaded and exists
for tests. `ComputeJournalHash()` identifies the command history.

Struct memory is never hashed: padding and endianness would make the hash platform dependent.

## Pinned values

`TestGenerator.cpp` pins the content hash of one generated chunk for generation version 1. If the
generator changes in any way:

1. bump `ProceduralGenerator::kGenerationVersion` and the default `WorldConfig::generationVersion`;
2. update the pinned hash in the same commit;
3. note it in `CHANGELOG.md`.

Saved journals from an older generation version are then correctly rejected by config comparison
instead of silently producing a different world.

## Out of scope

Derived data (meshes, collision, rendering) uses floating point and is not required to be bitwise
identical across platforms. It must be a deterministic function of the snapshot on a given platform,
which `MesherIsDeterministicAndRejectsBadInput` checks.
