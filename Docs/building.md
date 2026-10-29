# Building

## Standalone core, tests and benchmarks (no Unreal required)

Requirements: CMake 3.20+, a C++17 compiler (GCC 11+, Clang 14+, MSVC 2022, Apple Clang 14+),
Ninja recommended, Python 3 for `Tools/bench_compare.py`.

```bash
cmake --preset release          # or: debug, asan, cpp20, msvc-release
cmake --build --preset release
ctest --preset release
./build/release/Benchmarks/Core/ob_bench --config Benchmarks/workloads/default.cfg --out results.json
```

Without presets:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Options: `OB_BUILD_TESTS`, `OB_BUILD_BENCHMARKS`, `OB_WARNINGS_AS_ERRORS` (default ON),
`OB_ENABLE_SANITIZERS` (GCC/Clang), `OB_CXX_STANDARD` (17 or 20).

Run a single test binary with a name filter: `./build/release/Tests/Core/TestWorld Boundary`.

## As an Unreal Engine plugin (verified on UE 5.8.2 / Windows, see status.md)

1. Clone the repository into `YourProject/Plugins/OpenBattlefield` (the repository root is the
   plugin root; `OpenBattlefield.uplugin` must sit directly under `Plugins/OpenBattlefield/`).
2. Enable the `ProceduralMeshComponent` plugin (declared as a dependency in the `.uplugin`).
3. Regenerate project files and build. The engine version targeted is 5.4; later 5.x versions are
   expected to work.
4. Place an `AOBTerrainActor` in a level, set its `Settings`, play, and call `ApplyCrater` from a
   Blueprint or console command.

Please report the result of the first in-engine build in an issue (see `status.md`).

## Formatting

```bash
clang-format -i $(git ls-files 'Source/*.h' 'Source/*.cpp' 'Tests/*.h' 'Tests/*.cpp' 'Benchmarks/*.h' 'Benchmarks/*.cpp')
```

CI runs the same command with `--dry-run --Werror`.
