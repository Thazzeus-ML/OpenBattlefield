# One source tree, two build systems

The requirement: no duplicate terrain algorithms between standalone tests and Unreal modules, and no
fragile build-system coupling.

## How it works

1. The core sources live only in the Unreal module directory, `Source/OpenBattlefieldCore/{Public,Private}`.
2. UnrealBuildTool compiles that directory as the module `OpenBattlefieldCore`
   (`OpenBattlefieldCore.Build.cs`). It defines `OBCORE_UE_BUILD=1` and provides `OPENBATTLEFIELDCORE_API`.
3. The root `CMakeLists.txt` lists the same files by path and compiles them into a static library.
   It defines neither macro.
4. `ObApi.h` maps `OBCORE_API` to `OPENBATTLEFIELDCORE_API` when Unreal defines it and to nothing
   otherwise.
5. `Private/OpenBattlefieldCoreModule.cpp` is the only file that includes an Unreal header, and the
   whole file is wrapped in `#if OBCORE_UE_BUILD`. Standalone it compiles to an anchor function.

There is no generated code, no shared CMake/UBT glue and no symlinks. The two builds only agree on a
file list, and CI compiling the standalone build is what guarantees the core stays engine free.

## Rules for core code

* C++17, also compiled as C++20 in CI (`OB_CXX_STANDARD=20`) because Unreal 5.3+ defaults to C++20.
* No exceptions, no RTTI (CMake mirrors Unreal with `-fno-exceptions -fno-rtti`).
* No Unreal types, macros or headers. No `check`, `ensure`, `TEXT`, `TArray`, `FVector`.
* Standard containers are fine (`std::vector`, `std::map`, `std::set`, `std::deque`). The core does
  not use `std::string` or iostreams in interfaces.
* Public headers are included as `OpenBattlefieldCore/ObFoo.h`.
* Avoid identifiers that collide with Windows or Unreal macros (`min`, `max`, `PI`, `DLLEXPORT`).
* Any change to a `Public/` header must keep the file compiling in both environments. If a header is
  added, add it to both `CMakeLists.txt` and nothing else: UBT globs the directory.

## Known limitation

The project maintainers have not yet compiled the Unreal module in an engine installation
(see `status.md`). The standalone side is verified in CI; the UBT side is reviewed but unverified.
