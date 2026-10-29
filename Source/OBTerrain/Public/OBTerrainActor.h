// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
//
// STATUS: built and run on UE 5.8.2 (Windows). See Docs/status.md.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "OBTerrainTypes.h"

#include "OBTerrainActor.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;

/**
 * Reference presentation of a terrain world: one UProceduralMeshComponent per visible chunk, rebuilt
 * synchronously on the game thread from core snapshots (the "synchronous reference path" of
 * Docs/concurrency.md). Render and collision geometry come from the same section, so for any
 * published chunk they are always consistent with each other; a chunk that is still queued keeps its
 * previous (conservative) geometry for both.
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

    /** Upper bound on chunk mesh rebuilds per tick; the rest stay queued (and conservative). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "OpenBattlefield", meta = (ClampMin = "1"))
    int32 MaxChunkRebuildsPerTick = 8;

    /** Subtracts a sphere at a world location. Server/authority only. Returns false with a log on rejection.
     */
    UFUNCTION(BlueprintCallable, Category = "OpenBattlefield|Terrain")
    bool ApplyCrater(FVector WorldLocation, float RadiusCm);

    /** Number of chunks waiting for a mesh rebuild. */
    UFUNCTION(BlueprintCallable, Category = "OpenBattlefield|Terrain")
    int32 GetPendingRebuildCount() const { return PendingRebuilds.Num(); }

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Tick(float DeltaSeconds) override;

private:
    void EnqueueInitialChunks();
    void ProcessRebuildQueue();
    bool RebuildChunk(const FIntVector& ChunkCoord);
    UProceduralMeshComponent* GetOrCreateChunkMesh(const FIntVector& ChunkCoord);
    bool IsChunkVisible(const FIntVector& ChunkCoord) const;

    UPROPERTY(Transient)
    TMap<FIntVector, TObjectPtr<UProceduralMeshComponent>> ChunkMeshes;

    /** Core chunk version last published to each mesh component (version-checked publication). */
    TMap<FIntVector, uint64> PublishedVersions;

    /** FIFO of chunks needing a rebuild, deduplicated. */
    TArray<FIntVector> PendingRebuilds;
    TSet<FIntVector> PendingSet;
};
