// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
//
// STATUS: built and run on UE 5.8.2 (Windows). See Docs/status.md.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "OBTerrainTypes.h"
#include "OpenBattlefieldCore/ObRemeshScheduler.h"

#include "OBTerrainActor.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;
struct FOBRemeshCompletions;
struct FOBRemeshResult;

/**
 * Reference presentation of a terrain world: one UProceduralMeshComponent per visible chunk. The game
 * thread takes an immutable snapshot of each changed chunk; meshing runs on a worker task and the result
 * is published back on the game thread only if it is still the newest one for that chunk
 * (ob::RemeshScheduler, Docs/concurrency.md). Render and collision geometry come from the same section,
 * so for any published chunk they are always consistent with each other; a chunk that is queued or being
 * meshed keeps its previous (conservative) geometry for both.
 *
 * Place one in a level, set Settings, and call ApplyCrater from gameplay code or Blueprint.
 */
UCLASS()
class OBTERRAIN_API AOBTerrainActor : public AActor
{
    GENERATED_BODY()

public:
    AOBTerrainActor();

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "OpenBattlefield")
    FOBTerrainWorldSettings Settings;

    /** Chunks built in each horizontal direction around the actor at BeginPlay. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "OpenBattlefield", meta = (ClampMin = "0"))
    int32 VisibleChunkRadius = 3;

    /** Chunks built in each vertical direction around chunk z = 0. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "OpenBattlefield", meta = (ClampMin = "0"))
    int32 VisibleChunkRadiusZ = 1;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "OpenBattlefield")
    TObjectPtr<UMaterialInterface> TerrainMaterial = nullptr;

    /** Generate simple collision from the chunk mesh sections. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "OpenBattlefield")
    bool bGenerateCollision = true;

    /**
     * Reverse triangle winding when converting core meshes. The core emits counter-clockwise faces in a
     * right-handed frame and its coordinates map directly onto Unreal's left-handed axes, which mirrors
     * the winding, so this must stay true (verified in UE 5.8: false renders the terrain inside out).
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "OpenBattlefield")
    bool bFlipWinding = true;

    /**
     * Merge coplanar faces of the same material into larger quads (ob::EmitGreedyChunkMesh): about 10x
     * fewer triangles on terrain, same surface and collision. Merged quads meet smaller ones with
     * T-junctions, which can show as single-pixel cracks; turn this off to emit one quad per voxel face.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "OpenBattlefield")
    bool bGreedyMeshing = true;

    /**
     * Mesh changed chunks on worker tasks (true) or synchronously on the game thread (false, the reference
     * path). Either way geometry is published on the game thread.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "OpenBattlefield")
    bool bAsyncRemesh = true;

    /** Upper bound on chunk snapshots started per tick; the rest stay queued (and conservative). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "OpenBattlefield", meta = (ClampMin = "1"))
    int32 MaxChunkRebuildsPerTick = 8;

    /** Subtracts a sphere at a world location. Server/authority only. Returns false with a log on rejection.
     */
    UFUNCTION(BlueprintCallable, Category = "OpenBattlefield|Terrain")
    bool ApplyCrater(FVector WorldLocation, float RadiusCm);

    /** Number of chunks waiting for a mesh rebuild or being meshed. */
    UFUNCTION(BlueprintCallable, Category = "OpenBattlefield|Terrain")
    int32 GetPendingRebuildCount() const { return static_cast<int32>(Scheduler.PendingCount()); }

    /** Chunk meshes published since BeginPlay; grows by one per published result. */
    UFUNCTION(BlueprintCallable, Category = "OpenBattlefield|Terrain")
    int64 GetPublishedChunkCount() const { return PublishedChunkCount; }

    /** Game-thread time spent taking chunk snapshots since BeginPlay (seconds). */
    double GetSnapshotSeconds() const { return SnapshotSeconds; }

    /** Game-thread time spent meshing (synchronous path only) since BeginPlay (seconds). */
    double GetInlineMeshSeconds() const { return InlineMeshSeconds; }

    /** Game-thread time spent publishing mesh sections, collision cooking included, since BeginPlay. */
    double GetPublishSeconds() const { return PublishSeconds; }

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Tick(float DeltaSeconds) override;

private:
    void EnqueueInitialChunks();
    void StartRemeshJobs();
    void PublishCompletedJobs();
    /** Snapshots a chunk and meshes it (on a worker or inline); false if it needed no work. */
    bool StartRemesh(const FIntVector& ChunkCoord, uint64 Ticket);
    void Publish(FOBRemeshResult& Result);
    UProceduralMeshComponent* GetOrCreateChunkMesh(const FIntVector& ChunkCoord);
    bool IsChunkVisible(const FIntVector& ChunkCoord) const;

    UPROPERTY(Transient)
    TMap<FIntVector, TObjectPtr<UProceduralMeshComponent>> ChunkMeshes;

    /** Core chunk version last published to each mesh component. */
    TMap<FIntVector, uint64> PublishedVersions;

    int64 PublishedChunkCount = 0;
    double SnapshotSeconds = 0.0;
    double InlineMeshSeconds = 0.0;
    double PublishSeconds = 0.0;

    /** Which chunks wait for a remesh, which are being meshed, and which results may be published. */
    ob::RemeshScheduler Scheduler;

    /**
     * Worker jobs push results here (defined in the .cpp). Jobs hold a reference, never the actor, so a
     * job that finishes after EndPlay writes into a queue nobody reads (EndPlay swaps in a fresh one).
     */
    TSharedPtr<FOBRemeshCompletions, ESPMode::ThreadSafe> Completions;
};
