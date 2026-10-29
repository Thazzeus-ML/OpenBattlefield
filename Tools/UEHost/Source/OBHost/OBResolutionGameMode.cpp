#include "OBResolutionGameMode.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "OBTerrainActor.h"
#include "ProceduralMeshComponent.h"
// The core's exported classes hold std::vector members (C4251); host and plugin share one toolchain.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)
#endif
#include "OBTerrainWorldSubsystem.h"
#include "OpenBattlefieldCore/ObTerrainWorld.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include "RHI.h"
#include "UnrealClient.h"

namespace
{
const FVector kTerrainOrigin(0.0, 0.0, 3000.0);
constexpr double kHalfSideCm = 1600.0; // the scene is 32 m across at every voxel size
// Meshed at least +/-4 m vertically: the relief (+/-2 m nominal, more at peaks and valleys) and craters.
constexpr double kHalfHeightCm = 400.0;

double Percentile(TArray<double> Values, double P)
{
    if (Values.Num() == 0)
        return 0.0;
    Values.Sort();
    const int32 Rank = FMath::Clamp(FMath::CeilToInt(P * Values.Num()) - 1, 0, Values.Num() - 1);
    return Values[Rank];
}

double Mean(const TArray<double>& Values)
{
    double Sum = 0.0;
    for (double V : Values)
        Sum += V;
    return Values.Num() > 0 ? Sum / Values.Num() : 0.0;
}
} // namespace

AOBResolutionGameMode::AOBResolutionGameMode()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = true;
}

void AOBResolutionGameMode::BeginPlay()
{
    Super::BeginPlay();
    UWorld* World = GetWorld();

    FParse::Value(FCommandLine::Get(), TEXT("OBVoxelCm="), VoxelCm);
    int32 Greedy = 1;
    FParse::Value(FCommandLine::Get(), TEXT("OBGreedy="), Greedy);
    bGreedy = Greedy != 0;
    FParse::Value(FCommandLine::Get(), TEXT("OBFrames="), MeasureFrames);

    // Uncapped frames, so frame time measures the work and not the display.
    if (IConsoleVariable* VSync = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync")))
        VSync->Set(0);
    if (IConsoleVariable* MaxFps = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS")))
        MaxFps->Set(0);

    // Scale every generation length with the voxel so the terrain is physically identical (the noise
    // lattice is a power of two in voxels, so only power-of-two refinements of 25 cm qualify).
    Scale = FMath::Max(1, FMath::RoundToInt(25.0f / VoxelCm));
    VoxelCm = 25.0f / static_cast<float>(Scale);
    PhaseStartSeconds = FPlatformTime::Seconds();
    SpawnTerrain();

    const FVector Eye = kTerrainOrigin + FVector(-2600.0, -2600.0, 2200.0);
    Camera =
        World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), Eye, (kTerrainOrigin - Eye).Rotation());
    Camera->GetCameraComponent()->SetFieldOfView(55.0f);
    if (APlayerController* PC = World->GetFirstPlayerController())
        PC->SetViewTarget(Camera);

    UE_LOG(LogTemp, Display, TEXT("OBResolution: voxel %.3f cm, greedy %d"), VoxelCm, bGreedy ? 1 : 0);
}

void AOBResolutionGameMode::SpawnTerrain()
{
    const int32 S = Scale;
    const double ChunkCm = 32.0 * VoxelCm;
    const int32 Radius = FMath::CeilToInt(kHalfSideCm / ChunkCm);
    const int32 RadiusZ = FMath::Max(1, FMath::CeilToInt(kHalfHeightCm / ChunkCm));
    FActorSpawnParameters Params;
    Params.bDeferConstruction = true;
    Terrain = GetWorld()->SpawnActor<AOBTerrainActor>(AOBTerrainActor::StaticClass(),
                                                      FTransform(kTerrainOrigin), Params);
    FOBTerrainWorldSettings& Settings = Terrain->Settings;
    Settings.ChunkSize = 32;
    Settings.VoxelSizeCm = VoxelCm;
    Settings.MinChunk = FIntVector(-Radius - 1, -Radius - 1, -RadiusZ - 1);
    Settings.MaxChunk = FIntVector(Radius + 1, Radius + 1, RadiusZ + 1);
    Settings.MaxOperationRadiusVoxels = 64 * S;
    Settings.BaseHeight = 0;
    Settings.HeightAmplitude = 8 * S;
    Settings.NoiseScaleLog2 = 6 + FMath::FloorLog2(S);
    Settings.TopsoilDepth = S;
    Settings.SoilDepth = 10 * S;
    Settings.BedrockHeight = -64 * S;
    Terrain->VisibleChunkRadius = Radius;
    Terrain->VisibleChunkRadiusZ = RadiusZ;
    Terrain->MaxChunkRebuildsPerTick = 100000; // load everything as fast as possible: measures throughput
    Terrain->bGreedyMeshing = bGreedy;
    Terrain->FinishSpawning(FTransform(kTerrainOrigin));
}

bool AOBResolutionGameMode::CountMeshes(int32& OutChunks, int64& OutTriangles) const
{
    OutChunks = 0;
    OutTriangles = 0;
    if (!Terrain || Terrain->GetPendingRebuildCount() != 0)
        return false;
    for (UActorComponent* C : Terrain->GetComponents())
        if (UProceduralMeshComponent* Mesh = Cast<UProceduralMeshComponent>(C))
            if (Mesh->GetNumSections() > 0)
            {
                const int32 Indices = Mesh->GetProcMeshSection(0)->ProcIndexBuffer.Num();
                OutChunks += Indices > 0 ? 1 : 0;
                OutTriangles += Indices / 3;
            }
    return OutChunks > 0;
}

