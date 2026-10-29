# Coordinates and units

## Spaces

| Space | Type | Units | Notes |
|---|---|---|---|
| Voxel space | `ob::VoxelCoord` (`int64` x, y, z) | 1 = one voxel | Authoritative. All operations are expressed here. |
| Chunk space | `ob::ChunkCoord` (`int32` x, y, z) | 1 = one chunk | `chunk = floor(voxel / chunkSize)` per axis |
| Local space | `ob::LocalCoord` (`uint32` x, y, z) | voxels within a chunk | `[0, chunkSize)`; `local = voxel - chunk * chunkSize` |
| Unreal space | `FVector` | centimetres | Relative to the terrain actor transform |

Axes: X and Y horizontal, Z up, in every space. Voxel `(0,0,0)` occupies `[0, voxelSizeCm)^3`
relative to the terrain origin. Neither space is handed differently; the adapter only scales.

## Conversions

```text
chunk        = FloorDiv(voxel, chunkSize)                 (rounds toward -inf, see ObCoordinates.h)
local        = FloorMod(voxel, chunkSize)                  (always in [0, chunkSize))
voxel        = chunk * chunkSize + local
unreal (cm)  = voxel * voxelSizeCm                          (min corner of the voxel)
voxel        = floor(unreal_cm / voxelSizeCm)               (FOBCoordinates::ToVoxel)
radius_vox   = max(1, round(radius_cm / voxelSizeCm))
```

`voxelSizeMm` is stored in the config (default 250 mm = 25 cm) so that the physical scale is part of
the hashed world identity even though the core never uses it in arithmetic.

## Ranges and overflow

* Chunk coordinates are limited to `[-2^20 + 1, 2^20 - 1]` per axis (`kMaxAbsChunkCoord`). A chunk
  coordinate packs into a stable 64-bit key (`TryPackChunkKey`, 21 bits per axis) suitable for
  persistent ids and network addressing.
* Chunk edge length is a power of two in `[8, 128]` voxels.
* With the maximum chunk size the voxel range is about `±1.3 x 10^8`, far from `int64` limits.
  Operation validation nevertheless uses checked addition/subtraction/multiplication
  (`CheckedAdd` etc.) and tests prove extreme inputs are rejected, not wrapped
  (`ExtremeCoordinatesNeverOverflow`).
* An operation's centre must lie inside the world bounds. The sphere is clipped to the bounds; the
  part outside is ignored (`ComputeAffectedBox`).

## Why integers

Floating-point arithmetic is not bitwise deterministic across compilers, platforms, and FMA
settings. Authoritative operations therefore carry integer voxel centres and radii, and the
containment test is `dx*dx + dy*dy + dz*dz <= r*r` in `int64`. Floating point appears only in derived
data (mesh positions), which is never hashed or replayed.

## Large worlds and Unreal

Unreal 5 large world coordinates use `double` for `FVector`. The adapter converts with `double`
division and `floor`, which is exact for the ranges above. If a project needs a terrain origin far
from the Unreal origin, the terrain actor transform is applied first (`InverseTransformPosition`), so
the core always sees coordinates relative to the terrain origin.
