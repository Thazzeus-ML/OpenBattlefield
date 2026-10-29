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

## Written but NOT compiled against Unreal Engine

No Unreal Engine installation was available in the environment where this foundation was built. The
following files were written against the UE 5.4 API from documentation and experience, reviewed, but
never built or run:

* `OpenBattlefield.uplugin`
* `Source/OpenBattlefieldCore/OpenBattlefieldCore.Build.cs`
* `Source/OpenBattlefieldCore/Private/OpenBattlefieldCoreModule.cpp` (the `OBCORE_UE_BUILD` branch)
* `Source/OBTerrain/**`

Expected risks when first compiled: UBT settings (`PCHUsage`, include-what-you-use warnings),
`UPROPERTY` on `TMap<FIntVector, TObjectPtr<...>>`, `ProceduralMeshComponent` API drift, triangle
winding (`bFlipWinding`), and `%s` with `ANSI_TO_TCHAR` in log macros. None of these affect the core.

The first contributor with an engine installation should open an issue titled "First in-engine build"
with the build log, and the fixes should be a single PR that updates this table.

## Deferred features (by design)

* Asynchronous mesh/collision rebuilds (policy documented in `concurrency.md`, `rendering-collision.md`).
* Dirty-region-limited remeshing (the AABB is tracked, not yet consumed by the mesher).
* Client-side requests and replication (`networking.md`).
* Smooth surfaces / density representation (`terrain.md`).
* Editor module (`future-modules.md`).

## Known limitations of the baseline

* Dense chunks: 1 byte per voxel regardless of content.
* Journal grows without bound until compaction exists.
* Whole-chunk remesh on any change; border sampling in `BuildMeshInput` is per voxel.
* The procedural foundation is a heightfield; caves only arise from modifications.
