// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
//
// STATUS: built and run on UE 5.8.2 (Windows). See Docs/status.md.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Templates/UniquePtr.h"

#include "OBTerrainTypes.h"
#include "OpenBattlefieldCore/OpenBattlefieldCore.h"

#include "OBTerrainWorldSubsystem.generated.h"

OBTERRAIN_API DECLARE_LOG_CATEGORY_EXTERN(LogOBTerrain, Log, All);

/**
 * Owns the authoritative ob::TerrainWorld for a UWorld.
 *
 * Threading: all access happens on the game thread (the core's single-owner contract). Derived
 * data for other threads must go through ob::TerrainWorld::BuildMeshInput snapshots.
 *
 * Authority: terrain operations are only applied where the world has authority (standalone,
 * listen server, dedicated server). On a pure client ApplyCraterAtLocation refuses and logs; client
 * requests and replication are a future milestone (Docs/networking.md).
 */
UCLASS()
class OBTERRAIN_API UOBTerrainWorldSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Deinitialize() override;

    /** Creates the core world. Fails (and logs) if the settings are invalid or a world already exists. */
    bool InitializeTerrain(const FOBTerrainWorldSettings& InSettings, const FTransform& InTerrainOrigin);

    /** Destroys the core world and all derived bookkeeping. */
    void ShutdownTerrain();

    bool IsTerrainInitialized() const { return CoreWorld.IsValid(); }
    ob::TerrainWorld* GetCoreWorld() { return CoreWorld.Get(); }
    const ob::TerrainWorld* GetCoreWorld() const { return CoreWorld.Get(); }
    const FOBTerrainWorldSettings& GetSettings() const { return Settings; }
    const FTransform& GetTerrainOrigin() const { return TerrainOrigin; }
    double GetVoxelSizeCm() const { return static_cast<double>(Settings.VoxelSizeCm); }

    /** True when this world may mutate authoritative terrain state. */
    bool HasTerrainAuthority() const;

    /**
     * Server-side entry point: subtracts a sphere centred at a world-space location. Issues the
     * operation id and sequence number, validates through the core and returns false with a reason if
     * the core rejects it.
     */
    UFUNCTION(BlueprintCallable, Category = "OpenBattlefield|Terrain")
    bool ApplyCraterAtLocation(FVector WorldLocation, float RadiusCm, FString& OutError);

    /** Applies a fully specified operation (ids included). Used by replay, tests and future networking. */
    bool ApplyOperation(const ob::TerrainOperation& Op, ob::OperationResult& OutResult);

    /** Canonical state hash of the authoritative terrain as a hex string. */
    UFUNCTION(BlueprintCallable, Category = "OpenBattlefield|Terrain")
    FString GetStateHashString() const;

    UFUNCTION(BlueprintCallable, Category = "OpenBattlefield|Terrain")
    int64 GetWorldVersion() const;

    /** Moves the pending dirty chunk set out of the core (sorted, canonical order). */
    void TakeDirtyChunks(TArray<FIntVector>& OutChunks);

    /** Serializes the full snapshot (config + journal + modified chunks). */
    bool SaveSnapshot(TArray<uint8>& OutBytes) const;

    /** Replaces the current world with a deserialized snapshot. */
    bool LoadSnapshot(const TArray<uint8>& Bytes, FString& OutError);

    /** Converts a world-space location into core voxel coordinates. */
    ob::VoxelCoord WorldToVoxel(const FVector& WorldLocation) const;

private:
    TUniquePtr<ob::TerrainWorld> CoreWorld;
    FOBTerrainWorldSettings Settings;
    FTransform TerrainOrigin = FTransform::Identity;
    uint64 NextOperationId = 1;
};
