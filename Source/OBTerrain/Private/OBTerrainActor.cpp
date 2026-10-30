// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
//
// STATUS: built and run on UE 5.8.2 (Windows). See Docs/status.md.
#include "OBTerrainActor.h"

#include "Containers/Queue.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "Tasks/Task.h"

#include "OBMeshConversion.h"
#include "OBTerrainWorldSubsystem.h"

/** A finished meshing job: Unreal-ready buffers for one chunk, tagged with its scheduler ticket. */
struct FOBRemeshResult
{
    FIntVector Chunk;
    uint64 Ticket = 0;
    uint64 Version = 0;
    FOBMeshBuffers Buffers;
};

struct FOBRemeshCompletions
{
    TQueue<FOBRemeshResult, EQueueMode::Mpsc> Queue;
};

namespace
{
/** Pure function of the snapshot: safe on any thread (Docs/concurrency.md, rule 1). */
void MeshSnapshot(const ob::ChunkMeshInput& Input, bool bGreedy, double VoxelSizeCm, bool bFlipWinding,
                  FOBMeshBuffers& Out)
{
    ob::MeshData MeshData;
    if (bGreedy)
    {
        ob::ChunkFaces Faces;
        ob::BuildChunkFaces(Input, Faces);
        ob::EmitGreedyChunkMesh(Faces, MeshData);
    }
    else
    {
        ob::BuildChunkMesh(Input, MeshData);
    }
    OBConvertChunkMesh(MeshData, VoxelSizeCm, bFlipWinding, Out);
}
} // namespace

AOBTerrainActor::AOBTerrainActor() : Completions(MakeShared<FOBRemeshCompletions, ESPMode::ThreadSafe>())
{
    PrimaryActorTick.bCanEverTick = true;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    bReplicates = false; // terrain replication is a future milestone (Docs/networking.md)
}

void AOBTerrainActor::BeginPlay()
{
    Super::BeginPlay();
    UOBTerrainWorldSubsystem* Subsystem =
        GetWorld() ? GetWorld()->GetSubsystem<UOBTerrainWorldSubsystem>() : nullptr;
    if (!Subsystem)
    {
        UE_LOG(LogOBTerrain, Error, TEXT("AOBTerrainActor: no terrain subsystem"));
        return;
    }
    if (!Subsystem->IsTerrainInitialized() && !Subsystem->InitializeTerrain(Settings, GetActorTransform()))
    {
        UE_LOG(LogOBTerrain, Error, TEXT("AOBTerrainActor: terrain initialisation failed"));
        return;
    }
    EnqueueInitialChunks();
}

void AOBTerrainActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // Queued work references chunks by coordinate only, and running jobs hold only their snapshot and the
    // old completion queue, so dropping both is always safe; their results are never published.
    Scheduler.Reset();
    Completions = MakeShared<FOBRemeshCompletions, ESPMode::ThreadSafe>();
    PublishedVersions.Reset();
    ChunkMeshes.Reset();
    if (UOBTerrainWorldSubsystem* Subsystem =
            GetWorld() ? GetWorld()->GetSubsystem<UOBTerrainWorldSubsystem>() : nullptr)
    {
        Subsystem->ShutdownTerrain();
    }
    Super::EndPlay(EndPlayReason);
}

bool AOBTerrainActor::IsChunkVisible(const FIntVector& C) const
{
    return FMath::Abs(C.X) <= VisibleChunkRadius && FMath::Abs(C.Y) <= VisibleChunkRadius &&
           FMath::Abs(C.Z) <= VisibleChunkRadiusZ;
}

void AOBTerrainActor::EnqueueInitialChunks()
{
    // Canonical (z, y, x) order so startup is deterministic.
    for (int32 Z = -VisibleChunkRadiusZ; Z <= VisibleChunkRadiusZ; ++Z)
    {
        for (int32 Y = -VisibleChunkRadius; Y <= VisibleChunkRadius; ++Y)
        {
            for (int32 X = -VisibleChunkRadius; X <= VisibleChunkRadius; ++X)
            {
                Scheduler.Request(FOBCoordinates::ToChunkCoord(FIntVector(X, Y, Z)));
            }
        }
    }
}

