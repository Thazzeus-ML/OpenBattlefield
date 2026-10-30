// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
#pragma once

// Umbrella header for the OpenBattlefield simulation core.
//
// The core is an engine-agnostic C++17 library. It is compiled twice from the same sources: once by
// CMake for tests and benchmarks, and once by UnrealBuildTool as the "OpenBattlefieldCore" module.
// See Docs/shared-source.md for how that is achieved.

#include "OpenBattlefieldCore/ObApi.h"
#include "OpenBattlefieldCore/ObChunk.h"
#include "OpenBattlefieldCore/ObChunkMesher.h"
#include "OpenBattlefieldCore/ObCoordinates.h"
#include "OpenBattlefieldCore/ObHash.h"
#include "OpenBattlefieldCore/ObInstrumentation.h"
#include "OpenBattlefieldCore/ObProceduralGenerator.h"
#include "OpenBattlefieldCore/ObRemeshScheduler.h"
#include "OpenBattlefieldCore/ObSerialization.h"
#include "OpenBattlefieldCore/ObTerrainOperation.h"
#include "OpenBattlefieldCore/ObTerrainWorld.h"
#include "OpenBattlefieldCore/ObTypes.h"
#include "OpenBattlefieldCore/ObWorldConfig.h"

namespace ob
{

constexpr uint32_t kCoreVersionMajor = 0;
constexpr uint32_t kCoreVersionMinor = 1;
constexpr uint32_t kCoreVersionPatch = 0;

OBCORE_API const char* CoreVersionString();

} // namespace ob
