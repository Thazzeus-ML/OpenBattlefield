# Implementation status

Honest record of what has and has not been verified. Update this file in every PR that changes it.

## Verified in this repository's CI

| Component | Verification |
|---|---|
| `OpenBattlefieldCore` standalone build | GCC 13 and Clang 18 on Linux, MSVC on Windows, Apple Clang on macOS (CI matrix), C++17 and C++20, `-Werror` with an extensive warning set, `-fno-exceptions -fno-rtti` |
| Core test suite | 66 test cases across 8 executables, all passing; also under AddressSanitizer + UndefinedBehaviorSanitizer |
| Benchmark harness | `smoke` preset runs in CI; `default` preset run manually with the results in `benchmarking.md` |
| Cross-compiler determinism | Identical state hashes for every workload under GCC and Clang |
| Formatting | `clang-format` check in CI |

The MSVC preset builds with `/W4` but without `/WX` because MSVC warning output could not be checked where this was authored; making it warning-clean under `/WX` is an open task.

## Verified in Unreal Engine (UE 5.8.2, Windows 11, VS 2022, 2026-10-29)

Built as `Plugins/OpenBattlefield` in a minimal C++ host project and run in a packaged-style
`-game` session. Not yet repeated on 5.4–5.7, Linux or macOS.

| Component | Verification |
|---|---|
| `OpenBattlefieldCore` + `OBTerrain` modules, editor target (Development) | Builds; only C4251 ("needs dll-interface") warnings for STL members of exported classes |
| Game target, Shipping | Builds, no warnings |
| Server target | **Not built**: requires a source-built engine; the installed engine has no server target |
| `AOBTerrainActor` in an empty level | Generates a 5 x 5 x 5 chunk area (32-voxel chunks, 25 cm voxels); renders solid with `bFlipWinding = true` |
| `ApplyCrater` | Visible craters inside a chunk, on a chunk face and on a chunk corner; complex-collision line traces at each centre drop by 275 cm for a 300 cm crater (3/3) |
| Walking into a crater with a character | Default `ACharacter` capsule (AI-controlled) walks into two craters, one centred on a chunk face so it ends on the seam between two chunks' collision: both end on ground, 17–18 cm from the centre, feet 2 cm above the traced floor, never lower (2/2, three runs; repeated with off-game-thread remeshing, now the default: 3/3 craters, 2/2 walks) |

### Resolution in engine