bool AOBTerrainActor::ApplyCrater(FVector WorldLocation, float RadiusCm)
{
    UOBTerrainWorldSubsystem* Subsystem =
        GetWorld() ? GetWorld()->GetSubsystem<UOBTerrainWorldSubsystem>() : nullptr;
    if (!Subsystem)
    {
        return false;
    }
    FString Error;
    if (!Subsystem->ApplyCraterAtLocation(WorldLocation, RadiusCm, Error))
    {
        UE_LOG(LogOBTerrain, Warning, TEXT("ApplyCrater failed: %s"), *Error);
        return false;
    }
    return true;
}

void AOBTerrainActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    UOBTerrainWorldSubsystem* Subsystem =
        GetWorld() ? GetWorld()->GetSubsystem<UOBTerrainWorldSubsystem>() : nullptr;
    if (!Subsystem || !Subsystem->IsTerrainInitialized())
    {
        return;
    }

    TArray<FIntVector> Dirty;
    Subsystem->TakeDirtyChunks(Dirty);
    for (const FIntVector& C : Dirty)
    {
        if (IsChunkVisible(C))
        {
            Scheduler.Request(FOBCoordinates::ToChunkCoord(C));
        }
    }
    // Publish first: a job completed since the last tick frees its chunk to start again this tick.
    PublishCompletedJobs();
    StartRemeshJobs();
    if (!bAsyncRemesh)
    {
        PublishCompletedJobs();
    }
}

void AOBTerrainActor::StartRemeshJobs()
{
    int32 Budget = MaxChunkRebuildsPerTick;
    ob::ChunkCoord Coord;
    uint64 Ticket = 0;
    while (Budget > 0 && Scheduler.TryStart(Coord, Ticket))
    {
        if (StartRemesh(FOBCoordinates::ToIntVector(Coord), Ticket))
        {
            --Budget;
        }
    }
}

void AOBTerrainActor::PublishCompletedJobs()
{
    FOBRemeshResult Result;
    while (Completions->Queue.Dequeue(Result))
    {
        // Only the chunk's current job may publish: results from before EndPlay or a reset are dropped.
        if (Scheduler.Complete(FOBCoordinates::ToChunkCoord(Result.Chunk), Result.Ticket))
        {
            Publish(Result);
        }
    }
}

UProceduralMeshComponent* AOBTerrainActor::GetOrCreateChunkMesh(const FIntVector& C)
{
    if (TObjectPtr<UProceduralMeshComponent>* Existing = ChunkMeshes.Find(C))
    {
        return Existing->Get();
    }
    UOBTerrainWorldSubsystem* Subsystem = GetWorld()->GetSubsystem<UOBTerrainWorldSubsystem>();
    const uint32_t ChunkSize = Subsystem->GetSettings().ChunkSize > 0
                                   ? static_cast<uint32_t>(Subsystem->GetSettings().ChunkSize)
                                   : 32u;
    UProceduralMeshComponent* Mesh =
        NewObject<UProceduralMeshComponent>(this, *FString::Printf(TEXT("Chunk_%d_%d_%d"), C.X, C.Y, C.Z));
    Mesh->RegisterComponent();
    Mesh->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
    Mesh->SetRelativeLocation(FOBCoordinates::ChunkOriginCm(FOBCoordinates::ToChunkCoord(C), ChunkSize,
                                                            Subsystem->GetVoxelSizeCm()));
    Mesh->bUseAsyncCooking = false; // synchronous reference path: collision is ready when the section is
    Mesh->SetCollisionEnabled(bGenerateCollision ? ECollisionEnabled::QueryAndPhysics
                                                 : ECollisionEnabled::NoCollision);
    if (TerrainMaterial)
    {
        Mesh->SetMaterial(0, TerrainMaterial);
    }
    ChunkMeshes.Add(C, Mesh);
    return Mesh;
}

