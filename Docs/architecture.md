# Architecture

OpenBattlefield is a hybrid: an independently testable simulation core, thin Unreal Engine
integration layers, and external benchmark/research tooling.

```text
Applications / Reference Game        (future: sample projects built on the plugin)
              |
        UE5 Integration              Source/OBTerrain  (UWorldSubsystem, AActor, ProceduralMesh)
              |
      Framework Subsystems           future: OBDestruction, OBConstruction, OBNetworking, ...
              |
      Simulation Core                Source/OpenBattlefieldCore  (no Unreal dependency)
```

## Dependency rules

* `OpenBattlefieldCore` depends on nothing but the C++17 standard library. It never includes an
  Unreal header, never uses `UObject`, world actors, rendering types or the editor. This is enforced
  by compiling it standalone with CMake in CI (`-fno-exceptions -fno-rtti -Werror`).
* Adapters (`OBTerrain`) depend on the core. The core never depends on an adapter.
* Tests and benchmarks depend only on the core. They are what makes the project's performance
  claims checkable without an engine installation.

## Core responsibilities (implemented)

| Area | Types | Notes |
|---|---|---|
| Coordinates | `VoxelCoord`, `ChunkCoord`, `LocalCoord`, `VoxelBox` | Integer; floor division for negatives; checked arithmetic |
| Chunk identity | `TryPackChunkKey` | 21 bits per axis, stable 64-bit key |
| Configuration | `WorldConfig` | Seed, generation version, chunk size, bounds, limits |
| Procedural foundation | `ProceduralGenerator` | Integer value noise heightfield, layered materials, bedrock |
| Terrain data | `Chunk` | Dense `uint8` material grid (none while uniform), solid count, version, dirty region |
| Modification | `TerrainOperation`, `ValidateOperation`, `TerrainWorld::ApplyOperation` | Validated, ordered, journaled |
| Dirty tracking | `DirtyRegion`, `TerrainWorld::TakeDirtyChunks` | Per-chunk inclusive AABB + world-level sorted set |
| Versioning | `Chunk::Version`, `TerrainWorld::Version` | Content versions for publication checks |
| Serialization | `ByteWriter`, `ByteReader`, snapshot and journal formats | Canonical little-endian, bounds checked, no exceptions |
| Hashing | `Hasher`, `ComputeStateHash` | FNV-1a over canonical bytes, canonical chunk order |
| Derived data | `ChunkMeshInput`, `BuildChunkMesh` | Immutable padded snapshot, face-culled CPU mesh |
| Instrumentation | `WorldStats`, `InstrumentationSink` | Counters and an event hook |
| Errors | `ResultCode`, `ToString` | Every fallible call returns a code |

## World data model

```text
World State = Procedural Foundation (WorldConfig: seed + generation version + rules)
            + Validated World Modifications (ordered journal of TerrainOperation)
            + Active Simulation State (not part of the terrain core)
```

The loaded chunk cache is a derived artifact: every loaded, unmodified chunk is a pure function of
`WorldConfig`, and every modified chunk is a pure function of `WorldConfig` plus the journal prefix
that touched it. This is why `ComputeStateHash` hashes only modified chunks: the result is independent
of which chunks happen to be loaded (verified by `StateHashIsIndependentOfLoadedUnmodifiedChunks`).

## Module layout

```text
OpenBattlefield.uplugin
Source/OpenBattlefieldCore/      Unreal module AND standalone library (same files)
Source/OBTerrain/                Unreal runtime integration
Tests/Core/                      Standalone tests (CTest)
Benchmarks/Core/                 Benchmark harness
Benchmarks/workloads/            Workload configurations
Tools/                           bench_compare.py
Docs/                            This documentation
.github/workflows/               CI
```

The repository root is the plugin root. Clone or symlink it into `YourProject/Plugins/OpenBattlefield`.

See `shared-source.md` for how one set of sources serves both builds, `status.md` for what has
actually been compiled and verified, and `future-modules.md` for planned boundaries.
