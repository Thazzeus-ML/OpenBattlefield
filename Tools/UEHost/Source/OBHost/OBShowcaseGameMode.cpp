#include "OBShowcaseGameMode.h"

#include "AIController.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
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
    // Stills only: no motion blur when the camera is moved between shots.
    Camera->GetCameraComponent()->PostProcessSettings.bOverride_MotionBlurAmount = true;
    Camera->GetCameraComponent()->PostProcessSettings.MotionBlurAmount = 0.0f;
    if (APlayerController* PC = World->GetFirstPlayerController())
        PC->SetViewTarget(Camera);

    // Craters: one inside a chunk, one on a chunk face, one on a chunk corner (8 m chunks).
    CraterXY = {FVector2D(400.0, 400.0), FVector2D(800.0, -300.0), FVector2D(-800.0, -800.0)};
    PhaseStart = World->GetTimeSeconds();
    UE_LOG(LogTemp, Display, TEXT("OBShowcase: terrain actor spawned"));
}

bool AOBShowcaseGameMode::TraceDown(const FVector2D& XY, FVector& OutHit, const AActor* Ignore) const
{
    const FVector Start(XY.X, XY.Y, kTerrainOrigin.Z + 5000.0);
    const FVector End(XY.X, XY.Y, kTerrainOrigin.Z - 5000.0);
    FHitResult Hit;
    FCollisionQueryParams Query(TEXT("OBShowcaseTrace"), /*bTraceComplex=*/true);
    for (const FWalk& W : Walks)
        if (W.Walker)
            Query.AddIgnoredActor(W.Walker);
    if (Ignore)
        Query.AddIgnoredActor(Ignore);
    if (!GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Query))
        return false;
    if (!Hit.GetActor() || Hit.GetActor() != Terrain)
        return false;
    OutHit = Hit.ImpactPoint;
    return true;
}

