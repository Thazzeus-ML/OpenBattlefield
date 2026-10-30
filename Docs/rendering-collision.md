# Rendering and collision contract

Rendering and collision are **derived** from authoritative terrain. They never decide terrain state.

## Current implementation

* Core: `BuildChunkMesh` produces a face-culled cube mesh from a `ChunkMeshInput` snapshot. Faces on
  the chunk boundary are resolved using the snapshot's one-voxel border, so neighbouring chunks never
  show seams or duplicate faces.
* Core: `EmitGreedyChunkMesh` turns a chunk's `ChunkFaces` into a greedy mesh: coplanar faces with
  the same direction and material merge into maximal rectangles. It covers exactly the unit faces of
  `BuildChunkMesh` with the same normals, materials and winding (`TestMesher.cpp` cuts every quad
  back into unit faces and compares), with about 10x fewer quads on generated terrain. Merged quads
  meet smaller ones at T-junctions, which a rasterizer can show as single-pixel cracks; none were
  visible in the UE 5.8 host at 25 cm.
* Adapter: `AOBTerrainActor::bGreedyMeshing` (default true) selects the greedy mesh; false emits one
  quad per voxel face through `BuildChunkMesh`.
* Adapter: `AOBTerrainActor` keeps one `UProceduralMeshComponent` per visible chunk and calls
  `CreateMeshSection_LinearColor(..., bCreateCollision)`. Render geometry and collision geometry are
  the same section, published in the same call.

### Winding

The core emits quads so that `cross(v1 - v0, v2 - v0)` equals the outward normal (counter-clockwise
viewed from outside in a right-handed frame); `TestMesher.cpp` verifies this for every triangle.
Whether Unreal treats that as front-facing depends on its left-handed convention; `bFlipWinding` on
the actor flips the index order. Because the adapter has not been run in-engine yet, the correct
default has not been confirmed (see `status.md`).

### Materials

Each vertex carries the material id in the red channel of the vertex colour (`id / 255`). A terrain
material graph can branch on it. UVs are planar per face in voxel units.

## Collision consistency policy

The policy must be decided before any asynchronous collision update is attempted. The policy is:

1. **Atomic per chunk.** A chunk's render and collision geometry are always published together from
   the same snapshot version. A player can never see a crater whose collision still blocks them in
   that chunk, or walk into a crater that is not yet visible.
2. **Conservative while pending.** Until a dirty chunk is rebuilt, it keeps its previous geometry for
   both rendering and collision. Old solid stays solid (players cannot fall through terrain that
   looks solid); a removed obstruction stays visible until the rebuild. `MaxChunkRebuildsPerTick`
   bounds the snapshots started per frame and meshing runs on worker tasks, so pending state is
   visible as "the crater appears a frame or two later", never as a render/collision mismatch.
3. **Server collision is authoritative.** Gameplay collision decisions happen on the server against
   its own published geometry. Client geometry is for presentation and prediction.
4. **Versioned.** Published geometry records the chunk version it came from, and only the result of
   a chunk's current meshing job is published; stale results are dropped (`ob::RemeshScheduler`, see
   `concurrency.md`).

Gaps in the current implementation, stated plainly:

* Collision uses the render mesh. Collision generation independent of render detail (coarser
  collision, finer render) is not implemented.
* Incremental collision reconstruction is not implemented; the whole chunk section is recooked
  synchronously on the game thread when it is published (meshing itself runs on a worker).
  `bUseAsyncCooking` is deliberately off so the policy above holds.
* In-engine measurements are in `status.md` (resolution in engine).

## Future work

* Dirty-region-limited remeshing in the adapter (the core's `UpdateChunkFaces` already does it), and
  a greedy mesh updated per dirty region instead of re-emitted per chunk.
* Smooth surfaces (marching cubes / dual contouring) once a density layer exists.
* LODs per chunk.
* GPU-generated geometry: plugin-local shaders will be added only when there is a working compute
  implementation to register; no shader module is configured today.
