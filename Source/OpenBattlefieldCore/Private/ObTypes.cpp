// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
#include "OpenBattlefieldCore/ObTypes.h"
#include "OpenBattlefieldCore/OpenBattlefieldCore.h"

namespace ob
{

const char* ToString(ResultCode code)
{
    switch (code)
    {
    case ResultCode::Ok:
        return "Ok";
    case ResultCode::InvalidConfig:
        return "InvalidConfig";
    case ResultCode::InvalidChunkSize:
        return "InvalidChunkSize";
    case ResultCode::InvalidWorldBounds:
        return "InvalidWorldBounds";
    case ResultCode::InvalidOperationType:
        return "InvalidOperationType";
    case ResultCode::OperationSchemaVersionMismatch:
        return "OperationSchemaVersionMismatch";
    case ResultCode::WorldIdMismatch:
        return "WorldIdMismatch";
    case ResultCode::InvalidRadius:
        return "InvalidRadius";
    case ResultCode::RadiusTooLarge:
        return "RadiusTooLarge";
    case ResultCode::CenterOutOfBounds:
        return "CenterOutOfBounds";
    case ResultCode::ArithmeticOverflow:
        return "ArithmeticOverflow";
    case ResultCode::InvalidMaterial:
        return "InvalidMaterial";
    case ResultCode::StaleSequence:
        return "StaleSequence";
    case ResultCode::DuplicateOperation:
        return "DuplicateOperation";
    case ResultCode::InvalidOperationId:
        return "InvalidOperationId";
    case ResultCode::ChunkOutOfBounds:
        return "ChunkOutOfBounds";
    case ResultCode::ChunkNotLoaded:
        return "ChunkNotLoaded";
    case ResultCode::ChunkModified:
        return "ChunkModified";
    case ResultCode::SerializationTruncated:
        return "SerializationTruncated";
    case ResultCode::SerializationCorrupt:
        return "SerializationCorrupt";
    case ResultCode::SerializationVersionMismatch:
        return "SerializationVersionMismatch";
    case ResultCode::SerializationBadMagic:
        return "SerializationBadMagic";
    case ResultCode::SerializationLimitExceeded:
        return "SerializationLimitExceeded";
    case ResultCode::WorldNotInitialized:
        return "WorldNotInitialized";
    case ResultCode::ReplayDiverged:
        return "ReplayDiverged";
    }
    return "UnknownResultCode";
}

const char* ToString(Material material)
{
    switch (material)
    {
    case Material::Air:
        return "Air";
    case Material::Soil:
        return "Soil";
    case Material::Grass:
        return "Grass";
    case Material::Rock:
        return "Rock";
    case Material::Bedrock:
        return "Bedrock";
    case Material::Count:
        break;
    }
    return "UnknownMaterial";
}

const char* CoreVersionString()
{
    return "0.1.0";
}

} // namespace ob
