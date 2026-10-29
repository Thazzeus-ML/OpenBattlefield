// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
#pragma once

#include "OpenBattlefieldCore/ObChunk.h"
#include "OpenBattlefieldCore/ObChunkMesher.h"
#include "OpenBattlefieldCore/ObInstrumentation.h"
#include "OpenBattlefieldCore/ObProceduralGenerator.h"
#include "OpenBattlefieldCore/ObTerrainOperation.h"
#include "OpenBattlefieldCore/ObWorldConfig.h"

#include <deque>
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace ob
{

class ByteWriter;
class ByteReader;

/// Authoritative terrain state for one world:
///
///     World State = Procedural Foundation (WorldConfig)
///                 + Validated Modifications (ordered journal of TerrainOperations)
///                 + loaded Chunk cache (derived, reconstructable)
///
/// Threading contract (see Docs/concurrency.md): a TerrainWorld is owned and mutated by exactly one
/// thread at a time. Derived work for other threads is handed out as immutable ChunkMeshInput
/// snapshots carrying the chunk version they were taken from.
///
/// Chunks are stored in a std::map keyed by ChunkCoord so that every iteration (hashing, serialization,
/// dirty flushing) happens in canonical order regardless of load order.
class OBCORE_API TerrainWorld
{
public:
    /// Snapshot format version written by SerializeSnapshot. Version 2 adds the journal base and the
    /// part of the duplicate-detection window that predates the journal; version 1 still loads.
    static constexpr uint16_t kSnapshotSchemaVersion = 2;
    /// Journal format version written by SerializeJournal. Version 2 adds the journal base; version 1
    /// still loads.
    static constexpr uint16_t kJournalSchemaVersion = 2;
    /// Number of most recent operation ids retained for duplicate detection.
    static constexpr size_t kRecentOperationWindow = 4096;

    explicit TerrainWorld(const WorldConfig& config, InstrumentationSink* sink = nullptr);
    ~TerrainWorld();

    TerrainWorld(const TerrainWorld&) = delete;
    TerrainWorld& operator=(const TerrainWorld&) = delete;

    /// ResultCode::Ok when the configuration validated; otherwise every mutation is refused.
    ResultCode Status() const { return status_; }
    const WorldConfig& Config() const { return config_; }
    const ProceduralGenerator& Generator() const { return generator_; }

    // --- Chunk access -----------------------------------------------------------------------------

    Chunk* FindChunk(const ChunkCoord& coord);
    const Chunk* FindChunk(const ChunkCoord& coord) const;

    /// Returns the loaded chunk, generating it from the procedural foundation if needed.
    ResultCode EnsureChunk(const ChunkCoord& coord, Chunk*& outChunk);

    /// Unloads a chunk. Modified chunks are refused (ChunkModified) unless force is set, because
    /// their state would have to be reconstructed by journal replay.
    ResultCode UnloadChunk(const ChunkCoord& coord, bool force = false);

    /// Material at a world voxel: the loaded chunk if present, otherwise the procedural foundation.
    /// Air outside the world bounds.
    Material SampleMaterial(const VoxelCoord& voxel) const;

    size_t LoadedChunkCount() const { return chunks_.size(); }
    size_t ModifiedChunkCount() const;

    template <typename Fn> void ForEachChunk(Fn&& fn) const
    {
        for (const auto& kv : chunks_)
            fn(*kv.second);
    }

    // --- Authoritative modification ---------------------------------------------------------------

    /// Validates and applies an operation. On success the operation is appended to the journal, the
    /// world version advances and affected chunks are versioned and marked dirty. Rejections leave
    /// the world untouched.
    OperationResult ApplyOperation(const TerrainOperation& op);

    /// Applies an ordered list of operations, stopping at the first rejection.
    ResultCode ReplayJournal(const std::vector<TerrainOperation>& ops, size_t* outApplied = nullptr);

    /// Operations accepted since JournalBase(), in order. Version() == JournalBase() + Journal().size().
    const std::vector<TerrainOperation>& Journal() const { return journal_; }
    WorldVersion Version() const { return worldVersion_; }
    SequenceNumber LastSequence() const { return lastSequence_; }
    /// Number of accepted operations dropped by CompactJournal (world version the journal starts at).
    /// Zero means the journal is the full history and replays onto a fresh world.
    WorldVersion JournalBase() const { return journalBase_; }

    /// Drops all but the most recent keepLast journal entries. The modified chunks already hold their
    /// effect, so the state hash, chunk versions, Version(), LastSequence() and duplicate detection are
    /// unchanged; afterwards the world is reconstructable from a snapshot plus the operations that
    /// follow it, but not from the journal alone. Returns the number of entries dropped.
    size_t CompactJournal(size_t keepLast);

    // --- Dirty tracking -----------------------------------------------------------------------------

    /// Moves the sorted set of chunks with pending dirty regions into out and clears the pending set.
    /// Per-chunk dirty regions are consumed separately through Chunk::ConsumeDirtyRegion.
    void TakeDirtyChunks(std::vector<ChunkCoord>& out);
    size_t PendingDirtyChunkCount() const { return dirtyChunks_.size(); }

    // --- Derived data -------------------------------------------------------------------------------

    /// Builds an immutable padded snapshot of a chunk for meshing on any thread. Neighbour voxels come
    /// from loaded chunks when available, otherwise from the procedural foundation.
    ResultCode BuildMeshInput(const ChunkCoord& coord, ChunkMeshInput& out) const;

    // --- State identity -----------------------------------------------------------------------------

    /// Hash of the authoritative state: configuration plus the content of every modified chunk, in
    /// canonical order. Unmodified chunks are excluded because they are a pure function of the
    /// configuration, which makes the hash independent of which chunks happen to be loaded.
    StateHash ComputeStateHash() const;

    /// Hash of every loaded chunk (modified or not). Useful for tests; depends on load set.
    StateHash ComputeLoadedChunkHash() const;

    /// Hash of the journal contents.
    StateHash ComputeJournalHash() const;

    // --- Persistence --------------------------------------------------------------------------------

    /// Writes config, journal, sequence state and every modified chunk.
    void SerializeSnapshot(ByteWriter& w) const;

    /// Reconstructs a world from SerializeSnapshot output.
    static ResultCode DeserializeSnapshot(ByteReader& r, std::unique_ptr<TerrainWorld>& outWorld,
                                          InstrumentationSink* sink = nullptr);

    /// Writes only the journal (config + journal base + ordered operations). When JournalBase() is
    /// zero, replay against a fresh world rebuilds the same state.
    void SerializeJournal(ByteWriter& w) const;
    /// Reads SerializeJournal output. A compacted journal (base > 0) cannot rebuild a world from the
    /// procedural foundation and is refused with JournalCompacted unless outBase is given.
    static ResultCode DeserializeJournal(ByteReader& r, WorldConfig& outConfig,
                                         std::vector<TerrainOperation>& outOps,
                                         WorldVersion* outBase = nullptr);

    // --- Diagnostics --------------------------------------------------------------------------------

    const WorldStats& Stats() const { return stats_; }
    /// Approximate heap footprint of chunks and journal.
    size_t EstimateMemoryBytes() const;

private:
    Chunk& GenerateChunkUnchecked(const ChunkCoord& coord);
    void Emit(const EventData& e);
    void RememberOperationId(OperationId id);
    bool IsRecentOperationId(OperationId id) const;
    OperationResult Reject(ResultCode code, OperationId id);

    WorldConfig config_;
    ResultCode status_ = ResultCode::Ok;
    ProceduralGenerator generator_;
    InstrumentationSink* sink_ = nullptr;

    std::map<ChunkCoord, std::unique_ptr<Chunk>> chunks_;
    std::set<ChunkCoord> dirtyChunks_;

    std::vector<TerrainOperation> journal_;
    WorldVersion worldVersion_ = 0;
    WorldVersion journalBase_ = 0;
    SequenceNumber lastSequence_ = 0;
    std::set<OperationId> recentIds_;
    std::deque<OperationId> recentIdOrder_;

    WorldStats stats_;
};

} // namespace ob
