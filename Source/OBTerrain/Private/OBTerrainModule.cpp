// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
//
// STATUS: built and run on UE 5.8.2 (Windows). See Docs/status.md.
#include "Modules/ModuleManager.h"

class FOBTerrainModule : public IModuleInterface
{
public:
    virtual void StartupModule() override {}
    virtual void ShutdownModule() override {}
};

IMPLEMENT_MODULE(FOBTerrainModule, OBTerrain)
