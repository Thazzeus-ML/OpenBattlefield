// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
//
// STATUS: not yet compiled against Unreal Engine. See Docs/status.md.
#include "OBTerrainActor.h"

#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"

#include "OBMeshConversion.h"
#include "OBTerrainWorldSubsystem.h"

AOBTerrainActor::AOBTerrainActor()
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
    // Pending work references chunks by coordinate only, so dropping the queue is always safe.
    PendingRebuilds.Reset();
    PendingSet.Reset();
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
                const FIntVector C(X, Y, Z);
                if (PendingSet.Contains(C))
                {
                    continue;
                }
                PendingSet.Add(C);
                PendingRebuilds.Add(C);
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
        if (!IsChunkVisible(C) || PendingSet.Contains(C))
        {
            continue;
        }
        PendingSet.Add(C);
        PendingRebuilds.Add(C);
    }
    ProcessRebuildQueue();
}

void AOBTerrainActor::ProcessRebuildQueue()
{
    int32 Budget = MaxChunkRebuildsPerTick;
    while (Budget > 0 && PendingRebuilds.Num() > 0)
    {
        const FIntVector C = PendingRebuilds[0];
        PendingRebuilds.RemoveAt(0);
        PendingSet.Remove(C);
        if (RebuildChunk(C))
        {
            --Budget;
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

bool AOBTerrainActor::RebuildChunk(const FIntVector& C)
{
    UOBTerrainWorldSubsystem* Subsystem = GetWorld()->GetSubsystem<UOBTerrainWorldSubsystem>();
    ob::TerrainWorld* Core = Subsystem ? Subsystem->GetCoreWorld() : nullptr;
    if (!Core)
    {
        return false;
    }
    const ob::ChunkCoord Coord = FOBCoordinates::ToChunkCoord(C);
    ob::Chunk* Chunk = nullptr;
    if (ob::Failed(Core->EnsureChunk(Coord, Chunk)))
    {
        return false;
    }
    // The reference path rebuilds the whole chunk; the region only says whether anything changed.
    const bool bDirty = Chunk->ConsumeDirtyRegion().any;

    // Version-checked publication: skip if what is on screen already matches the core. A dirty
    // region with an unchanged version means a neighbour changed this chunk's boundary faces.
    if (const uint64* Published = PublishedVersions.Find(C))
    {
        if (!bDirty && *Published == Chunk->Version() && ChunkMeshes.Contains(C))
        {
            return false;
        }
    }

    ob::ChunkMeshInput Input;
    if (ob::Failed(Core->BuildMeshInput(Coord, Input)))
    {
        return false;
    }
    ob::MeshData MeshData;
    ob::BuildChunkMesh(Input, MeshData);

    FOBMeshBuffers Buffers;
    OBConvertChunkMesh(MeshData, Subsystem->GetVoxelSizeCm(), bFlipWinding, Buffers);

    UProceduralMeshComponent* Mesh = GetOrCreateChunkMesh(C);
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
    // Input.version is the version the snapshot was taken from; publishing records exactly that.
    PublishedVersions.Add(C, Input.version);
    return true;
}
