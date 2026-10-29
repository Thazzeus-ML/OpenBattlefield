# OpenBattlefield

**An open-source, experimental framework for large-scale destructible-world simulation in Unreal
Engine 5, built as a reusable plugin around an engine-independent, deterministic simulation core.**

The framework is game agnostic. It assumes no genre, setting or game mode: it provides reusable
systems (deterministic terrain, persistent deformation, construction, structural destruction,
replication) for any game or simulation that needs a large, shared world that hundreds of
participants can change.

Research hypothesis guiding the work:

> Computational cost can be made increasingly dependent on relevant simulation activity rather than
> total world complexity.

That is a hypothesis to be tested with reproducible benchmarks, not a claim.

## What exists today

| Part | State |
|---|---|
| `Source/OpenBattlefieldCore` — deterministic terrain core (coordinates, chunks, procedural foundation, validated operations, journal, versions, dirty regions, serialization, hashing, mesher) | **Implemented and verified** standalone: 50 tests, sanitizers, GCC/Clang/C++17/C++20, identical state hashes across compilers |
| `Benchmarks/` — harness with six workloads, JSON output, comparison tool | **Implemented**; sample measurements in `Docs/benchmarking.md` |
| `.github/workflows/core-ci.yml` — Linux/macOS/Windows matrix | **Implemented** |
| `Source/OBTerrain` + `OpenBattlefield.uplugin` — Unreal integration (world subsystem, terrain actor with ProceduralMesh render + collision) | **Written, not yet compiled in an engine** (no engine was available where this was built). See `Docs/status.md`. |
| Destruction, construction, networking, physics, rendering, scheduling, persistence modules | **Documented boundaries only** (`Docs/future-modules.md`) |

## Quick start (no Unreal required)

```bash
git clone https://github.com/Thazzeus-ML/OpenBattlefield.git
cd OpenBattlefield
cmake --preset release
cmake --build --preset release
ctest --preset release
./build/release/Benchmarks/Core/ob_bench --config Benchmarks/workloads/default.cfg --out results.json
```

As an Unreal plugin: clone into `YourProject/Plugins/OpenBattlefield`, enable it, place an
`AOBTerrainActor` in a level and call `ApplyCrater`. Please file an "Engine build report" issue with
the result; this path is unverified.

## Design in one paragraph

World state is `procedural foundation + validated modifications + active simulation state`. The
foundation is a pure function of a seed and a generation version, computed with integer arithmetic
only. Modifications are integer-space `TerrainOperation` commands with ids and sequence numbers,
validated by the server, applied in order, and journaled. Chunks are dense material grids that
version themselves and track dirty regions. Rendering and collision are derived from immutable,
versioned snapshots and published atomically per chunk. Replaying the journal onto a fresh world
reproduces the state hash exactly; this is tested, benchmarked and checked across compilers in CI.

## Documentation

`Docs/architecture.md` · `Docs/shared-source.md` · `Docs/coordinates.md` · `Docs/terrain.md` ·
`Docs/terrain-operations.md` · `Docs/determinism.md` · `Docs/concurrency.md` ·
`Docs/rendering-collision.md` · `Docs/networking.md` · `Docs/benchmarking.md` ·
`Docs/future-modules.md` · `Docs/roadmap.md` · `Docs/status.md` · `Docs/building.md`

## Contributing

See `CONTRIBUTING.md`. Discussion also happens on Overflow:
https://overflow.nitjsefni.eu/repositories/new

## License

Apache License 2.0 for all original project code (`LICENSE`). Unreal Engine itself is licensed
separately by Epic Games and is not distributed here.