ACharacter* AOBShowcaseGameMode::SpawnWalker(const FVector& FeetLocation)
{
    UWorld* World = GetWorld();
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride =
        ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
    ACharacter* Walker =
        World->SpawnActor<ACharacter>(ACharacter::StaticClass(), FeetLocation, FRotator::ZeroRotator, Params);
    if (!Walker)
        return nullptr;
    // Drop the default capsule (r 34, half height 88) from 30 cm above the given point, so it lands
    // instead of starting embedded in a voxel step.
    const float HalfHeight = Walker->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    Walker->SetActorLocation(FeetLocation + FVector(0.0, 0.0, HalfHeight + 30.0));
    // A visible stand-in for the capsule: the engine cylinder (100 cm across, 100 cm tall).
    if (UStaticMesh* Cylinder =
            LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")))
    {
        UStaticMeshComponent* Body = NewObject<UStaticMeshComponent>(Walker);
        Body->SetStaticMesh(Cylinder);
        Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Body->SetupAttachment(Walker->GetCapsuleComponent());
        Body->SetRelativeScale3D(FVector(0.68, 0.68, HalfHeight * 2.0 / 100.0));
        Body->RegisterComponent();
    }
    AAIController* Brain = World->SpawnActor<AAIController>();
    Brain->Possess(Walker);
    return Walker;
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
                FloorAfter.Add(bHit ? static_cast<float>(Hit.Z) : NAN);
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
            Advance(EPhase::WalkSpawn);
        break;

    case EPhase::WalkSpawn:
    {
        // Walk into crater 0 (inside a chunk) from -x, and into crater 1 (centred on the chunk face at
        // x = 800 cm) from +x, so that walk ends on the seam between two chunks' collision.
        const FVector2D Offsets[2] = {FVector2D(-550.0, 0.0), FVector2D(550.0, 0.0)};
        for (int32 c = 0; c < 2; ++c)
        {
            FWalk W;
            W.Crater = c;
            W.From = CraterXY[c] + Offsets[c];
            W.FloorZ = FloorAfter[c];
            FVector Ground;
            if (FMath::IsNaN(W.FloorZ) || !TraceDown(W.From, Ground))
            {
                UE_LOG(LogTemp, Error, TEXT("OBShowcase: walk %d: no ground at the start point"), c);
                continue;
            }
            W.Walker = SpawnWalker(Ground);
            Walks.Add(W);
        }
        Advance(EPhase::Walk);
        break;
    }

    case EPhase::Walk:
    {
        bool bAllArrived = true;
        for (FWalk& W : Walks)
        {
            if (!W.Walker)
                continue;
            const FVector Loc = W.Walker->GetActorLocation();
            const float Bottom =
                static_cast<float>(Loc.Z) - W.Walker->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
            W.MinBottomZ = FMath::Min(W.MinBottomZ, Bottom);
            if (FramesInPhase % 30 == 1)
            {
                const UCharacterMovementComponent* Move = W.Walker->GetCharacterMovement();
                UE_LOG(LogTemp, Display,
                       TEXT("OBShowcase: walk %d t=%.1f mode=%s pos=(%.0f, %.0f, %.0f) vel=(%.0f, %.0f, "
                            "%.0f) floor=%d"),
                       W.Crater, Now - PhaseStart, *Move->GetMovementName(), Loc.X, Loc.Y, Bottom,
                       Move->Velocity.X, Move->Velocity.Y, Move->Velocity.Z,
                       Move->CurrentFloor.IsWalkableFloor() ? 1 : 0);
            }
            const FVector2D ToCentre = CraterXY[W.Crater] - FVector2D(Loc.X, Loc.Y);
            const UCharacterMovementComponent* Move = W.Walker->GetCharacterMovement();
            if (Now - PhaseStart > 0.5 && ToCentre.Size() > 40.0) // let it land first
                W.Walker->AddMovementInput(FVector(ToCentre.X, ToCentre.Y, 0.0).GetSafeNormal(), 1.0f);
            // Arrived means standing still on the ground near the centre, not passing over it mid-fall.
            const bool bArrived =
                ToCentre.Size() <= 40.0 && Move->IsMovingOnGround() && Move->Velocity.Size() < 5.0;
            bAllArrived = bAllArrived && bArrived;
        }
        if ((bAllArrived && Now - PhaseStart > 1.0) || Now - PhaseStart > 15.0)
        {
            for (const FWalk& W : Walks)
            {
                if (!W.Walker)
                    continue;
                const FVector Loc = W.Walker->GetActorLocation();
                const float Bottom =
                    static_cast<float>(Loc.Z) - W.Walker->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
                const float Horizontal =
                    static_cast<float>((CraterXY[W.Crater] - FVector2D(Loc.X, Loc.Y)).Size());
                const bool bOnGround = W.Walker->GetCharacterMovement()->IsMovingOnGround();
                // Standing on the floor: near the centre, on ground, feet within one voxel of the floor,
                // and never lower than that (no fall through).
                const bool bPass = Horizontal < 80.0f && bOnGround && FMath::Abs(Bottom - W.FloorZ) < 30.0f &&
                                   W.MinBottomZ > W.FloorZ - 30.0f;
                WalkPassed += bPass ? 1 : 0;
                UE_LOG(LogTemp, Display,
                       TEXT("OBShowcase: walk into crater %d: %.0f cm from centre, feet z=%.1f floor z=%.1f "
                            "lowest "
                            "z=%.1f %s %s"),
                       W.Crater, Horizontal, Bottom, W.FloorZ, W.MinBottomZ,
                       bOnGround ? TEXT("on ground") : TEXT("NOT on ground"),
                       bPass ? TEXT("PASS") : TEXT("FAIL"));
            }
            UE_LOG(LogTemp, Display, TEXT("OBShowcase: character walks: %d/%d"), WalkPassed, Walks.Num());
            // Close-up of the crater on the chunk face, where the walk ended on the seam.
            if (Walks.Num() > 1 && !FMath::IsNaN(FloorAfter[1]))
            {
                const FVector Centre(CraterXY[1].X, CraterXY[1].Y, FloorAfter[1] + 90.0);
                const FVector Eye =
                    Centre + FVector(-250.0, -350.0, 1900.0); // steep, above the hills (up to z 3600)
                Camera->SetActorLocationAndRotation(Eye, (Centre - Eye).Rotation());
            }
            Advance(EPhase::ShotWalk);
        }
        break;
    }

    case EPhase::ShotWalk:
        if (FramesInPhase == 15) // let the moved camera settle (exposure, streaming) first
            Screenshot(TEXT("walk.png"));
        if (FramesInPhase > 35)
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
