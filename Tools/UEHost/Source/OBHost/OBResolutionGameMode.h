#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "OBResolutionGameMode.generated.h"

class AOBTerrainActor;
class ACameraActor;

/**
 * Host-project harness for in-engine resolution measurements (roadmap goal: resolution). Loads the
 * same physical scene as the core's `resolution` benchmark (a +/-2 m relief, 32 m across) at the voxel
 * size given by -OBVoxelCm= (25, 12.5 or 6.25), with greedy meshing on or off (-OBGreedy=1/0). Logs
 * one line "OBResolution: {json}" with load time, memory, mesh size, frame times and crater remesh
 * latency, saves a screenshot and quits. Not part of the plugin.
 */
UCLASS()
class AOBResolutionGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    AOBResolutionGameMode();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

private:
    enum class EPhase : uint8
    {
        WarmUp,
        Respawn,
        Load,
        Settle,
        Measure,
        Crater,
        Shot,
        Done
    };

    void SpawnTerrain();
    /** Chunks with geometry and their triangles; false while nothing is meshed yet. */
    bool CountMeshes(int32& OutChunks, int64& OutTriangles) const;
    void Report();

    UPROPERTY()
    TObjectPtr<AOBTerrainActor> Terrain;
    UPROPERTY()
    TObjectPtr<ACameraActor> Camera;

    EPhase Phase = EPhase::WarmUp;
    int32 Scale = 1; // voxels per 25 cm
    float VoxelCm = 25.0f;
    bool bGreedy = true;
    int32 MeasureFrames = 300;
    int32 FramesInPhase = 0;
    double PhaseStartSeconds = 0.0;
    double LastTickSeconds = 0.0;
    uint64 UsedPhysicalBefore = 0;

    double WarmLoadSeconds = 0.0;
    double LoadSeconds = 0.0;
    uint64 UsedPhysicalLoaded = 0;
    int32 MeshedChunks = 0;
    int64 Triangles = 0;
    int64 CoreWorldBytes = 0;
    TArray<double> FrameMs;
    TArray<double> GpuMs;
    double CraterSeconds = 0.0;
    double CraterWorstFrameMs = 0.0;
    int32 CraterFrames = 0;
    bool bCraterAccepted = false;
};
