# Implementation status

Honest record of what has and has not been verified. Update this file in every PR that changes it.

## Verified in this repository's CI

| Component | Verification |
|---|---|
| `OpenBattlefieldCore` standalone build | GCC 13 and Clang 18 on Linux, MSVC on Windows, Apple Clang on macOS (CI matrix), C++17 and C++20, `-Werror` with an extensive warning set, `-fno-exceptions -fno-rtti` |
| Core test suite | 50 test cases across 7 executables, all passing; also under AddressSanitizer + UndefinedBehaviorSanitizer |
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
| Walking into a crater with a character | Default `ACharacter` capsule (AI-controlled) walks into two craters, one centred on a chunk face so it ends on the seam between two chunks' collision: both end on ground, 17–18 cm from the centre, feet 2 cm above the traced floor, never lower (2/2, three runs) |

Fixes the first build needed: the `.generated.h` include must be last (UHT); the core's export macro
needed `DLLEXPORT`/`DLLIMPORT`, which only engine headers define; `bFlipWinding` must default to
true because core coordinates map directly onto Unreal's left-handed axes, mirroring the winding.

## Deferred features (by design)

* Asynchronous mesh/collision rebuilds (policy documented in `concurrency.md`, `rendering-collision.md`).
* Client-side requests and replication (`networking.md`).
* Smooth surfaces / density representation (`terrain.md`).
* Editor module (`future-modules.md`).

## Known limitations of the baseline

* Mixed chunks are dense: 1 byte per voxel. Uniform chunks (one material throughout) hold no voxel
  array, but a chunk made uniform by edits is not compacted until it is reloaded.
* Journal compaction (`CompactJournal`) is manual; there is no automatic checkpoint policy yet.
* Remeshing rescans only the dirty region (`UpdateChunkFaces`), but every remesh still builds a full
  `ChunkMeshInput` snapshot and re-emits the whole chunk's `MeshData`; `AOBTerrainActor` still
  rebuilds whole chunks with `BuildChunkMesh`.
* The procedural foundation is a heightfield; caves only arise from modifications.
