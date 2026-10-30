# Concurrency and threading contract

## Current state

`TerrainWorld` is single-owner: exactly one thread mutates it at a time, and in the Unreal adapter that
thread is the game thread. Chunk meshing is the one piece of derived work that runs elsewhere:
`AOBTerrainActor` (with `bAsyncRemesh`, the default) snapshots a changed chunk on the game thread,
meshes the snapshot on a `UE::Tasks` worker and publishes the result on the game thread. With
`bAsyncRemesh` off everything runs on the game thread (the synchronous reference path); the benchmark
harness measures that path so that parallelism has a baseline to beat.

## Authoritative state

* Modifications are applied in sequence-number order by the owning thread. There is no concurrent
  write path, so conflicting writes to one region cannot occur.
* Rejected operations never touch state (`RejectedOperationsLeaveWorldUntouched`).

## Worker jobs

Expensive derived work is meant to run elsewhere: geometry generation, dirty-region processing,
compression, serialization, non-critical spatial queries. The core already provides the input
side of that contract:

* `TerrainWorld::BuildMeshInput(chunk)` returns an **immutable snapshot** (`ChunkMeshInput`): the
  chunk's materials plus a one-voxel border, and the chunk version it was taken from. It owns its
  memory and can be moved to any thread.
* `BuildChunkMesh(input, out)` is a pure function of the snapshot and is safe on any thread.

## Safe publication

`ob::RemeshScheduler` (core, no threads, tested standalone) decides what may be published:

* At most one job per chunk is in flight. A chunk that changes while its job runs is queued again
  when the job completes, so its newest state is always meshed eventually.
* Every job carries a **ticket** that grows for the scheduler's lifetime. A result is published only
  if its ticket is the chunk's in-flight ticket; `Reset()` (on `EndPlay`) drops every in-flight job.

A chunk's version alone cannot order results: a neighbour's change dirties this chunk's boundary
faces without bumping its version, so two snapshots of one version can differ. Tickets order them.

Workers push `(chunk, ticket, version, buffers)` into a completion queue (`TQueue`, MPSC) that they
hold by shared reference, never through the actor. The game thread drains it at the start of each
tick, asks the scheduler whether each result is current and publishes the ones that are. The
published version recorded per chunk is the snapshot's version, never the current chunk version, so a
result computed against version N is never mistaken for version N+1. Unreal APIs that must run on the
game thread (`CreateMeshSection_LinearColor`, collision cooking) are called only there.

## Cancellation and shutdown

| Situation | Behaviour today | Rule for async work |
|---|---|---|
| Chunk unloads during processing | n/a (the actor does not unload chunks yet) | Result for an unloaded chunk is dropped at publication time (lookup by coordinate fails) |
| Modification invalidates an in-flight job | The result is still published (it is newer than what is on screen); the chunk is queued again and remeshed | Same |
| World destroyed | `AOBTerrainActor::EndPlay` resets the scheduler and swaps in a fresh completion queue, then the subsystem destroys the core; running jobs finish into the orphaned queue | Workers hold snapshots, never pointers into the world; publication after destruction is a no-op |
| Game shutdown | `UOBTerrainWorldSubsystem::Deinitialize` | Same |
| Job fails | Mesher returns 0 faces for malformed input | Record a failure counter; keep previous published geometry (conservative) |

## Rules

1. No worker ever holds a `Chunk*` or `TerrainWorld*`.
2. Snapshots carry their version; publication is version checked.
3. Publication of Unreal state happens on the game thread.
4. Measure the synchronous path first (done: `benchmarking.md`), then introduce bounded parallelism
   where the data shows it matters. The data currently says: mesh rebuilds dominate operation cost by
   two to three orders of magnitude, so that is where parallelism and incremental rebuilds go first.
