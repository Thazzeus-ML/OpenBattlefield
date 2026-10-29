# Concurrency and threading contract

## Current state: synchronous reference path

Everything in the first milestone runs on one thread. `TerrainWorld` is single-owner: exactly one
thread mutates it at a time, and in the Unreal adapter that thread is the game thread. The benchmark
harness measures this path so that later parallelism has a baseline to beat.

## Authoritative state

* Modifications are applied in sequence-number order by the owning thread. There is no concurrent
  write path, so conflicting writes to one region cannot occur.
* Rejected operations never touch state (`RejectedOperationsLeaveWorldUntouched`).

## Worker jobs (design, partially implemented)

Expensive derived work is meant to run elsewhere: geometry generation, dirty-region processing,
compression, serialization, non-critical spatial queries. The core already provides the input
side of that contract:

* `TerrainWorld::BuildMeshInput(chunk)` returns an **immutable snapshot** (`ChunkMeshInput`): the
  chunk's materials plus a one-voxel border, and the chunk version it was taken from. It owns its
  memory and can be moved to any thread.
* `BuildChunkMesh(input, out)` is a pure function of the snapshot and is safe on any thread.

## Safe publication

Results are published with a **version check**: a publisher compares `ChunkMeshInput::version`
against the live `Chunk::Version()` (or against the version it last published) and discards results
that are older than what is already published. `AOBTerrainActor::RebuildChunk` records the snapshot
version it published, never the current chunk version, so a result computed against version N is
never mistaken for version N+1. Unreal APIs that must run on the game thread
(`CreateMeshSection_LinearColor`) are called only there.

Planned refinement (not implemented): worker results are placed in a completion queue with
`(chunk, version)`; the game thread publishes the newest per chunk and drops the rest.

## Cancellation and shutdown

| Situation | Behaviour today | Rule for async work |
|---|---|---|
| Chunk unloads during processing | n/a (synchronous) | Result for an unloaded chunk is dropped at publication time (lookup by coordinate fails) |
| Modification invalidates an in-flight job | n/a | Stale version detected at publication; chunk is re-queued because it is dirty again |
| World destroyed | `AOBTerrainActor::EndPlay` clears queues, then the subsystem destroys the core | Workers must hold snapshots, never pointers into the world; publication after destruction is a no-op |
| Game shutdown | `UOBTerrainWorldSubsystem::Deinitialize` | Same |
| Job fails | Mesher returns 0 faces for malformed input | Record a failure counter; keep previous published geometry (conservative) |

## Rules

1. No worker ever holds a `Chunk*` or `TerrainWorld*`.
2. Snapshots carry their version; publication is version checked.
3. Publication of Unreal state happens on the game thread.
4. Measure the synchronous path first (done: `benchmarking.md`), then introduce bounded parallelism
   where the data shows it matters. The data currently says: mesh rebuilds dominate operation cost by
   two to three orders of magnitude, so that is where parallelism and incremental rebuilds go first.
