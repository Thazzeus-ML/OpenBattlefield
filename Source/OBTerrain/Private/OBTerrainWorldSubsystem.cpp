// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
//
// STATUS: not yet compiled against Unreal Engine. See Docs/status.md.
#include "OBTerrainWorldSubsystem.h"

#include "Engine/World.h"

DEFINE_LOG_CATEGORY(LogOBTerrain);

void UOBTerrainWorldSubsystem::Deinitialize()
{
    ShutdownTerrain();
    Super::Deinitialize();
}

bool UOBTerrainWorldSubsystem::InitializeTerrain(const FOBTerrainWorldSettings& InSettings,
                                                 const FTransform& InTerrainOrigin)
{
    if (CoreWorld.IsValid())
    {
        UE_LOG(LogOBTerrain, Warning, TEXT("InitializeTerrain called while a terrain world already exists"));
        return false;
    }
    const ob::WorldConfig Config = InSettings.ToCoreConfig();
    const ob::ResultCode Valid = Config.Validate();
    if (ob::Failed(Valid))
    {
        UE_LOG(LogOBTerrain, Error, TEXT("Invalid terrain settings: %s"), ANSI_TO_TCHAR(ob::ToString(Valid)));
        return false;
    }
    Settings = InSettings;
    TerrainOrigin = InTerrainOrigin;
    CoreWorld = MakeUnique<ob::TerrainWorld>(Config);
    NextOperationId = 1;
    UE_LOG(LogOBTerrain, Log, TEXT("Terrain world %llu initialised (seed %llu, chunk %u, voxel %.1f cm)"),
           static_cast<unsigned long long>(Config.worldId), static_cast<unsigned long long>(Config.seed),
           Config.chunkSize, Settings.VoxelSizeCm);
    return true;
}

void UOBTerrainWorldSubsystem::ShutdownTerrain()
{
    CoreWorld.Reset();
}

bool UOBTerrainWorldSubsystem::HasTerrainAuthority() const
{
    const UWorld* World = GetWorld();
    return World != nullptr && World->GetNetMode() != NM_Client;
}

ob::VoxelCoord UOBTerrainWorldSubsystem::WorldToVoxel(const FVector& WorldLocation) const
{
    const FVector Local = TerrainOrigin.InverseTransformPosition(WorldLocation);
    return FOBCoordinates::ToVoxel(Local, GetVoxelSizeCm());
}

bool UOBTerrainWorldSubsystem::ApplyCraterAtLocation(FVector WorldLocation, float RadiusCm, FString& OutError)
{
    if (!CoreWorld.IsValid())
    {
        OutError = TEXT("terrain not initialised");
        return false;
    }
    if (!HasTerrainAuthority())
    {
        OutError = TEXT("terrain modification refused: no authority on this client");
        UE_LOG(LogOBTerrain, Warning, TEXT("%s"), *OutError);
        return false;
    }

    ob::TerrainOperation Op;
    Op.operationId = NextOperationId;
    Op.sequenceNumber = CoreWorld->LastSequence() + 1;
    Op.worldId = CoreWorld->Config().worldId;
    Op.type = ob::OperationType::SubtractSphere;
    Op.center = WorldToVoxel(WorldLocation);
    Op.radius = FOBCoordinates::RadiusToVoxels(RadiusCm, GetVoxelSizeCm());
    Op.material = ob::Material::Air;

    ob::OperationResult Result;
    if (!ApplyOperation(Op, Result))
    {
        OutError = FString::Printf(TEXT("operation rejected: %s"), ANSI_TO_TCHAR(ob::ToString(Result.code)));
        return false;
    }
    ++NextOperationId;
    return true;
}

bool UOBTerrainWorldSubsystem::ApplyOperation(const ob::TerrainOperation& Op, ob::OperationResult& OutResult)
{
    if (!CoreWorld.IsValid())
    {
        OutResult.code = ob::ResultCode::WorldNotInitialized;
        return false;
    }
    OutResult = CoreWorld->ApplyOperation(Op);
    if (!OutResult.Accepted())
    {
        UE_LOG(LogOBTerrain, Verbose, TEXT("Terrain operation %llu rejected: %s"),
               static_cast<unsigned long long>(Op.operationId), ANSI_TO_TCHAR(ob::ToString(OutResult.code)));
        return false;
    }
    if (Op.operationId >= NextOperationId)
    {
        NextOperationId = Op.operationId + 1;
    }
    return true;
}

FString UOBTerrainWorldSubsystem::GetStateHashString() const
{
    if (!CoreWorld.IsValid())
    {
        return TEXT("0x0000000000000000");
    }
    return FString::Printf(TEXT("0x%016llx"), static_cast<unsigned long long>(CoreWorld->ComputeStateHash()));
}

int64 UOBTerrainWorldSubsystem::GetWorldVersion() const
{
    return CoreWorld.IsValid() ? static_cast<int64>(CoreWorld->Version()) : 0;
}

void UOBTerrainWorldSubsystem::TakeDirtyChunks(TArray<FIntVector>& OutChunks)
{
    OutChunks.Reset();
    if (!CoreWorld.IsValid())
    {
        return;
    }
    std::vector<ob::ChunkCoord> Dirty;
    CoreWorld->TakeDirtyChunks(Dirty);
    OutChunks.Reserve(static_cast<int32>(Dirty.size()));
    for (const ob::ChunkCoord& C : Dirty)
    {
        OutChunks.Add(FOBCoordinates::ToIntVector(C));
    }
}

bool UOBTerrainWorldSubsystem::SaveSnapshot(TArray<uint8>& OutBytes) const
{
    OutBytes.Reset();
    if (!CoreWorld.IsValid())
    {
        return false;
    }
    ob::ByteWriter Writer;
    CoreWorld->SerializeSnapshot(Writer);
    OutBytes.Append(Writer.Buffer().data(), static_cast<int32>(Writer.Size()));
    return true;
}

bool UOBTerrainWorldSubsystem::LoadSnapshot(const TArray<uint8>& Bytes, FString& OutError)
{
    ob::ByteReader Reader(Bytes.GetData(), static_cast<size_t>(Bytes.Num()));
    std::unique_ptr<ob::TerrainWorld> Loaded;
    const ob::ResultCode Code = ob::TerrainWorld::DeserializeSnapshot(Reader, Loaded);
    if (ob::Failed(Code) || !Loaded)
    {
        OutError = FString::Printf(TEXT("snapshot rejected: %s"), ANSI_TO_TCHAR(ob::ToString(Code)));
        return false;
    }
    CoreWorld.Reset(Loaded.release());
    NextOperationId = 1;
    for (const ob::TerrainOperation& Op : CoreWorld->Journal())
    {
        if (Op.operationId >= NextOperationId)
        {
            NextOperationId = Op.operationId + 1;
        }
    }
    return true;
}