void AOBResolutionGameMode::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const double Now = FPlatformTime::Seconds();
    const double FrameMsNow = LastTickSeconds > 0.0 ? (Now - LastTickSeconds) * 1000.0 : 0.0;
    LastTickSeconds = Now;
    ++FramesInPhase;
    auto Advance = [&](EPhase Next)
    {
        Phase = Next;
        PhaseStartSeconds = Now;
        FramesInPhase = 0;
    };

    switch (Phase)
    {
    case EPhase::WarmUp:
    {
        // The first load also pays for engine warm-up (window, shaders); measure a second one.
        int32 Chunks = 0;
        int64 Tris = 0;
        if (FramesInPhase > 2 && CountMeshes(Chunks, Tris))
        {
            WarmLoadSeconds = Now - PhaseStartSeconds;
            Terrain->Destroy();
            Terrain = nullptr;
            GEngine->ForceGarbageCollection(true);
            Advance(EPhase::Respawn);
        }
        break;
    }
    case EPhase::Respawn:
        if (FramesInPhase > 30)
        {
            UsedPhysicalBefore = FPlatformMemory::GetStats().UsedPhysical;
            SpawnTerrain();
            Advance(EPhase::Load);
        }
        break;
    case EPhase::Load:
        if (FramesInPhase > 2 && CountMeshes(MeshedChunks, Triangles))
        {
            LoadSeconds = Now - PhaseStartSeconds;
            UsedPhysicalLoaded = FPlatformMemory::GetStats().UsedPhysical;
            if (const UOBTerrainWorldSubsystem* Sub = GetWorld()->GetSubsystem<UOBTerrainWorldSubsystem>())
                if (const ob::TerrainWorld* Core = Sub->GetCoreWorld())
                    CoreWorldBytes = static_cast<int64>(Core->EstimateMemoryBytes());
            Advance(EPhase::Settle);
        }
        break;
    case EPhase::Settle:
        if (FramesInPhase > 120)
            Advance(EPhase::Measure);
        break;
    case EPhase::Measure:
        FrameMs.Add(FrameMsNow);
        GpuMs.Add(FPlatformTime::ToMilliseconds64(RHIGetGPUFrameCycles(0)));
        if (FrameMs.Num() >= MeasureFrames)
        {
            // A 1.5 m crater in the middle, as in the core benchmark.
            FVector Top = kTerrainOrigin + FVector(0.0, 0.0, 1000.0);
            FHitResult Hit;
            FCollisionQueryParams Query(TEXT("OBResolutionTrace"), true);
            if (GetWorld()->LineTraceSingleByChannel(Hit, Top, kTerrainOrigin - FVector(0.0, 0.0, 1000.0),
                                                     ECC_Visibility, Query))
                Top = Hit.ImpactPoint;
            bCraterAccepted = Terrain->ApplyCrater(Top, 150.0f);
            Advance(EPhase::Crater);
        }
        break;
    case EPhase::Crater:
        // Time from ApplyCrater until every affected chunk is republished, and the worst frame.
        CraterWorstFrameMs = FMath::Max(CraterWorstFrameMs, FrameMsNow);
        ++CraterFrames;
        if (Terrain->GetPendingRebuildCount() == 0)
        {
            CraterSeconds = Now - PhaseStartSeconds;
            Report();
            const FString Path = FPaths::Combine(
                FPaths::ProjectSavedDir(), TEXT("Resolution"),
                FString::Printf(TEXT("%gcm-%s.png"), VoxelCm, bGreedy ? TEXT("greedy") : TEXT("faces")));
            FScreenshotRequest::RequestScreenshot(Path, false, false);
            Advance(EPhase::Shot);
        }
        break;
    case EPhase::Shot:
        if (FramesInPhase > 10)
        {
            Advance(EPhase::Done);
            FPlatformMisc::RequestExit(false, TEXT("OBResolution finished"));
        }
        break;
    case EPhase::Done:
        break;
    }
}

void AOBResolutionGameMode::Report()
{
    const double Mb = 1024.0 * 1024.0;
    UE_LOG(
        LogTemp, Display,
        TEXT("OBResolution: {\"voxel_cm\": %g, \"greedy\": %d, \"chunks_meshed\": %d, \"triangles\": %lld, "
             "\"first_load_s\": %.3f, \"load_s\": %.3f, \"process_mb_delta\": %.1f, \"core_world_mb\": %.2f, "
             "\"frames\": %d, "
             "\"frame_ms_mean\": %.3f, \"frame_ms_p50\": %.3f, \"frame_ms_p95\": %.3f, \"gpu_ms_p50\": %.3f, "
             "\"gpu_ms_p95\": %.3f, \"crater_accepted\": %d, \"crater_remesh_ms\": %.2f, "
             "\"crater_frames\": %d, \"crater_worst_frame_ms\": %.2f}"),
        VoxelCm, bGreedy ? 1 : 0, MeshedChunks, Triangles, WarmLoadSeconds, LoadSeconds,
        (static_cast<double>(UsedPhysicalLoaded) - static_cast<double>(UsedPhysicalBefore)) / Mb,
        static_cast<double>(CoreWorldBytes) / Mb, FrameMs.Num(), Mean(FrameMs), Percentile(FrameMs, 0.5),
        Percentile(FrameMs, 0.95), Percentile(GpuMs, 0.5), Percentile(GpuMs, 0.95), bCraterAccepted ? 1 : 0,
        CraterSeconds * 1000.0, CraterFrames, CraterWorstFrameMs);
}
