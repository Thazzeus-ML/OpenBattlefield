#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "OBShowcaseGameMode.generated.h"

class AOBTerrainActor;
class ACameraActor;
class ACharacter;

/**
 * Host-project harness for the first in-engine verification of the OpenBattlefield plugin (issue #2).
 * Spawns an AOBTerrainActor, waits for its chunk meshes, screenshots it, applies craters through the
 * public ApplyCrater API, checks collision with line traces before and after, screenshots again,
 * then walks a capsule character into two of the craters (one centred on a chunk face, so the walk
 * reaches a chunk seam) and checks that it ends up standing on the crater floor. Results are logged
 * with the prefix "OBShowcase:". Not part of the plugin.
 */
UCLASS()
class AOBShowcaseGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    AOBShowcaseGameMode();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

private:
    enum class EPhase : uint8
    {
        WaitForTerrain,
        ShotBefore,
        Crater,
        WaitForRemesh,
        ShotAfter,
        WalkSpawn,
        Walk,
        ShotWalk,
        Done
    };

    struct FWalk
    {
        int32 Crater = 0;
        FVector2D From;
        float FloorZ = 0.0f;          // crater floor at the centre, from the line trace after the crater
        float MinBottomZ = 1e9f;      // lowest capsule bottom seen during the walk
        ACharacter* Walker = nullptr; // owned by the world
    };

    bool TraceDown(const FVector2D& XY, FVector& OutHit, const AActor* Ignore = nullptr) const;
    ACharacter* SpawnWalker(const FVector& FeetLocation);
    void Screenshot(const TCHAR* Name);

    UPROPERTY()
    TObjectPtr<AOBTerrainActor> Terrain;
    UPROPERTY()
    TObjectPtr<ACameraActor> Camera;

    EPhase Phase = EPhase::WaitForTerrain;
    double PhaseStart = 0.0;
    int32 FramesInPhase = 0;
    TArray<FVector2D> CraterXY;
    TArray<float> SurfaceBefore;
    TArray<float> FloorAfter;
    TArray<FWalk> Walks;
    int32 WalkPassed = 0;
};
