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
* Target: artillery flush cost scales with changed voxels, not with chunk count. Measured against
  the Milestone 0 baseline on the same machine.

## Milestone 3 — Persistence and replication

* Journal compaction (snapshot + suffix) with hash-verified equivalence.
* Server-authoritative client requests, interest-managed event replication, late join.
* Measure bytes per second per client and divergence recovery time under packet loss.

## Milestone 4 — Representation experiments

* Sparse chunk storage, density/SDF layer for smooth surfaces, per-representation benchmarks with
  identical workloads and shared correctness criteria.

## Milestone 5 — Combined arms scale

* 256-client load generator with realistic movement/combat/construction/destruction traffic.
* Server tick percentiles under that load. Idle connections do not count.

## Non-goals for now

Forking the engine, a custom task scheduler before measurements justify it, shader modules without
working shaders, and building the full game before the reusable systems exist.
