// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
//
// STATUS: built and run on UE 5.8.2 (Windows). See Docs/status.md.
#pragma once

#include "CoreMinimal.h"

#include "OpenBattlefieldCore/OpenBattlefieldCore.h"

// UHT requires the generated header to be the last include (kept in its own block so
// clang-format does not sort it).
#include "OBTerrainTypes.generated.h"

/**
 * Editor-facing description of a terrain world. Converted to ob::WorldConfig by ToCoreConfig().
 * Units follow Unreal (centimetres) here; the core works in integer voxels (see Docs/coordinates.md).
 */
USTRUCT(BlueprintType)
struct OBTERRAIN_API FOBTerrainWorldSettings
{
    GENERATED_BODY()

    /** Identifies the world; terrain operations must carry the same id. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenBattlefield")
    int64 WorldId = 1;

    /** Procedural generation seed. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenBattlefield")
    int64 Seed = 20240601;

    /** Voxels per chunk edge. Power of two in [8, 128]. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenBattlefield",
              meta = (ClampMin = "8", ClampMax = "128"))
    int32 ChunkSize = 32;

    /** Edge length of one voxel in centimetres. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenBattlefield", meta = (ClampMin = "1.0"))
    float VoxelSizeCm = 25.0f;

    /** Inclusive chunk-space bounds. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenBattlefield")
    FIntVector MinChunk = FIntVector(-16, -16, -4);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenBattlefield")
    FIntVector MaxChunk = FIntVector(15, 15, 3);

    /** Largest crater radius accepted by the server, in voxels. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenBattlefield", meta = (ClampMin = "1"))
    int32 MaxOperationRadiusVoxels = 64;

    /** Mean surface height in voxels. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenBattlefield|Generation")
    int32 BaseHeight = 0;

    /** Peak-to-mean surface amplitude in voxels. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenBattlefield|Generation",
              meta = (ClampMin = "0"))
    int32 HeightAmplitude = 24;

    /** Everything at or below this height (voxels) is indestructible bedrock. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenBattlefield|Generation")
    int32 BedrockHeight = -100;

    /** log2 of the largest noise feature spacing in voxels. Add 1 per halving of VoxelSizeCm to keep
     *  the same physical terrain (with HeightAmplitude, TopsoilDepth, SoilDepth and BedrockHeight
     *  doubled). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenBattlefield|Generation",
              meta = (ClampMin = "0", ClampMax = "30"))
    int32 NoiseScaleLog2 = 6;

    /** Number of value-noise octaves. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenBattlefield|Generation",
              meta = (ClampMin = "1", ClampMax = "8"))
    int32 NoiseOctaves = 3;

    /** Depth of the grass layer in voxels. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenBattlefield|Generation",
              meta = (ClampMin = "0"))
    int32 TopsoilDepth = 1;

    /** Depth of the soil layer below the grass in voxels. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenBattlefield|Generation",
              meta = (ClampMin = "0"))
    int32 SoilDepth = 10;

    ob::WorldConfig ToCoreConfig() const
    {
        ob::WorldConfig Config;
        Config.worldId = static_cast<ob::WorldId>(WorldId);
        Config.seed = static_cast<uint64_t>(Seed);
        Config.chunkSize = static_cast<uint32_t>(ChunkSize);
        Config.voxelSizeMm = static_cast<uint32_t>(FMath::RoundToInt(VoxelSizeCm * 10.0f));
        Config.minChunk = ob::ChunkCoord(MinChunk.X, MinChunk.Y, MinChunk.Z);
        Config.maxChunk = ob::ChunkCoord(MaxChunk.X, MaxChunk.Y, MaxChunk.Z);
        Config.maxOperationRadius = static_cast<uint32_t>(MaxOperationRadiusVoxels);
        Config.baseHeight = BaseHeight;
        Config.heightAmplitude = HeightAmplitude;
        Config.bedrockHeight = BedrockHeight;
        Config.noiseScaleLog2 = static_cast<uint32_t>(NoiseScaleLog2);
        Config.noiseOctaves = static_cast<uint32_t>(NoiseOctaves);
        Config.topsoilDepth = static_cast<uint32_t>(TopsoilDepth);
        Config.soilDepth = static_cast<uint32_t>(SoilDepth);
        return Config;
    }
};

/**
 * Conversions between Unreal space (centimetres, relative to the terrain actor) and core voxel space.
 * Voxel (0,0,0) occupies [0, VoxelSizeCm)^3 relative to the terrain origin. Both spaces are Z-up.
 */
struct OBTERRAIN_API FOBCoordinates
{
    static ob::VoxelCoord ToVoxel(const FVector& LocalCm, double VoxelSizeCm)
    {
        return ob::VoxelCoord(static_cast<int64_t>(FMath::FloorToDouble(LocalCm.X / VoxelSizeCm)),
                              static_cast<int64_t>(FMath::FloorToDouble(LocalCm.Y / VoxelSizeCm)),
                              static_cast<int64_t>(FMath::FloorToDouble(LocalCm.Z / VoxelSizeCm)));
    }

    static FVector VoxelMinCornerCm(const ob::VoxelCoord& Voxel, double VoxelSizeCm)
    {
        return FVector(static_cast<double>(Voxel.x) * VoxelSizeCm, static_cast<double>(Voxel.y) * VoxelSizeCm,
                       static_cast<double>(Voxel.z) * VoxelSizeCm);
    }

    static FVector ChunkOriginCm(const ob::ChunkCoord& Chunk, uint32_t ChunkSize, double VoxelSizeCm)
    {
        return VoxelMinCornerCm(ob::ChunkOrigin(Chunk, ChunkSize), VoxelSizeCm);
    }

    static uint32_t RadiusToVoxels(float RadiusCm, double VoxelSizeCm)
    {
        const int32 R = FMath::RoundToInt(static_cast<float>(RadiusCm / VoxelSizeCm));
        return static_cast<uint32_t>(FMath::Max(1, R));
    }

    static FIntVector ToIntVector(const ob::ChunkCoord& C) { return FIntVector(C.x, C.y, C.z); }
    static ob::ChunkCoord ToChunkCoord(const FIntVector& V) { return ob::ChunkCoord(V.X, V.Y, V.Z); }
};
