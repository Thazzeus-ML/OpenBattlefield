# Terrain operation schema

```text
TerrainOperation (schemaVersion 1, 56 bytes serialized)
    operationId      u64   issuer-chosen, globally unique, non-zero
    schemaVersion    u16   must equal 1
    type             u8    1 = SubtractSphere, 2 = AddSphere
    worldId          u64   must equal WorldConfig::worldId
    sequenceNumber   u64   strictly increasing per world; defines application order
    center           3x i64 voxel coordinates
    radius           u32   voxels, 1..WorldConfig::maxOperationRadius
    material         u8    AddSphere: destructible solid material; SubtractSphere: Air
```

## Design decisions

* **No affected-chunk set in the schema.** It is derived on the server from `center` and `radius`
  (`ComputeAffectedChunks`) and returned in `OperationResult`. Client-supplied chunk lists, costs or
  outcomes are never trusted.
* **Integer geometry.** See `coordinates.md`. The containment test is inclusive:
  `dx² + dy² + dz² <= r²`.
* **Operation id and sequence number are separate.** The id identifies the command (for duplicate
  suppression across retransmits); the sequence number orders commands. A retransmitted packet
  carries the same id *and* sequence and is rejected as `StaleSequence`; a reused id with a fresh
  sequence is rejected as `DuplicateOperation`.
* **Gaps are allowed in sequence numbers.** Only monotonicity is required, so a server can drop
  rejected commands without renumbering.

## Validation (`ValidateOperation`, stateless)

| Check | Code |
|---|---|
| schema version | `OperationSchemaVersionMismatch` |
| id non-zero | `InvalidOperationId` |
| known type | `InvalidOperationType` |
| world id | `WorldIdMismatch` |
| radius 0 | `InvalidRadius` |
| radius > max | `RadiusTooLarge` |
| material consistent with type, never Bedrock | `InvalidMaterial` |
| centre inside world voxel bounds | `CenterOutOfBounds` |
| centre ± radius and r² representable | `ArithmeticOverflow` |

Stateful checks in `TerrainWorld::ApplyOperation`: `sequence > lastSequence` else `StaleSequence`;
id not among the last 4096 accepted ids else `DuplicateOperation`.

## Semantics

* `SubtractSphere`: every voxel inside the sphere that is solid and destructible becomes `Air`.
  `Bedrock` is untouched.
* `AddSphere`: every `Air` voxel inside the sphere becomes `material`. Existing solids are untouched.
* Chunks are processed in canonical sorted order; voxels within a chunk in `(z, y, x)` order. The
  result does not depend on this order (voxel updates are independent), but the order is fixed so
  instrumentation and future partial-application policies are deterministic.
* An accepted operation is appended to the journal and advances `TerrainWorld::Version()` even if
  it changed no voxel (for example a crater in open air). Chunk versions advance only when chunk
  content changed. Replaying the journal therefore reproduces versions as well as content.

## Replay and compatibility

* A journal replays onto a fresh world with the same `WorldConfig` and reproduces the state hash
  (`TestReplay.cpp`, benchmark E).
* `generationVersion` is part of `WorldConfig`; a journal saved against one foundation cannot be
  mistakenly replayed on another because the config is hashed and serialized with it.
* `schemaVersion` on each operation allows a future reader to migrate old operations explicitly.
  Current readers reject unknown versions rather than guessing.

## Limits

Defaults: `maxOperationRadius = 64` voxels (16 m at 25 cm voxels) per world; hard cap 4096. The
cost of an operation is O(radius³) voxel visits, so servers should set this to the largest radius
gameplay actually needs. Measured costs are in `benchmarking.md`.