`AOBResolutionGameMode` (`Tools/UEHost`) loads the core `resolution` scene (32 m across) at each
voxel size, with greedy meshing on and off. The first table was measured with synchronous remeshing (#19). Development editor binary in `-game`, 1600 x 900, VSync
off, one Windows 11 machine. Load is the second load of the session (the first warms the engine).
The terrain is meshed at least +/-4 m vertically. Frame time is per-process noisy by about +/-1 ms
(6.25 cm is three alternating pairs, shown as ranges), so only differences larger than that are read
as real.

| voxel | mesh | chunks | triangles | load | process memory | frame p50 | GPU p50 | crater remesh (one frame) |
|---|---|---|---|---|---|---|---|---|
| 25 cm | greedy | 45 | 17 k | 0.12 s | +1.7 MB | 4.8 ms | 2.4 ms | 19 ms |
| 25 cm | per face | 45 | 70 k | 0.25 s | +6.3 MB | 5.0 ms | 2.5 ms | 33 ms |
| 12.5 cm | greedy | 116 | 51 k | 0.38 s | +6.8 MB | 5.3 ms | 2.6 ms | 15 ms |
| 12.5 cm | per face | 116 | 226 k | 0.76 s | +37 MB | 5.4 ms | 2.7 ms | 37 ms |
| 6.25 cm | greedy | 367 | 176 k | 2.1 s | +14-22 MB | 7.8-9.0 ms | 3.0-3.2 ms | 28-36 ms |
| 6.25 cm | per face | 367 | 803 k | 3.5-4.2 s | +61-73 MB | 7.4-8.0 ms | 3.2-3.3 ms | 56-57 ms |

* Steady frame time follows the number of chunk components more than triangles: at 6.25 cm, 4.6x
  fewer triangles lower the GPU time by about 6 %, while the frame time differs by less than the noise
  (greedy was slower in two of three pairs, faster in one).
* Greedy meshing halves load time and the crater remesh, and cuts process memory by 3-5x.
* With synchronous remeshing a crater was a 15-57 ms single-frame hitch at every resolution.

#### Off-game-thread remeshing

Same harness, greedy meshes, `-OBAsync=1` (default: snapshot on the game thread, mesh on `UE::Tasks`
workers, publish on the game thread) against `-OBAsync=0` (everything on the game thread), three
alternating pairs per voxel size, ranges over the three runs. 3.125 cm is measured in engine for the
first time.

| voxel | chunks | triangles | load async | load sync | frame p50 | GPU p50 | crater worst frame async (2 frames) | sync (1 frame) |
|---|---|---|---|---|---|---|---|---|
| 25 cm | 45 | 17 k | 0.10 s | 0.11-0.12 s | 4.2-4.8 ms | 2.4 ms | 10.4-12.1 ms | 13.1-13.9 ms |
| 12.5 cm | 116 | 51 k | 0.28-0.29 s | 0.33-0.37 s | 4.4-4.9 ms | 2.5 ms | 11.8-14.7 ms | 14.9-16.2 ms |
| 6.25 cm | 367 | 176 k | 1.3-1.4 s | 1.6-2.0 s | 6.0-8.3 ms | 2.9 ms | 18.8-22.6 ms | 28.4-37.9 ms |
| 3.125 cm | 1380 | 660 k | 9.7-11.0 s | 11.2-12.0 s | 13.9-15.0 ms | 4.3-4.7 ms | 98-107 ms | 113-130 ms |

Where the crater's game-thread time goes (one run each; `crater_*_ms` in the harness output; the
crater is placed on the core's surface so it is identical with collision off):

| voxel | path | collision | snapshot | meshing on game thread | publish | worst frame |
|---|---|---|---|---|---|---|
| 6.25 cm | async | on | 1.7 ms | - | 18.4 ms | 22.8 ms |
| 6.25 cm | sync | on | 1.4 ms | 3.7 ms | 15.6 ms | 25.9 ms |
| 6.25 cm | async | off | 2.6 ms | - | 1.1 ms | 6.8 ms |
| 6.25 cm | sync | off | 1.8 ms | 5.6 ms | 1.4 ms | 13.6 ms |
| 3.125 cm | async | on | 3.1 ms | - | 102.5 ms | 113.1 ms |
| 3.125 cm | sync | on | 2.0 ms | 16.7 ms | 95.4 ms | 127.8 ms |
| 3.125 cm | async | off | 3.3 ms | - | 3.0 ms | 16.9 ms |
| 3.125 cm | sync | off | 2.0 ms | 15.0 ms | 2.5 ms | 32.0 ms |

* Off-game-thread meshing removes meshing from the crater frame and loads 10-25 % faster (meshing
  runs on several cores). Without collision the crater frame is at or near a normal frame at every
  size (6.8 ms at 6.25 cm, 16.9 ms at 3.125 cm where a normal frame is 14 ms).
* **Collision cooking is now the hitch**: publishing a section cooks its collision synchronously on
  the game thread (`bUseAsyncCooking` is off so render and collision change in the same frame), which
  is 85-95 % of the crater frame with collision on: about 17 ms at 6.25 cm and 100 ms at 3.125 cm.
  Cooking collision off the game thread while keeping render and collision atomic per chunk
  (`rendering-collision.md`) is the next step.
* At 3.125 cm the steady frame is 14 ms with 1380 chunk components, close to a 60 Hz budget before any
  crater.

Fixes the first build needed: the `.generated.h` include must be last (UHT); the core's export macro
needed `DLLEXPORT`/`DLLIMPORT`, which only engine headers define; `bFlipWinding` must default to
true because core coordinates map directly onto Unreal's left-handed axes, mirroring the winding.

## Deferred features (by design)

* Asynchronous collision cooking (meshing is asynchronous; policy in `concurrency.md`, `rendering-collision.md`).
* Client-side requests and replication (`networking.md`).
* Smooth surfaces / density representation (`terrain.md`).
* Editor module (`future-modules.md`).

## Known limitations of the baseline

* Mixed chunks are dense: 1 byte per voxel. Uniform chunks (one material throughout) hold no voxel
  array, but a chunk made uniform by edits is not compacted until it is reloaded.
* Journal compaction (`CompactJournal`) is manual; there is no automatic checkpoint policy yet.
* Remeshing rescans only the dirty region (`UpdateChunkFaces`), but every remesh still builds a full
  `ChunkMeshInput` snapshot and re-emits the whole chunk's `MeshData`; `AOBTerrainActor` still
  rebuilds whole chunks (greedy by default, `BuildChunkMesh` with `bGreedyMeshing` off).
* Greedy meshes have T-junctions. No cracks were visible in the UE 5.8 host at 25 cm, but nothing
  closes them if they appear (for example at finer voxels or with MSAA off).
* The procedural foundation is a heightfield; caves only arise from modifications.
