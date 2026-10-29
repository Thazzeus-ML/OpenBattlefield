// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
#pragma once

// Export/import macro shared between the standalone (CMake) build and the Unreal Engine module build.
//
// When this directory is compiled as the Unreal module "OpenBattlefieldCore", UnrealBuildTool defines
// OPENBATTLEFIELDCORE_API to the correct DLL export/import attribute (or to nothing in monolithic
// builds). The standalone build never defines it, so OBCORE_API expands to nothing and the library is
// linked statically. No Unreal header is included here or anywhere else in the core.
#if defined(OPENBATTLEFIELDCORE_API)
// UnrealBuildTool spells the attribute as DLLEXPORT / DLLIMPORT, which the engine's platform headers
// define. The core includes none of them, so supply the same definitions (textually identical to the
// engine's, so a later engine include is not a conflicting redefinition).
#if !defined(DLLEXPORT)
#if defined(_MSC_VER) || defined(_WIN32)
#define DLLEXPORT __declspec(dllexport)
#define DLLIMPORT __declspec(dllimport)
#else
#define DLLEXPORT __attribute__((visibility("default")))
#define DLLIMPORT __attribute__((visibility("default")))
#endif
#endif
#define OBCORE_API OPENBATTLEFIELDCORE_API
#else
#define OBCORE_API
#endif

// The core is built without exceptions and without RTTI so that it compiles identically inside Unreal,
// which disables both. Nothing in the core may throw or use dynamic_cast / typeid.
#if defined(__cpp_exceptions) && !defined(OBCORE_ALLOW_EXCEPTIONS_IN_HOST)
// Exceptions may be enabled by the host toolchain; the core simply never uses them.
#endif
