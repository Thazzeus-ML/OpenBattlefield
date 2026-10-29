// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
#pragma once

#include "OpenBattlefieldCore/ObTypes.h"

namespace ob
{

/// Monotonic counters maintained by a TerrainWorld. Cheap enough to always be on.
struct WorldStats
{
    uint64_t chunksGenerated = 0;
    uint64_t chunksUnloaded = 0;
    uint64_t chunksDeserialized = 0;
    uint64_t operationsAccepted = 0;
    uint64_t operationsRejected = 0;
    uint64_t operationsNoOp = 0;
    uint64_t voxelsChanged = 0;
    uint64_t voxelsVisited = 0;
    uint64_t chunkModifications = 0;
    uint64_t duplicateOperations = 0;
    uint64_t staleOperations = 0;
};

enum class EventKind : uint8_t
{
    ChunkGenerated,
    ChunkUnloaded,
    OperationAccepted,
    OperationRejected,
};

struct EventData
{
    EventKind kind = EventKind::ChunkGenerated;
    ChunkCoord chunk;
    OperationId operationId = 0;
    ResultCode code = ResultCode::Ok;
    uint64_t changedVoxels = 0;
};

/// Optional observer hook. Implementations must be cheap and must not re-enter the world.
class OBCORE_API InstrumentationSink
{
public:
    virtual ~InstrumentationSink() = default;
    virtual void OnEvent(const EventData& event) = 0;
};

} // namespace ob
