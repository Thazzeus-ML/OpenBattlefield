# UEHost: in-engine verification project

A minimal C++ project that builds the plugin and runs a scripted check in a `-game` session. It is
not part of the plugin; UnrealBuildTool only compiles it when you open or build this project.

1. Make the repository available as `Tools/UEHost/Plugins/OpenBattlefield`, for example on Windows:
   `mklink /J Tools\UEHost\Plugins\OpenBattlefield <repository root>` (or copy the repository there).
2. Build: `<Engine>/Build/BatchFiles/Build.bat OBHostEditor Win64 Development -project=<path>/OBHost.uproject`
   (`OBHost Win64 Shipping` for the game target).
3. Run: `UnrealEditor.exe <path>/OBHost.uproject -game -windowed -ResX=1600 -ResY=900 -log`

`AOBShowcaseGameMode` spawns an `AOBTerrainActor` over the template map, waits until every chunk is
meshed, screenshots it, applies three craters through `ApplyCrater` (inside a chunk, on a chunk face,
on a chunk corner), checks with complex-collision line traces that the surface dropped by most of the
crater radius, screenshots again and quits. Results are logged with the prefix `OBShowcase:`;
screenshots land in `Saved/Showcase/`.