bool AOBTerrainActor::StartRemesh(const FIntVector& C, uint64 Ticket)
{
    UOBTerrainWorldSubsystem* Subsystem = GetWorld()->GetSubsystem<UOBTerrainWorldSubsystem>();
    ob::TerrainWorld* Core = Subsystem ? Subsystem->GetCoreWorld() : nullptr;
    const ob::ChunkCoord Coord = FOBCoordinates::ToChunkCoord(C);
    const double SnapshotStart = FPlatformTime::Seconds();
    ob::Chunk* Chunk = nullptr;
    ob::ChunkMeshInput Input;
    bool bNeeded = Core && ob::Succeeded(Core->EnsureChunk(Coord, Chunk));
    if (bNeeded)
    {
        // The whole chunk is remeshed; the region only says whether anything changed.
        const bool bDirty = Chunk->ConsumeDirtyRegion().any;
        // Skip if what is on screen already matches the core. A dirty region with an unchanged version
        // means a neighbour changed this chunk's boundary faces.
        const uint64* Published = PublishedVersions.Find(C);
        bNeeded = bDirty || !Published || *Published != Chunk->Version() || !ChunkMeshes.Contains(C);
    }
    // The snapshot is taken here, on the game thread, which owns the core world.
    const bool bSnapshotTaken = bNeeded && ob::Succeeded(Core->BuildMeshInput(Coord, Input));
    SnapshotSeconds += FPlatformTime::Seconds() - SnapshotStart;
    if (!bSnapshotTaken)
    {
        Scheduler.Complete(Coord, Ticket); // nothing to publish; frees the chunk
        return false;
    }

    const bool bGreedy = bGreedyMeshing;
    const bool bFlip = bFlipWinding;
    const double VoxelSizeCm = Subsystem->GetVoxelSizeCm();
    if (!bAsyncRemesh)
    {
        const double MeshStart = FPlatformTime::Seconds();
        FOBRemeshResult Result{C, Ticket, Input.version, {}};
        MeshSnapshot(Input, bGreedy, VoxelSizeCm, bFlip, Result.Buffers);
        InlineMeshSeconds += FPlatformTime::Seconds() - MeshStart;
        Completions->Queue.Enqueue(MoveTemp(Result));
        return true;
    }
    // The job owns its snapshot and a reference to the completion queue; it never touches the actor or
    // the core world (Docs/concurrency.md, rules 1-3).
    UE::Tasks::Launch(UE_SOURCE_LOCATION,
                      [Queue = Completions, Input = MoveTemp(Input), C, Ticket, bGreedy, bFlip, VoxelSizeCm]()
                      {
                          FOBRemeshResult Result{C, Ticket, Input.version, {}};
                          MeshSnapshot(Input, bGreedy, VoxelSizeCm, bFlip, Result.Buffers);
                          Queue->Queue.Enqueue(MoveTemp(Result));
                      });
    return true;
}

void AOBTerrainActor::Publish(FOBRemeshResult& Result)
{
    // Game thread only: Unreal mesh and collision state (rule 3).
    const double PublishStart = FPlatformTime::Seconds();
    UProceduralMeshComponent* Mesh = GetOrCreateChunkMesh(Result.Chunk);
    FOBMeshBuffers& Buffers = Result.Buffers;
    if (Buffers.Vertices.Num() == 0)
    {
        Mesh->ClearMeshSection(0);
    }
    else
    {
        Mesh->CreateMeshSection_LinearColor(0, Buffers.Vertices, Buffers.Triangles, Buffers.Normals,
                                            Buffers.UV0, Buffers.VertexColors, Buffers.Tangents,
                                            bGenerateCollision);
    }
    // The version the snapshot was taken from, never the current one, so a result computed against
    // version N is never mistaken for version N+1.
    PublishedVersions.Add(Result.Chunk, Result.Version);
    ++PublishedChunkCount;
    PublishSeconds += FPlatformTime::Seconds() - PublishStart;
}
