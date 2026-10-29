# Networking (design, not implemented)

Full multiplayer replication is not part of the first milestone. This document fixes the interfaces
and protocol shape the core already supports so that the networking milestone builds on them rather
than around them.

## Server authority

The server validates everything that changes shared state. For terrain that is exactly
`TerrainWorld::ApplyOperation`: client requests are converted into `TerrainOperation`s whose
`operationId` and `sequenceNumber` are **issued by the server** (as `UOBTerrainWorldSubsystem` does
today), validated with the server's `WorldConfig` limits, and only then journaled.

Clients may predict visuals; they never alter shared state. `ApplyCraterAtLocation` already refuses
on `NM_Client`.

## Terrain replication model (versioned, event oriented)

A client eventually receives:

1. the `WorldConfig` (canonical serialization, 94 bytes) so it can regenerate the foundation;
2. baseline state for relevant chunks: either nothing (regenerate) or a chunk snapshot
   (`Chunk::Serialize`, RLE, typically 100 B to a few KB per modified chunk);
3. ordered accepted operations (`TerrainOperation::Serialize`, 56 bytes each);
4. version information: `TerrainWorld::Version()`, `LastSequence()`, per-chunk versions;
5. recovery data when it diverges: a chunk snapshot or a full `SerializeSnapshot`.

Measured in benchmark E with 500 operations: journal 28 KB versus snapshot 2.7 MB. Event replication
is cheaper by two orders of magnitude for a fresh world, but a long-lived world's journal grows
without bound while the snapshot is bounded by the modified area. The crossover point and the
compaction policy (snapshot + journal suffix) are open research questions; the data needed to study
them comes from the stress workload.

## Reliability and ordering

The core already provides the primitives:

| Problem | Core mechanism |
|---|---|
| Duplicate messages | `operationId` window → `DuplicateOperation`; same sequence → `StaleSequence` |
| Out-of-order delivery | `sequenceNumber` must increase; the transport must buffer and reorder before applying |
| Lost unreliable events | detected by sequence gaps vs. the server's advertised `LastSequence`; recovered by requesting the missing range or a chunk snapshot |
| Reconnection / late join | `SerializeSnapshot` (bounded) or config + journal (unbounded) |
| Missing chunk versions | per-chunk version in `ChunkMeshInput`/`Chunk`; client requests a chunk snapshot when its version lags |
| Server restart | snapshot restores journal, sequence state, duplicate window |
| Schema migration | `schemaVersion` per operation, per config, per chunk, per snapshot |

A retransmitted command is never applied twice: the id window and sequence check are exactly the
idempotency guard.

## Abuse prevention

The server must reject unreasonable parameters, and it does: radius bounds, centre bounds, material
rules, world id, schema. Still to add at the transport layer: per-client rate limits, payload size
limits, interest management (only replicate chunks near the client), snapshot budgets, and
prioritisation of events by relevance. Client-supplied affected-chunk lists, costs, collision
results and damage outcomes are never accepted; the schema has no field for them.

## Unreal replication strategy

Not decided. The candidates are Iris, the legacy replication graph, or RPC-based custom transport
of the byte formats above. The decision requires profiling with real gameplay workloads on the
supported engine version. Nothing in the core prefers one; all formats are plain byte buffers.
