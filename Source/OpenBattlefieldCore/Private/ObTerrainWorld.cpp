// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
#include "OpenBattlefieldCore/ObTerrainWorld.h"

#include "OpenBattlefieldCore/ObCoordinates.h"
#include "OpenBattlefieldCore/ObHash.h"
#include "OpenBattlefieldCore/ObSerialization.h"

namespace ob
{

namespace
{
constexpr uint8_t kSnapshotMagic[4] = {'O', 'B', 'W', 'S'};
constexpr uint8_t kJournalMagic[4] = {'O', 'B', 'W', 'J'};
// Hard caps on counts read from untrusted serialized data, so a corrupt header cannot drive a huge
// allocation before the payload is validated.
constexpr uint32_t kMaxSerializedOperations = 1u << 24;
constexpr uint32_t kMaxSerializedChunks = 1u << 22;

bool ReadMagic(ByteReader& r, const uint8_t expected[4])
{
    uint8_t magic[4] = {0, 0, 0, 0};
    if (!r.Bytes(magic, 4))
        return false;
    for (int i = 0; i < 4; ++i)
        if (magic[i] != expected[i])
            return false;
    return true;
}

template <typename T> T MaxOf(T a, T b)
{
    return a < b ? b : a;
}
template <typename T> T MinOf(T a, T b)
{
    return b < a ? b : a;
}
} // namespace

TerrainWorld::TerrainWorld(const WorldConfig& config, InstrumentationSink* sink)
    : config_(config), status_(config.Validate()), generator_(config), sink_(sink)
{
}

TerrainWorld::~TerrainWorld() = default;

// --- Chunk access ---------------------------------------------------------------------------------

Chunk* TerrainWorld::FindChunk(const ChunkCoord& coord)
{
    auto it = chunks_.find(coord);
    return it == chunks_.end() ? nullptr : it->second.get();
}

const Chunk* TerrainWorld::FindChunk(const ChunkCoord& coord) const
{
    auto it = chunks_.find(coord);
    return it == chunks_.end() ? nullptr : it->second.get();
}

Chunk& TerrainWorld::GenerateChunkUnchecked(const ChunkCoord& coord)
{
    std::unique_ptr<Chunk> chunk(new Chunk(coord, config_.chunkSize));
    generator_.GenerateChunk(*chunk);
    Chunk& ref = *chunk;
    chunks_.emplace(coord, static_cast<std::unique_ptr<Chunk>&&>(chunk));
    ++stats_.chunksGenerated;
    EventData e;
    e.kind = EventKind::ChunkGenerated;
    e.chunk = coord;
    Emit(e);
    return ref;
}

ResultCode TerrainWorld::EnsureChunk(const ChunkCoord& coord, Chunk*& outChunk)
{
    outChunk = nullptr;
    if (Failed(status_))
        return ResultCode::WorldNotInitialized;
    if (!config_.ContainsChunk(coord))
        return ResultCode::ChunkOutOfBounds;
    if (Chunk* existing = FindChunk(coord))
    {
        outChunk = existing;
        return ResultCode::Ok;
    }
    outChunk = &GenerateChunkUnchecked(coord);
    return ResultCode::Ok;
}

ResultCode TerrainWorld::UnloadChunk(const ChunkCoord& coord, bool force)
{
    auto it = chunks_.find(coord);
    if (it == chunks_.end())
        return ResultCode::ChunkNotLoaded;
    if (it->second->IsModified() && !force)
        return ResultCode::ChunkModified;
    const bool wasModified = it->second->IsModified();
    chunks_.erase(it);
    if (wasModified)
    {
        // Neighbours' borders fall back to the procedural foundation: their whole facing slab changes.
        DirtyRegion whole;
        whole.Include(LocalCoord(0, 0, 0));
        const uint32_t last = config_.chunkSize - 1;
        whole.Include(LocalCoord(last, last, last));
        MarkFaceNeighboursDirty(coord, whole);
    }
    dirtyChunks_.erase(coord);
    ++stats_.chunksUnloaded;
    EventData e;
    e.kind = EventKind::ChunkUnloaded;
    e.chunk = coord;
    Emit(e);
    return ResultCode::Ok;
}

Material TerrainWorld::SampleMaterial(const VoxelCoord& voxel) const
{
    if (!config_.ContainsVoxel(voxel))
        return Material::Air;
    ChunkCoord cc;
    if (!TryChunkOf(voxel, config_.chunkSize, cc))
        return Material::Air;
    if (const Chunk* chunk = FindChunk(cc))
        return chunk->Get(LocalOf(voxel, config_.chunkSize));
    return generator_.MaterialAt(voxel);
}

size_t TerrainWorld::ModifiedChunkCount() const
{
    size_t n = 0;
    for (const auto& kv : chunks_)
        n += kv.second->IsModified() ? 1u : 0u;
    return n;
}

// --- Authoritative modification -------------------------------------------------------------------

OperationResult TerrainWorld::Reject(ResultCode code, OperationId id)
{
    ++stats_.operationsRejected;
    EventData e;
    e.kind = EventKind::OperationRejected;
    e.operationId = id;
    e.code = code;
    Emit(e);
    OperationResult r;
    r.code = code;
    return r;
}

bool TerrainWorld::IsRecentOperationId(OperationId id) const
{
    return recentIds_.find(id) != recentIds_.end();
}

void TerrainWorld::RememberOperationId(OperationId id)
{
    recentIds_.insert(id);
    recentIdOrder_.push_back(id);
    while (recentIdOrder_.size() > kRecentOperationWindow)
    {
        recentIds_.erase(recentIdOrder_.front());
        recentIdOrder_.pop_front();
    }
}

OperationResult TerrainWorld::ApplyOperation(const TerrainOperation& op)
{
    if (Failed(status_))
        return Reject(ResultCode::WorldNotInitialized, op.operationId);

    const ResultCode valid = ValidateOperation(op, config_);
    if (Failed(valid))
        return Reject(valid, op.operationId);

    if (op.sequenceNumber <= lastSequence_)
    {
        ++stats_.staleOperations;
        return Reject(ResultCode::StaleSequence, op.operationId);
    }
    if (IsRecentOperationId(op.operationId))
    {
        ++stats_.duplicateOperations;
        return Reject(ResultCode::DuplicateOperation, op.operationId);
    }

    VoxelBox box;
    ResultCode code = ComputeAffectedBox(op, config_, box);
    if (Failed(code))
        return Reject(code, op.operationId);

    OperationResult result;
    code = ComputeAffectedChunks(op, config_, result.candidateChunks);
    if (Failed(code))
        return Reject(code, op.operationId);

    const int64_t size = static_cast<int64_t>(config_.chunkSize);
    const int64_t r2 = static_cast<int64_t>(op.radius) * static_cast<int64_t>(op.radius);

    for (const ChunkCoord& cc : result.candidateChunks)
    {
        Chunk* chunk = nullptr;
        code = EnsureChunk(cc, chunk);
        if (Failed(code))
        {
            // Cannot happen for a validated operation inside the world bounds, but never fail silently.
            return Reject(code, op.operationId);
        }

        const VoxelCoord origin = ChunkOrigin(cc, config_.chunkSize);
        const int64_t x0 = MaxOf(box.min.x, origin.x);
        const int64_t y0 = MaxOf(box.min.y, origin.y);
        const int64_t z0 = MaxOf(box.min.z, origin.z);
        const int64_t x1 = MinOf(box.max.x, origin.x + size - 1);
        const int64_t y1 = MinOf(box.max.y, origin.y + size - 1);
        const int64_t z1 = MinOf(box.max.z, origin.z + size - 1);

        uint64_t changed = 0;
        for (int64_t z = z0; z <= z1; ++z)
        {
            const int64_t dz = z - op.center.z;
            const int64_t dz2 = dz * dz;
            if (dz2 > r2)
                continue;
            for (int64_t y = y0; y <= y1; ++y)
            {
                const int64_t dy = y - op.center.y;
                const int64_t dyz2 = dz2 + dy * dy;
                if (dyz2 > r2)
                    continue;
                for (int64_t x = x0; x <= x1; ++x)
                {
                    const int64_t dx = x - op.center.x;
                    ++stats_.voxelsVisited;
                    if (dyz2 + dx * dx > r2)
                        continue;
                    const LocalCoord local(static_cast<uint32_t>(x - origin.x),
                                           static_cast<uint32_t>(y - origin.y),
                                           static_cast<uint32_t>(z - origin.z));
                    const Material current = chunk->Get(local);
                    if (op.type == OperationType::SubtractSphere)
                    {
                        if (IsSolid(current) && !IsIndestructible(current) &&
                            chunk->Modify(local, Material::Air))
                            ++changed;
                    }
                    else // AddSphere
                    {
                        if (current == Material::Air && chunk->Modify(local, op.material))
                            ++changed;
                    }
                }
            }
        }

        if (changed > 0)
        {
            chunk->CommitModification();
            dirtyChunks_.insert(cc);
            // The accumulated region is a superset of this operation's changes, so this may mark a
            // neighbour that a tighter box would not; never fewer.
            MarkFaceNeighboursDirty(cc, chunk->Dirty());
            result.modifiedChunks.push_back(cc);
            result.changedVoxels += changed;
            ++stats_.chunkModifications;
        }
    }

    // Commit: the operation is now part of the validated modification layer even if it changed nothing
    // (replaying it later must produce the same journal, versions and hashes).
    journal_.push_back(op);
    ++worldVersion_;
    lastSequence_ = op.sequenceNumber;
    RememberOperationId(op.operationId);
    ++stats_.operationsAccepted;
    stats_.voxelsChanged += result.changedVoxels;
    if (result.changedVoxels == 0)
        ++stats_.operationsNoOp;

    EventData e;
    e.kind = EventKind::OperationAccepted;
    e.operationId = op.operationId;
    e.changedVoxels = result.changedVoxels;
    Emit(e);

    result.code = ResultCode::Ok;
    return result;
}

ResultCode TerrainWorld::ReplayJournal(const std::vector<TerrainOperation>& ops, size_t* outApplied)
{
    size_t applied = 0;
    for (const TerrainOperation& op : ops)
    {
        const OperationResult r = ApplyOperation(op);
        if (!r.Accepted())
        {
            if (outApplied)
                *outApplied = applied;
            return r.code;
        }
        ++applied;
    }
    if (outApplied)
        *outApplied = applied;
    return ResultCode::Ok;
}

size_t TerrainWorld::CompactJournal(size_t keepLast)
{
    if (journal_.size() <= keepLast)
        return 0;
    const size_t drop = journal_.size() - keepLast;
    // The duplicate-detection window is kept separately (recentIds_), so it is unaffected; chunk
    // contents and versions already reflect every dropped operation.
    // Copy the suffix into a right-sized vector: shrink_to_fit is non-binding, and libstdc++ ignores it
    // when exceptions are disabled, which would keep the dropped entries' memory.
    std::vector<TerrainOperation> suffix(journal_.begin() + static_cast<std::ptrdiff_t>(drop),
                                         journal_.end());
    journal_.swap(suffix);
    journalBase_ += drop;
    return drop;
}

// --- Dirty tracking -------------------------------------------------------------------------------

void TerrainWorld::MarkFaceNeighboursDirty(const ChunkCoord& coord, const DirtyRegion& region)
{
    if (!region.any)
        return;
    // The face mesher reads only the six face neighbours of a voxel, so a change at local x = 0 can
    // alter only the -x neighbour's faces at its x = n - 1 (and likewise for the other faces).
    const uint32_t last = config_.chunkSize - 1;
    const uint32_t lo[3] = {region.min.x, region.min.y, region.min.z};
    const uint32_t hi[3] = {region.max.x, region.max.y, region.max.z};
    for (int axis = 0; axis < 3; ++axis)
        for (int side = -1; side <= 1; side += 2)
        {
            if (side < 0 ? lo[axis] != 0 : hi[axis] != last)
                continue;
            ChunkCoord nc = coord;
            int32_t* c[3] = {&nc.x, &nc.y, &nc.z};
            *c[axis] += side;
            Chunk* neighbour = FindChunk(nc);
            if (!neighbour)
                continue; // no mesh exists for a chunk that is not loaded
            uint32_t nlo[3] = {lo[0], lo[1], lo[2]};
            uint32_t nhi[3] = {hi[0], hi[1], hi[2]};
            nlo[axis] = nhi[axis] = side < 0 ? last : 0;
            neighbour->IncludeInDirtyRegion(LocalCoord(nlo[0], nlo[1], nlo[2]),
                                            LocalCoord(nhi[0], nhi[1], nhi[2]));
            dirtyChunks_.insert(nc);
        }
}

void TerrainWorld::TakeDirtyChunks(std::vector<ChunkCoord>& out)
{
    out.clear();
    out.reserve(dirtyChunks_.size());
    for (const ChunkCoord& c : dirtyChunks_)
        out.push_back(c);
    dirtyChunks_.clear();
}

// --- Derived data ---------------------------------------------------------------------------------

ResultCode TerrainWorld::BuildMeshInput(const ChunkCoord& coord, ChunkMeshInput& out) const
{
    const Chunk* chunk = FindChunk(coord);
    if (!chunk)
        return config_.ContainsChunk(coord) ? ResultCode::ChunkNotLoaded : ResultCode::ChunkOutOfBounds;

    const uint32_t n = config_.chunkSize;
    const uint32_t p = n + 2;
    out.coord = coord;
    out.size = n;
    out.version = chunk->Version();
    out.padded.assign(static_cast<size_t>(p) * p * p, static_cast<uint8_t>(Material::Air));

    const uint8_t* data = chunk->Data(); // nullptr for a uniform chunk
    const uint8_t uniform = static_cast<uint8_t>(chunk->UniformMaterial());
    for (uint32_t z = 0; z < n; ++z)
        for (uint32_t y = 0; y < n; ++y)
        {
            const size_t srcRow = LocalIndex(LocalCoord(0, y, z), n);
            const size_t dstRow = out.PaddedIndex(0, static_cast<int32_t>(y), static_cast<int32_t>(z));
            for (uint32_t x = 0; x < n; ++x)
                out.padded[dstRow + x] = data ? data[srcRow + x] : uniform;
        }

    // Border: fill the 26 neighbour regions (six slabs, twelve edges, eight corners; edges/corners are
    // never read by the face mesher, but we fill them for completeness so the snapshot is
    // self-describing). The result is identical to calling SampleMaterial per border voxel, but loaded
    // neighbours are copied directly and unloaded ones are generated per column, each column's surface
    // height computed at most once and shared between regions. World bounds are chunk aligned, so a
    // border voxel is in bounds iff its chunk is; out-of-bounds regions keep the Air fill.
    const VoxelCoord origin = ChunkOrigin(coord, n);
    const int32_t sn = static_cast<int32_t>(n);
    std::vector<int64_t> columnHeight(static_cast<size_t>(p) * p, 0);
    std::vector<uint8_t> columnKnown(static_cast<size_t>(p) * p, 0);
    for (int32_t dz = -1; dz <= 1; ++dz)
        for (int32_t dy = -1; dy <= 1; ++dy)
            for (int32_t dx = -1; dx <= 1; ++dx)
            {
                if (dx == 0 && dy == 0 && dz == 0)
                    continue;
                const ChunkCoord nc(coord.x + dx, coord.y + dy, coord.z + dz);
                if (!config_.ContainsChunk(nc))
                    continue;
                // Padded range per axis: -1 for d = -1, [0, n) for d = 0, n for d = +1.
                const int32_t x0 = dx < 0 ? -1 : (dx > 0 ? sn : 0);
                const int32_t x1 = dx == 0 ? sn - 1 : x0;
                const int32_t y0 = dy < 0 ? -1 : (dy > 0 ? sn : 0);
                const int32_t y1 = dy == 0 ? sn - 1 : y0;
                const int32_t z0 = dz < 0 ? -1 : (dz > 0 ? sn : 0);
                const int32_t z1 = dz == 0 ? sn - 1 : z0;

                if (const Chunk* neighbour = FindChunk(nc))
                {
                    // The neighbour-local coordinate is the padded coordinate shifted by -d * n.
                    const uint8_t* src = neighbour->Data();
                    if (!src)
                    {
                        const uint8_t m = static_cast<uint8_t>(neighbour->UniformMaterial());
                        for (int32_t z = z0; z <= z1; ++z)
                            for (int32_t y = y0; y <= y1; ++y)
                                for (int32_t x = x0; x <= x1; ++x)
                                    out.padded[out.PaddedIndex(x, y, z)] = m;
                        continue;
                    }
                    for (int32_t z = z0; z <= z1; ++z)
                        for (int32_t y = y0; y <= y1; ++y)
                            for (int32_t x = x0; x <= x1; ++x)
                            {
                                const LocalCoord l(static_cast<uint32_t>(x - dx * sn),
                                                   static_cast<uint32_t>(y - dy * sn),
                                                   static_cast<uint32_t>(z - dz * sn));
                                out.padded[out.PaddedIndex(x, y, z)] = src[LocalIndex(l, n)];
                            }
                    continue;
                }

                for (int32_t y = y0; y <= y1; ++y)
                    for (int32_t x = x0; x <= x1; ++x)
                    {
                        const size_t column =
                            static_cast<size_t>(x + 1) + static_cast<size_t>(p) * static_cast<size_t>(y + 1);
                        if (!columnKnown[column])
                        {
                            columnHeight[column] = generator_.SurfaceHeight(origin.x + x, origin.y + y);
                            columnKnown[column] = 1;
                        }
                        for (int32_t z = z0; z <= z1; ++z)
                            out.padded[out.PaddedIndex(x, y, z)] = static_cast<uint8_t>(
                                generator_.MaterialForColumn(origin.z + z, columnHeight[column]));
                    }
            }
    return ResultCode::Ok;
}

// --- State identity -------------------------------------------------------------------------------

StateHash TerrainWorld::ComputeStateHash() const
{
    Hasher h;
    config_.HashInto(h);
    h.U64(static_cast<uint64_t>(ModifiedChunkCount()));
    for (const auto& kv : chunks_)
        if (kv.second->IsModified())
        {
            kv.second->HashContentInto(h);
            h.U64(kv.second->Version());
        }
    return h.Finish();
}

StateHash TerrainWorld::ComputeLoadedChunkHash() const
{
    Hasher h;
    config_.HashInto(h);
    h.U64(static_cast<uint64_t>(chunks_.size()));
    for (const auto& kv : chunks_)
        kv.second->HashContentInto(h);
    return h.Finish();
}

StateHash TerrainWorld::ComputeJournalHash() const
{
    Hasher h;
    h.U64(journalBase_);
    h.U64(static_cast<uint64_t>(journal_.size()));
    for (const TerrainOperation& op : journal_)
        op.HashInto(h);
    return h.Finish();
}

// --- Persistence ----------------------------------------------------------------------------------

void TerrainWorld::SerializeSnapshot(ByteWriter& w) const
{
    w.Bytes(kSnapshotMagic, 4);
    w.U16(kSnapshotSchemaVersion);
    config_.Serialize(w);
    w.U64(worldVersion_);
    w.U64(lastSequence_);
    w.U32(static_cast<uint32_t>(journal_.size()));
    for (const TerrainOperation& op : journal_)
        op.Serialize(w);
    // Schema 2: the journal base, then the oldest ids of the duplicate-detection window that are no
    // longer in the journal (the rest of the window is the journal tail). Empty unless compacted.
    w.U64(journalBase_);
    const size_t inJournal = MinOf(journal_.size(), recentIdOrder_.size());
    const size_t beforeJournal = recentIdOrder_.size() - inJournal;
    w.U32(static_cast<uint32_t>(beforeJournal));
    for (size_t i = 0; i < beforeJournal; ++i)
        w.U64(recentIdOrder_[i]);
    w.U32(static_cast<uint32_t>(ModifiedChunkCount()));
    for (const auto& kv : chunks_)
        if (kv.second->IsModified())
            kv.second->Serialize(w);
}

ResultCode TerrainWorld::DeserializeSnapshot(ByteReader& r, std::unique_ptr<TerrainWorld>& outWorld,
                                             InstrumentationSink* sink)
{
    outWorld.reset();
    if (!ReadMagic(r, kSnapshotMagic))
        return r.Ok() ? ResultCode::SerializationBadMagic : ResultCode::SerializationTruncated;
    uint16_t schema = 0;
    if (!r.U16(schema))
        return ResultCode::SerializationTruncated;
    if (schema != 1 && schema != kSnapshotSchemaVersion)
        return ResultCode::SerializationVersionMismatch;

    WorldConfig config;
    ResultCode code = config.Deserialize(r);
    if (Failed(code))
        return code;

    std::unique_ptr<TerrainWorld> world(new TerrainWorld(config, sink));
    if (Failed(world->Status()))
        return ResultCode::SerializationCorrupt;

    uint32_t opCount = 0;
    if (!r.U64(world->worldVersion_) || !r.U64(world->lastSequence_) || !r.U32(opCount))
        return ResultCode::SerializationTruncated;
    if (opCount > kMaxSerializedOperations)
        return ResultCode::SerializationLimitExceeded;

    world->journal_.reserve(opCount);
    SequenceNumber prevSeq = 0;
    for (uint32_t i = 0; i < opCount; ++i)
    {
        TerrainOperation op;
        code = op.Deserialize(r);
        if (Failed(code))
            return code;
        if (Failed(ValidateOperation(op, config)) || op.sequenceNumber <= prevSeq)
            return ResultCode::SerializationCorrupt;
        prevSeq = op.sequenceNumber;
        world->journal_.push_back(op);
    }
    if (opCount > 0 && world->lastSequence_ != prevSeq)
        return ResultCode::SerializationCorrupt;

    // Schema 1 has no compaction: the journal is the full history and the window is its tail.
    std::vector<OperationId> beforeJournal;
    if (schema >= 2)
    {
        uint32_t beforeCount = 0;
        if (!r.U64(world->journalBase_) || !r.U32(beforeCount))
            return ResultCode::SerializationTruncated;
        // Window ids older than the journal exist only for dropped operations, and only while the
        // journal is shorter than the window.
        const size_t inJournal = MinOf(world->journal_.size(), kRecentOperationWindow);
        if (beforeCount > kRecentOperationWindow - inJournal || beforeCount > world->journalBase_)
            return ResultCode::SerializationCorrupt;
        beforeJournal.resize(beforeCount);
        for (uint32_t i = 0; i < beforeCount; ++i)
            if (!r.U64(beforeJournal[i]))
                return ResultCode::SerializationTruncated;
    }
    // Every accepted operation advanced the version once: those dropped by compaction plus the
    // journal.
    if (world->journalBase_ > world->worldVersion_ || world->worldVersion_ - world->journalBase_ != opCount)
        return ResultCode::SerializationCorrupt;

    // Restore the duplicate-detection window (pre-journal ids, then the journal tail); a repeated id
    // inside it means the snapshot was not produced by a TerrainWorld.
    for (OperationId id : beforeJournal)
    {
        if (world->IsRecentOperationId(id))
            return ResultCode::SerializationCorrupt;
        world->RememberOperationId(id);
    }
    const size_t firstRecent =
        world->journal_.size() > kRecentOperationWindow ? world->journal_.size() - kRecentOperationWindow : 0;
    for (size_t i = firstRecent; i < world->journal_.size(); ++i)
    {
        if (world->IsRecentOperationId(world->journal_[i].operationId))
            return ResultCode::SerializationCorrupt;
        world->RememberOperationId(world->journal_[i].operationId);
    }

    uint32_t chunkCount = 0;
    if (!r.U32(chunkCount))
        return ResultCode::SerializationTruncated;
    if (chunkCount > kMaxSerializedChunks)
        return ResultCode::SerializationLimitExceeded;
    for (uint32_t i = 0; i < chunkCount; ++i)
    {
        std::unique_ptr<Chunk> chunk(new Chunk(ChunkCoord(), config.chunkSize));
        code = Chunk::Deserialize(r, config.chunkSize, *chunk);
        if (Failed(code))
            return code;
        const ChunkCoord coord = chunk->Coord();
        if (!config.ContainsChunk(coord) || world->chunks_.count(coord) != 0 || !chunk->IsModified())
            return ResultCode::SerializationCorrupt;
        world->chunks_.emplace(coord, static_cast<std::unique_ptr<Chunk>&&>(chunk));
        ++world->stats_.chunksDeserialized;
    }

    outWorld = static_cast<std::unique_ptr<TerrainWorld>&&>(world);
    return ResultCode::Ok;
}

void TerrainWorld::SerializeJournal(ByteWriter& w) const
{
    w.Bytes(kJournalMagic, 4);
    w.U16(kJournalSchemaVersion);
    config_.Serialize(w);
    w.U64(journalBase_);
    w.U32(static_cast<uint32_t>(journal_.size()));
    for (const TerrainOperation& op : journal_)
        op.Serialize(w);
}

ResultCode TerrainWorld::DeserializeJournal(ByteReader& r, WorldConfig& outConfig,
                                            std::vector<TerrainOperation>& outOps, WorldVersion* outBase)
{
    outOps.clear();
    if (outBase)
        *outBase = 0;
    if (!ReadMagic(r, kJournalMagic))
        return r.Ok() ? ResultCode::SerializationBadMagic : ResultCode::SerializationTruncated;
    uint16_t schema = 0;
    if (!r.U16(schema))
        return ResultCode::SerializationTruncated;
    if (schema != 1 && schema != kJournalSchemaVersion)
        return ResultCode::SerializationVersionMismatch;
    ResultCode code = outConfig.Deserialize(r);
    if (Failed(code))
        return code;
    WorldVersion base = 0; // schema 1 journals are always the full history
    if (schema >= 2 && !r.U64(base))
        return ResultCode::SerializationTruncated;
    if (base != 0 && !outBase)
        return ResultCode::JournalCompacted;
    if (outBase)
        *outBase = base;
    uint32_t count = 0;
    if (!r.U32(count))
        return ResultCode::SerializationTruncated;
    if (count > kMaxSerializedOperations)
        return ResultCode::SerializationLimitExceeded;
    outOps.reserve(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        TerrainOperation op;
        code = op.Deserialize(r);
        if (Failed(code))
            return code;
        outOps.push_back(op);
    }
    return ResultCode::Ok;
}

// --- Diagnostics ----------------------------------------------------------------------------------

size_t TerrainWorld::EstimateMemoryBytes() const
{
    size_t bytes = sizeof(TerrainWorld);
    for (const auto& kv : chunks_)
        bytes += kv.second->MemoryBytes() + sizeof(ChunkCoord) + sizeof(void*) * 4; // map node overhead
    bytes += journal_.capacity() * sizeof(TerrainOperation);
    bytes += recentIds_.size() * (sizeof(OperationId) + sizeof(void*) * 4);
    bytes += recentIdOrder_.size() * sizeof(OperationId);
    bytes += dirtyChunks_.size() * (sizeof(ChunkCoord) + sizeof(void*) * 4);
    return bytes;
}

void TerrainWorld::Emit(const EventData& e)
{
    if (sink_)
        sink_->OnEvent(e);
}

} // namespace ob
