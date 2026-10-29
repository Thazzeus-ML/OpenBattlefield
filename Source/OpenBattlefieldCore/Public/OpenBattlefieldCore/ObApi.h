// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
#pragma once

// Export/import macro shared between the standalone (CMake) build and the Unreal Engine module build.
//
// When this directory is compiled as the Unreal module "OpenBattlefieldCore", UnrealBuildTool defines
// OPENBATTLEFIELDCORE_API to the correct DLL export/import attribute (or to nothing in monolithic
// builds). The standalone build never defines it, so OBCORE_API expands to nothing and the library is
// linked statically. No Unreal header is included here or anywhere else in the core.
#if defined(OPENBATTLEFIELDCORE_API)
#define OBCORE_API OPENBATTLEFIELDCORE_API
#else
#define OBCORE_API
#endif

// The core is built without exceptions and without RTTI so that it compiles identically inside Unreal,
// which disables both. Nothing in the core may throw or use dynamic_cast / typeid.
#if defined(__cpp_exceptions) && !defined(OBCORE_ALLOW_EXCEPTIONS_IN_HOST)
// Exceptions may be enabled by the host toolchain; the core simply never uses them.
#endif
