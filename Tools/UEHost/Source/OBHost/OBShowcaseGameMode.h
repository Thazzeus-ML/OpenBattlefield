#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "OBShowcaseGameMode.generated.h"

class AOBTerrainActor;
class ACameraActor;

/**
 * Host-project harness for the first in-engine verification of the OpenBattlefield plugin (issue #2).
 * Spawns an AOBTerrainActor, waits for its chunk meshes, screenshots it, applies craters through the
 * public ApplyCrater API, checks collision with line traces before and after, screenshots again and
 * quits. Results are logged with the prefix "OBShowcase:". Not part of the plugin.
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
        Done
    };

    bool TraceDown(const FVector2D& XY, FVector& OutHit) const;
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
};
