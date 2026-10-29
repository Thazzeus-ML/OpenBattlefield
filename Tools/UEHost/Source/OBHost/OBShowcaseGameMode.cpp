#include "OBShowcaseGameMode.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "Misc/Paths.h"
#include "OBTerrainActor.h"
#include "UnrealClient.h"

namespace
{
// The terrain actor sits above the template map's floor so the floor never hides it.
const FVector kTerrainOrigin(0.0, 0.0, 3000.0);
constexpr float kCraterRadiusCm = 300.0f;
} // namespace

AOBShowcaseGameMode::AOBShowcaseGameMode()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = true;
}

void AOBShowcaseGameMode::BeginPlay()
{
    Super::BeginPlay();
    UWorld* World = GetWorld();

    FActorSpawnParameters Params;
    Params.bDeferConstruction = true;
    Terrain = World->SpawnActor<AOBTerrainActor>(AOBTerrainActor::StaticClass(), FTransform(kTerrainOrigin),
                                                 Params);
    Terrain->VisibleChunkRadius = 2;
    Terrain->VisibleChunkRadiusZ = 2; // deep enough that a 3 m crater never exits the meshed volume
    Terrain->MaxChunkRebuildsPerTick = 256;
    Terrain->FinishSpawning(FTransform(kTerrainOrigin));

    // Isometric-ish view of the 5 x 5 chunk area (40 m across at 25 cm voxels).
    const FVector Eye = kTerrainOrigin + FVector(-4200.0, -4200.0, 3600.0);
    const FRotator Look = (kTerrainOrigin - Eye).Rotation();
    Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), Eye, Look);
    Camera->GetCameraComponent()->SetFieldOfView(50.0f);
    if (APlayerController* PC = World->GetFirstPlayerController())
        PC->SetViewTarget(Camera);

    // Craters: one inside a chunk, one on a chunk face, one on a chunk corner (8 m chunks).
    CraterXY = {FVector2D(400.0, 400.0), FVector2D(800.0, -300.0), FVector2D(-800.0, -800.0)};
    PhaseStart = World->GetTimeSeconds();
    UE_LOG(LogTemp, Display, TEXT("OBShowcase: terrain actor spawned"));
}

bool AOBShowcaseGameMode::TraceDown(const FVector2D& XY, FVector& OutHit) const
{
    const FVector Start(XY.X, XY.Y, kTerrainOrigin.Z + 5000.0);
    const FVector End(XY.X, XY.Y, kTerrainOrigin.Z - 5000.0);
    FHitResult Hit;
    FCollisionQueryParams Query(TEXT("OBShowcaseTrace"), /*bTraceComplex=*/true);
    if (!GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Query))
        return false;
    if (!Hit.GetActor() || Hit.GetActor() != Terrain)
        return false;
    OutHit = Hit.ImpactPoint;
    return true;
}

void AOBShowcaseGameMode::Screenshot(const TCHAR* Name)
{
    const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Showcase"), Name);
    FScreenshotRequest::RequestScreenshot(Path, /*bShowUI=*/false, /*bAddFilenameSuffix=*/false);
    UE_LOG(LogTemp, Display, TEXT("OBShowcase: screenshot requested %s"), *Path);
}

void AOBShowcaseGameMode::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!Terrain)
        return;
    const double Now = GetWorld()->GetTimeSeconds();
    ++FramesInPhase;
    auto Advance = [&](EPhase Next)
    {
        Phase = Next;
        PhaseStart = Now;
        FramesInPhase = 0;
    };

    switch (Phase)
    {
    case EPhase::WaitForTerrain:
        if (Terrain->GetPendingRebuildCount() == 0 && Now - PhaseStart > 2.0 && FramesInPhase > 30)
        {
            for (const FVector2D& XY : CraterXY)
            {
                FVector Hit;
                const bool bHit = TraceDown(XY, Hit);
                SurfaceBefore.Add(bHit ? static_cast<float>(Hit.Z) : NAN);
                UE_LOG(LogTemp, Display, TEXT("OBShowcase: before crater at (%.0f, %.0f): %s z=%.1f"), XY.X,
                       XY.Y, bHit ? TEXT("hit") : TEXT("NO HIT"), bHit ? Hit.Z : 0.0);
            }
            Screenshot(TEXT("before.png"));
            Advance(EPhase::ShotBefore);
        }
        else if (Now - PhaseStart > 60.0)
        {
            UE_LOG(LogTemp, Error, TEXT("OBShowcase: terrain never finished (%d pending)"),
                   Terrain->GetPendingRebuildCount());
            Advance(EPhase::Done);
        }
        break;

    case EPhase::ShotBefore:
        if (FramesInPhase > 20)
            Advance(EPhase::Crater);
        break;

    case EPhase::Crater:
        for (int32 i = 0; i < CraterXY.Num(); ++i)
        {
            const float Z =
                FMath::IsNaN(SurfaceBefore[i]) ? static_cast<float>(kTerrainOrigin.Z) : SurfaceBefore[i];
            const bool bOk = Terrain->ApplyCrater(FVector(CraterXY[i].X, CraterXY[i].Y, Z), kCraterRadiusCm);
            UE_LOG(LogTemp, Display, TEXT("OBShowcase: ApplyCrater %d -> %s"), i,
                   bOk ? TEXT("accepted") : TEXT("rejected"));
        }
        Advance(EPhase::WaitForRemesh);
        break;

    case EPhase::WaitForRemesh:
        if (FramesInPhase > 30 && Terrain->GetPendingRebuildCount() == 0)
        {
            int32 Passed = 0;
            for (int32 i = 0; i < CraterXY.Num(); ++i)
            {
                FVector Hit;
                const bool bHit = TraceDown(CraterXY[i], Hit);
                // A crater of radius r centred on the surface lowers the surface at its centre by about r.
                const float Drop = bHit && !FMath::IsNaN(SurfaceBefore[i])
                                       ? SurfaceBefore[i] - static_cast<float>(Hit.Z)
                                       : 0.0f;
                const bool bPass = bHit && Drop > kCraterRadiusCm * 0.6f;
                Passed += bPass ? 1 : 0;
                UE_LOG(LogTemp, Display,
                       TEXT("OBShowcase: after crater at (%.0f, %.0f): %s z=%.1f drop=%.1f cm %s"),
                       CraterXY[i].X, CraterXY[i].Y, bHit ? TEXT("hit") : TEXT("NO HIT"), bHit ? Hit.Z : 0.0,
                       Drop, bPass ? TEXT("PASS") : TEXT("FAIL"));
            }
            UE_LOG(LogTemp, Display, TEXT("OBShowcase: collision follows craters: %d/%d"), Passed,
                   CraterXY.Num());
            Screenshot(TEXT("after.png"));
            Advance(EPhase::ShotAfter);
        }
        break;

    case EPhase::ShotAfter:
        if (FramesInPhase > 20)
        {
            UE_LOG(LogTemp, Display, TEXT("OBShowcase: done"));
            Advance(EPhase::Done);
            FPlatformMisc::RequestExit(false, TEXT("OBShowcase finished"));
        }
        break;

    case EPhase::Done:
        break;
    }
}
