# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Changed

* The project is described as game agnostic throughout: no genre, setting or game mode is assumed
  (README, plugin description, roadmap, module plans).
* Incremental remeshing: `ChunkFaces`, `BuildChunkFaces`, `UpdateChunkFaces` and `EmitChunkMesh` keep
  a chunk's visible faces per (y, z) row and rescan only the dirty region expanded by one voxel,
  producing exactly `BuildChunkMesh`'s output. The benchmark flush path uses it, and the crater
  workloads mesh their chunks (untimed) before the crater, as a renderer would (#3).
* `TerrainWorld::BuildMeshInput` copies the border from loaded neighbour chunks and generates it per
  column (one `SurfaceHeight` per column) for unloaded ones, instead of calling `SampleMaterial` per
  border voxel. Output bytes are unchanged; dirty-chunk flush is about 2-3x faster in
  `isolated_crater` and `boundary_crater` (#4).

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
* `TerrainWorld::CompactJournal(keepLast)` and `JournalBase()`: drop old journal entries so a world
  is reconstructed from a checkpoint snapshot plus the journal suffix. Version, last sequence, state
  hash and duplicate detection are preserved. Snapshot and journal schema 2 (schema 1 still loads);
  new `ResultCode::JournalCompacted`; benchmark E reports compaction time and sizes (#5).
* Benchmark G, `resolution`: one physical scene at voxel sizes 25 cm / 2^k (generation, meshing,
  apply and flush time; voxel, face and mesh memory per level). Roadmap goal: resolution.
* Greedy meshing: `EmitGreedyChunkMesh` merges coplanar faces of one direction and material from a
  chunk's `ChunkFaces` into maximal rectangles, covering exactly the reference mesher's surface.
  `AOBTerrainActor::bGreedyMeshing` (default true) uses it. In the `resolution` sweep at 3.125 cm it
  cuts 841 k faces to 80 k quads and mesh memory from 104 MB to 9.9 MB, emitted in 21 ms.
  `ob_snapshot --greedy --edges` draws the greedy quads with outlines (CI: `ob_snapshot_greedy_smoke`).
* Uniform chunks: a chunk whose voxels all hold one material is stored without a voxel array
  (`Chunk::IsUniform`, `UniformMaterial`, `Compact`); `Data()` is `nullptr` while uniform. Content,
  state hashes and serialized bytes are unchanged. Voxel memory in the `resolution` sweep drops from
  67 MB to 24 MB at 3.125 cm and from 4.7 MB to 2.4 MB in `generation`.
* `ob_snapshot` (`Tools/Snapshot`): headless PNG render of a cratered diorama through the core
  mesher with renderer-style dirty flushes; fails (exit 3) and draws chunks red if any cached mesh
  is stale. Run in CI as `ob_snapshot_smoke`.
* `Tools/UEHost` also walks a character into craters (one on a chunk seam) and checks it ends standing
  on the crater floor; recorded in `Docs/status.md` (#2).
* `Tools/UEHost`: `AOBResolutionGameMode` measures load time, memory, triangles, frame and GPU time and
  crater remesh latency of the `resolution` scene at 25, 12.5 and 6.25 cm, greedy on or off
  (`-OBVoxelCm=`, `-OBGreedy=`); results in `Docs/status.md`.
* Off-game-thread remeshing: `ob::RemeshScheduler` keeps one meshing job in flight per chunk, tickets
  every job and accepts a result only from the chunk's current job, so results from before a reset or
  `EndPlay` are dropped and a chunk changed mid-job is meshed again. `AOBTerrainActor` snapshots on the
  game thread, meshes on `UE::Tasks` workers and publishes on the game thread (`bAsyncRemesh`, default
  true; false keeps the synchronous reference path). Meshing leaves the crater frame and loads are
  10-25 % faster; the remaining crater hitch is synchronous collision cooking (`Docs/status.md`).
  `AOBResolutionGameMode` takes `-OBAsync=` and `-OBCollision=`, reports the crater's game-thread time
  per phase and runs at 3.125 cm.
* `FOBTerrainWorldSettings` exposes `NoiseScaleLog2`, `NoiseOctaves`, `TopsoilDepth` and `SoilDepth`, so
  the same physical terrain can be generated at any power-of-two refinement of the voxel size.

### Fixed

* First in-engine build (UE 5.8.2): the `.generated.h` include in `OBTerrainTypes.h` is last again
  (UHT); the core export macro supplies `DLLEXPORT`/`DLLIMPORT`; `bFlipWinding` defaults to true
  (the terrain rendered inside out) (#2).

* A modification reaching a chunk face now marks the loaded face neighbour dirty (with the facing
  border slab in its dirty region), and force-unloading a modified chunk does the same for its
  neighbours. Previously only chunks whose own voxels changed were remeshed, leaving stale boundary
  faces (holes) in neighbours. `AOBTerrainActor` no longer skips a dirty chunk whose version is
  unchanged.
