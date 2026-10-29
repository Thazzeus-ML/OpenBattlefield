// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
//
// Unreal module boilerplate for the OpenBattlefieldCore module.
//
// This is the ONLY file in the core that touches an Unreal header, and it is compiled out entirely
// unless OBCORE_UE_BUILD is defined by OpenBattlefieldCore.Build.cs. The standalone CMake build sees an
// empty translation unit. Everything else in Public/ and Private/ is engine-agnostic C++17.
#if defined(OBCORE_UE_BUILD) && OBCORE_UE_BUILD

#include "Modules/ModuleManager.h"

class FOpenBattlefieldCoreModule : public IModuleInterface
{
public:
    virtual void StartupModule() override {}
    virtual void ShutdownModule() override {}
};

IMPLEMENT_MODULE(FOpenBattlefieldCoreModule, OpenBattlefieldCore)

#else

// Keep the translation unit non-empty for toolchains that warn about empty files.
namespace ob
{
namespace detail
{
int OpenBattlefieldCoreModuleStandaloneAnchor()
{
    return 0;
}
} // namespace detail
} // namespace ob

#endif
