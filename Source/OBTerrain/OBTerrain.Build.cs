// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
//
// OBTerrain: Unreal runtime integration of the OpenBattlefield terrain core.
//
// STATUS: not yet compiled against an Unreal Engine installation by the project maintainers.
// See Docs/status.md.

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
