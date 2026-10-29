# Roadmap

## Research hypothesis

> Computational cost can be made increasingly dependent on relevant simulation activity rather than
> total world complexity.

This is a hypothesis, not a result. Each milestone below produces measurements that either support
it or not, using the benchmark harness and the correctness checks that already exist.

## Milestone 0 — Foundation (this repository state)

Done, verified standalone:

* Deterministic integer core: coordinates, chunks, generator, operations, journal, dirty regions,
  versions, serialization, hashing, instrumentation.
* Test suite (7 executables, 50 test cases) under GCC, Clang, C++17/C++20, ASan/UBSan.
* Benchmark harness with workloads A–F and a comparison tool.
* CI for the standalone build.

Built and run on UE 5.8.2 (Windows), see `status.md`:

* Unreal plugin shell: `OpenBattlefieldCore` and `OBTerrain` modules, terrain subsystem and actor.

## Milestone 1 — In-engine vertical slice

* Compile and run the plugin in UE 5.4+; fix winding and any API drift; add a sample map.
* Console command / Blueprint node for craters; confirm render/collision atomicity.
* Record the first in-engine frame-time measurements for chunk rebuilds.
* Exit criterion: a crater applied in PIE at 60 FPS with collision correct, and a document of what
  the engine build needed changed.

## Milestone 2 — Derived-data cost proportional to change

* Dirty-region-limited remeshing, greedy meshing, border cache for `BuildMeshInput`.
* Off-game-thread remeshing with version-checked publication (`OBScheduling` seed).
* Target: flush cost in the `artillery` workload (repeated overlapping impacts) scales with changed
  voxels, not with chunk count. Measured against
  the Milestone 0 baseline on the same machine.

## Milestone 3 — Persistence and replication

* Journal compaction (snapshot + suffix) with hash-verified equivalence.
* Server-authoritative client requests, interest-managed event replication, late join.
* Measure bytes per second per client and divergence recovery time under packet loss.

## Milestone 4 — Representation experiments

* Sparse chunk storage, density/SDF layer for smooth surfaces, per-representation benchmarks with
  identical workloads and shared correctness criteria.

## Goal — Resolution: as fine as the budgets allow

Push voxel size below the 25 cm baseline as far as memory, meshing and frame-time budgets allow,
measured on the same physical scene at every level (benchmark G, `resolution`). Baseline on one
machine: from 25 cm to 3.125 cm, voxel memory and generation grow 8x per halving (dense chunks) and
faces 4x (surface), while edits stay in the millisecond range. Next steps, each measured against
that baseline:

* ~~Uniform chunks (all air / all one material) stored without a voxel array.~~ Done: voxel memory
  at 3.125 cm drops from 67 MB to 24 MB (64 % of chunks are uniform).
* ~~Greedy meshing to cut faces and mesh memory per area.~~ Done: `EmitGreedyChunkMesh`; mesh memory
  at 3.125 cm drops from 104 MB to 9.9 MB (841 k faces to 80 k quads), emitted in 21 ms.
* ~~In-engine frame time and memory at 25, 12.5 and 6.25 cm in `Tools/UEHost`.~~ Done
  (`AOBResolutionGameMode`, results in `status.md`): 6.25 cm over 32 m loads in about 1.3 s and
  renders at 6-8 ms per frame with greedy meshes. A crater is a 15-45 ms single-frame hitch at every
  resolution because the actor remeshes synchronously.
* Next: remesh craters off the game thread with version-checked publication (Milestone 2), then
  repeat the in-engine sweep, adding 3.125 cm.

## Milestone 5 — Large-session scale

* 256-client load generator with realistic movement, interaction, construction and destruction
  traffic.
* Server tick percentiles under that load. Idle connections do not count.

## Non-goals for now

Forking the engine, a custom task scheduler before measurements justify it, shader modules without
working shaders, and building a specific game before the reusable systems exist.
