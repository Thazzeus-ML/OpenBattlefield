// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
//
// OBTerrain: Unreal runtime integration of the OpenBattlefield terrain core.
//
// STATUS: built and run on UE 5.8.2 (Windows). See Docs/status.md.

using UnrealBuildTool;

public class OBTerrain : ModuleRules
{
    public OBTerrain(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "OpenBattlefieldCore",
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "ProceduralMeshComponent",
        });
    }
}
