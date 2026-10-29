// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
//
// Unreal module wrapper around the engine-agnostic simulation core.
//
// The sources in Public/ and Private/ are the SAME files the standalone CMake build compiles
// (see Docs/shared-source.md). They contain no Unreal types. The only Unreal-aware file is
// Private/OpenBattlefieldCoreModule.cpp, which is enabled by the OBCORE_UE_BUILD definition below.
//
// STATUS: built and run on UE 5.8.2 (Windows). See Docs/status.md.

using UnrealBuildTool;

public class OpenBattlefieldCore : ModuleRules
{
    public OpenBattlefieldCore(ReadOnlyTargetRules Target) : base(Target)
    {
        // The core is plain C++17/20 with no shared PCH and no unity builds, so it compiles
        // identically to the standalone build.
        PCHUsage = PCHUsageMode.NoPCHs;
        bUseUnity = false;

        // Enables the IMPLEMENT_MODULE boilerplate in OpenBattlefieldCoreModule.cpp.
        PublicDefinitions.Add("OBCORE_UE_BUILD=1");

        // "Core" is required only for Modules/ModuleManager.h in the module boilerplate. The core
        // library itself never includes it.
        PublicDependencyModuleNames.AddRange(new string[] { "Core" });
    }
}
