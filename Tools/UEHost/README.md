# UEHost: in-engine verification project

A minimal C++ project that builds the plugin and runs a scripted check in a `-game` session. It is
not part of the plugin; UnrealBuildTool only compiles it when you open or build this project.

1. Make the repository available as `Tools/UEHost/Plugins/OpenBattlefield`, for example on Windows:
   `mklink /J Tools\UEHost\Plugins\OpenBattlefield <repository root>` (or copy the repository there).
   On Linux, copy it (re-run the copy after changing the repository):
   `mkdir -p Tools/UEHost/Plugins && rsync -a --delete --exclude .git --exclude /Tools/UEHost/Plugins --exclude /Tools/UEHost/Binaries --exclude /Tools/UEHost/Intermediate --exclude /Tools/UEHost/Saved <repository root>/ Tools/UEHost/Plugins/OpenBattlefield/`.
2. Build: `<Engine>/Build/BatchFiles/Build.bat OBHostEditor Win64 Development -project=<path>/OBHost.uproject`
   (`OBHost Win64 Shipping` for the game target). On Linux:
   `<Engine>/Build/BatchFiles/Linux/Build.sh OBHostEditor Linux Development -project=<path>/OBHost.uproject`
   (`OBHost Linux Shipping`, `OBHostServer Linux Development`). `OBHostServer` is a dedicated-server
   target; it needs an engine that includes server targets (a launcher-installed engine does not), and
   building it does not run it (a dedicated server needs cooked content).
3. Run: `UnrealEditor.exe <path>/OBHost.uproject -game -windowed -ResX=1600 -ResY=900 -log`. Headless
   on Linux: `timeout 1800 <Engine>/Binaries/Linux/UnrealEditor <path>/OBHost.uproject -game -nullrhi -unattended -nosound -nosplash -log -stdout`.
   In Epic's UE 5.5 Linux development container, the headless run needed
   `-ddc=InstalledNoZenLocalFallback` (with the default local Zen DDC, it stalled at frame 0).
   `-nullrhi` renders nothing, so this checks collision and the walks, not the visuals; the session
   quits by itself once the checks finish, but not when the terrain never finishes meshing, hence the
   `timeout`.

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

* `-OBVoxelCm=` 25, 12.5, 6.25 or 3.125 (power-of-two refinements of 25 cm only); `-OBGreedy=` 1 or
  0; `-OBAsync=` 1 (mesh on worker tasks, default) or 0 (on the game thread); `-OBCollision=` 1 or 0; `-OBAsyncCook=` 1 (cook collision on a worker, default) or 0;
  `-OBFrames=` frames to time (default 300).
* The terrain is loaded once to warm the engine up (`first_load_s`), destroyed and loaded again;
  `load_s`, `process_mb_delta` (physical memory) and `core_world_mb` describe the second load, with
  every chunk rebuilt as fast as possible (no per-tick limit).
* Frame times are measured with VSync off and no frame cap from a fixed camera (`frame_ms_*`, game
  thread wall time per frame; `gpu_ms_*`, RHI GPU frame time).
* A 1.5 m crater is then cut in the middle: `crater_remesh_ms` is the time until every affected chunk
  is republished, `crater_worst_frame_ms` the slowest frame meanwhile, and `crater_snapshot_ms`,
  `crater_inline_mesh_ms` and `crater_publish_ms` the terrain actor's game-thread time taking
  snapshots, meshing (synchronous path only) and publishing sections (collision cooking included).
  The crater is placed on the core's surface, so it lands in the same place with collision off; a run
  that does not finish within 2000 frames reports `crater_timed_out`. A screenshot lands in
  `Saved/Resolution/`.
