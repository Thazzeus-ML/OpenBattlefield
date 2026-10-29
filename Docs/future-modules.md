# Future module boundaries

These modules are documented boundaries, not code. They are created only when there is a working
system to put in them, and each must come with tests and a benchmark workload.

| Module | Responsibility | Depends on | First deliverable that justifies creating it |
|---|---|---|---|
| `OBDestruction` | Structural destruction of placed objects: damage accumulation, support graphs, fracture scheduling, debris lifetime. Persistent outcomes become validated modifications. | Core (operations, journal), OBTerrain for terrain-coupled collapse | A deterministic support-graph solver for a modular wall with a standalone test and a benchmark |
| `OBConstruction` | Validated placement of modular structures and excavated terrain features (walls, ramps, ditches): placement rules, footprint queries against terrain, material costs. | Core | A placement validator that rejects overlapping/unsupported placements deterministically |
| `OBNetworking` | Server-authoritative transport of the byte formats in `networking.md`: request validation, interest management, rate limits, snapshot/journal delivery, divergence recovery. | Core, OBTerrain, chosen UE replication facility | Late-join of a client through snapshot + journal suffix with a measured bandwidth figure |
| `OBPhysics` | Adaptive-fidelity physics scheduling: which bodies simulate, at what rate, with what collision representation; ragdoll budgets. | OBTerrain collision versions | A scheduler that keeps 200 sleeping ragdolls under a measured CPU budget |
| `OBRendering` | Advanced terrain rendering: greedy/LOD meshes, GPU-generated geometry, damage-aware materials, plugin-local shaders. | OBTerrain snapshots | A greedy mesher with identical surface coverage to the reference mesher and a benchmark win |
| `OBScheduling` | Consequence-aware simulation scheduling: prioritising work by gameplay relevance, bounded worker pools, version-checked publication queues. | Core snapshots | A worker pool that remeshes dirty chunks off the game thread with the publication rules of `concurrency.md` |
| `OBPersistence` | Snapshot/journal storage, compaction, migration, world history reconstruction. | Core serialization | Journal compaction into snapshot + suffix with hash-verified equivalence |
| `OBEditor` | Editor-only tools: chunk visualization, dirty-region overlay, benchmark runner. Must never leak into runtime or server builds. | OBTerrain | The first debugging need that cannot be met with console commands |

Rules for every new module:

1. Core logic goes in an engine-agnostic library (the existing core or a sibling with the same
   shared-source arrangement) with standalone tests.
2. The Unreal module stays thin.
3. A benchmark workload and documented measurement accompany any performance claim.
4. Authority and determinism rules of the core apply unchanged.
