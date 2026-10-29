# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added

* Engine-agnostic simulation core (`OpenBattlefieldCore`): integer coordinates with floor division
  and checked arithmetic, stable chunk keys, `WorldConfig`, deterministic integer value-noise
  generator (generation version 1), dense material chunks with versions and dirty regions,
  validated/ordered/journaled sphere operations (schema version 1), canonical serialization for
  chunks, journals and snapshots, FNV-1a state hashing, immutable mesh snapshots and a face-culled
  reference mesher, stats and an instrumentation hook.
* Dependency-free test suite (50 cases) covering coordinates, serialization, generator determinism,
  operation validation, oracle-checked modification, chunk-boundary consistency, replay and snapshot
  equivalence, corruption rejection and mesh winding.
* Benchmark harness `ob_bench` with workloads A–F, presets, JSON output, allocation and memory
  metrics, and `Tools/bench_compare.py`.
* CMake presets, CI matrix (GCC, Clang, MSVC, Apple Clang, C++17/20, ASan/UBSan, cross-compiler hash
  check, clang-format).
* Unreal plugin shell: `.uplugin`, `OpenBattlefieldCore` and `OBTerrain` modules with a terrain world
  subsystem and a reference terrain actor (not yet compiled in-engine, see `Docs/status.md`).
* Documentation set under `Docs/`, contribution framework, issue and PR templates.

### Changed

* `TerrainWorld::BuildMeshInput` copies the border from loaded neighbour chunks and generates it per
  column (one `SurfaceHeight` per column) for unloaded ones, instead of calling `SampleMaterial` per
  border voxel. Output bytes are unchanged; dirty-chunk flush is about 2-3x faster in
  `isolated_crater` and `boundary_crater` (#4).

### Fixed

* A modification reaching a chunk face now marks the loaded face neighbour dirty (with the facing
  border slab in its dirty region), and force-unloading a modified chunk does the same for its
  neighbours. Previously only chunks whose own voxels changed were remeshed, leaving stale boundary
  faces (holes) in neighbours. `AOBTerrainActor` no longer skips a dirty chunk whose version is
  unchanged.
