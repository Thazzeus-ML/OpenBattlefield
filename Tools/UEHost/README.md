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
crater radius, and screenshots again. It then walks two default `ACharacter` capsules (AI-controlled,
with a cylinder for visibility) into two of the craters, one centred on a chunk face so the walk ends on
a chunk seam, checks that each ends standing on the crater floor without ever dropping below it,
takes a close-up and quits. Results are logged with the prefix `OBShowcase:`;
screenshots land in `Saved/Showcase/`.

## Resolution measurement

`AOBResolutionGameMode` loads the scene of the core's `resolution` benchmark (32 m across, +/-2 m
relief, every generation length scaled with the voxel) at one voxel size, and logs one line
`OBResolution: {json}`:

```
UnrealEditor.exe <path>/OBHost.uproject /Engine/Maps/Templates/Template_Default?game=/Script/OBHost.OBResolutionGameMode -game -windowed -ResX=1600 -ResY=900 -log -OBVoxelCm=12.5 -OBGreedy=1
```

* `-OBVoxelCm=` 25, 12.5 or 6.25 (power-of-two refinements of 25 cm only); `-OBGreedy=` 1 or 0;
  `-OBFrames=` frames to time (default 300).
* The terrain is loaded once to warm the engine up (`first_load_s`), destroyed and loaded again;
  `load_s`, `process_mb_delta` (physical memory) and `core_world_mb` describe the second load, with
  every chunk rebuilt as fast as possible (no per-tick limit).
* Frame times are measured with VSync off and no frame cap from a fixed camera (`frame_ms_*`, game
  thread wall time per frame; `gpu_ms_*`, RHI GPU frame time).
* A 1.5 m crater is then cut in the middle: `crater_remesh_ms` is the time until every affected chunk
  is republished, `crater_worst_frame_ms` the slowest frame meanwhile. A screenshot lands in
  `Saved/Resolution/`.
