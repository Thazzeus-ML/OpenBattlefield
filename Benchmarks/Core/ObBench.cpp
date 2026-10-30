// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
//
// OpenBattlefield terrain benchmark harness.
//
//   ob_bench --workload <name|all>   (generation, isolated_crater, boundary_crater, artillery,
//                                     reconstruction, stress, resolution)
//            [--preset smoke|default|large] [--iterations N] [--warmup N] [--seed S] [--chunk-size N]
//            [--ops N] [--radius N] [--region N] [--label TEXT] [--out FILE.json] [--list]
//
// Every workload is deterministic for a given parameter set, performs its own correctness checks
// (state hash, oracle comparison, consistency across chunk sizes) and reports machine-readable
// results. See Docs/benchmarking.md for the measurement protocol and Benchmarks/workloads/ for presets.

#include "ObBenchSupport.h"

#if defined(__GNUC__) && !defined(__clang__)
// GCC cannot see that the replaced global operator new/delete pair is consistent and reports false
// positives in this translation unit only.
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#pragma GCC diagnostic ignored "-Wnull-dereference"
#endif

#include "OpenBattlefieldCore/OpenBattlefieldCore.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <functional>
#include <memory>
#include <new>
#include <string>
#include <vector>

// --- Global allocation counting (single-threaded harness) ------------------------------------------

void* operator new(size_t size)
{
    obbench::AllocCounters& c = obbench::GlobalAllocCounters();
    ++c.allocations;
    c.bytes += size;
    if (void* p = std::malloc(size ? size : 1))
        return p;
    std::abort();
}
void* operator new[](size_t size)
{
    return operator new(size);
}
void operator delete(void* p) noexcept
{
    ++obbench::GlobalAllocCounters().frees;
    std::free(p);
}
void operator delete[](void* p) noexcept
{
    operator delete(p);
}
void operator delete(void* p, size_t) noexcept
{
    operator delete(p);
}
void operator delete[](void* p, size_t) noexcept
{
    operator delete(p);
}

using namespace ob;
using namespace obbench;

namespace
{

// --- Parameters -------------------------------------------------------------------------------------

struct Params
{
    std::string preset = "default";
    uint64_t seed = 20240601;
    uint32_t chunkSize = 32;
    uint32_t iterations = 0; // 0 = preset default
    uint32_t warmup = 0;
    uint32_t ops = 0;
    uint32_t radius = 0;
    uint32_t region = 0; // chunks per axis (generation) or voxel half-extent (artillery)
    bool explicitIterations = false, explicitWarmup = false, explicitOps = false, explicitRadius = false,
         explicitRegion = false;
};

struct Defaults
{
    uint32_t iterations, warmup, ops, radius, region;
};

Defaults PresetDefaults(const std::string& workload, const std::string& preset)
{
    const bool smoke = preset == "smoke";
    const bool large = preset == "large";
    if (workload == "generation")
        return smoke ? Defaults{2, 1, 0, 0, 2} : large ? Defaults{5, 1, 0, 0, 12} : Defaults{5, 1, 0, 0, 6};
    if (workload == "isolated_crater")
        return smoke   ? Defaults{5, 1, 0, 4, 0}
               : large ? Defaults{200, 20, 0, 8, 0}
                       : Defaults{50, 5, 0, 6, 0};
    if (workload == "boundary_crater")
        return smoke   ? Defaults{5, 1, 0, 6, 0}
               : large ? Defaults{200, 20, 0, 12, 0}
                       : Defaults{50, 5, 0, 8, 0};
    if (workload == "artillery")
        return smoke   ? Defaults{1, 0, 50, 6, 64}
               : large ? Defaults{3, 1, 5000, 10, 512}
                       : Defaults{3, 1, 500, 8, 256};
    if (workload == "reconstruction")
        return smoke   ? Defaults{1, 0, 50, 6, 64}
               : large ? Defaults{1, 0, 5000, 10, 512}
                       : Defaults{1, 0, 500, 8, 256};
    if (workload == "resolution") // region = finest level: voxel size 25 cm / 2^region
        return smoke ? Defaults{1, 0, 2, 0, 1} : large ? Defaults{1, 0, 8, 0, 4} : Defaults{1, 0, 8, 0, 3};
    if (workload == "stress")
        return smoke   ? Defaults{1, 0, 200, 6, 64}
               : large ? Defaults{1, 0, 50000, 10, 768}
                       : Defaults{1, 0, 10000, 8, 512};
    return Defaults{1, 0, 0, 0, 0};
}

Params Resolve(const Params& base, const std::string& workload)
{
    Params p = base;
    const Defaults d = PresetDefaults(workload, p.preset);
    if (!p.explicitIterations)
        p.iterations = d.iterations;
    if (!p.explicitWarmup)
        p.warmup = d.warmup;
    if (!p.explicitOps)
        p.ops = d.ops;
    if (!p.explicitRadius)
        p.radius = d.radius;
    if (!p.explicitRegion)
        p.region = d.region;
    return p;
}

// --- Result model ---------------------------------------------------------------------------------

struct Series
{
    std::string name;
    std::vector<double> samplesNs;
};

struct WorkloadResult
{
    std::string name;
    Params params;
    // deque: references returned by AddSeries stay valid as more series are added.
    std::deque<Series> series;
    std::map<std::string, double> numbers;
    std::map<std::string, uint64_t> integers;
    std::map<std::string, uint64_t> hashes;
    std::map<std::string, bool> checks;
    std::vector<std::string> notes;
    bool AllChecksPassed() const
    {
        for (const auto& kv : checks)
            if (!kv.second)
                return false;
        return true;
    }
    Series& AddSeries(const std::string& n)
    {
        series.push_back(Series{n, {}});
        return series.back();
    }
};

WorldConfig BenchConfig(const Params& p)
{
    WorldConfig cfg;
    cfg.seed = p.seed;
    cfg.chunkSize = p.chunkSize;
    cfg.minChunk = ChunkCoord(-256, -256, -8);
    cfg.maxChunk = ChunkCoord(255, 255, 7);
    cfg.maxOperationRadius = 64;
    cfg.baseHeight = 0;
    cfg.heightAmplitude = 24;
    cfg.bedrockHeight = -200;
    return cfg;
}

// Deterministic artillery sequence shared by the artillery, reconstruction and stress workloads.
std::vector<TerrainOperation> ArtillerySequence(const WorldConfig& cfg, uint32_t count, uint32_t maxRadius,
                                                uint32_t halfExtent, uint64_t seed)
{
    std::vector<TerrainOperation> ops;
    ops.reserve(count);
    ProceduralGenerator gen(cfg);
    const int64_t span = static_cast<int64_t>(halfExtent) * 2 + 1;
    for (uint32_t i = 0; i < count; ++i)
    {
        const uint64_t r = Mix64(seed ^ (static_cast<uint64_t>(i) * 0x9E3779B97F4A7C15ull));
        TerrainOperation op;
        op.operationId = 1 + i;
        op.sequenceNumber = 1 + i;
        op.worldId = cfg.worldId;
        const int64_t x = static_cast<int64_t>(r % static_cast<uint64_t>(span)) - halfExtent;
        const int64_t y = static_cast<int64_t>((r >> 20) % static_cast<uint64_t>(span)) - halfExtent;
        const int64_t h = gen.SurfaceHeight(x, y);
        // Overlapping craters: 80% subtract near the surface, 20% add rubble just above it.
        const uint32_t radius = 2 + static_cast<uint32_t>((r >> 40) % (maxRadius - 1));
        if ((r >> 56) % 5 == 0)
        {
            op.type = OperationType::AddSphere;
            op.material = Material::Rock;
            op.center = VoxelCoord(x, y, h + 1);
        }
        else
        {
            op.type = OperationType::SubtractSphere;
            op.center = VoxelCoord(x, y, h - static_cast<int64_t>(radius / 2));
        }
        op.radius = radius;
        ops.push_back(op);
    }
    return ops;
}

uint64_t MeshBytes(const MeshData& m)
{
    return m.positions.size() * sizeof(float) + m.normals.size() * sizeof(float) + m.vertexMaterial.size() +
           m.indices.size() * sizeof(uint32_t);
}

// Derived mesh state kept per chunk between flushes, as a renderer would keep it.
struct MeshCache
{
    std::map<ChunkCoord, ChunkFaces> faces;
    ChunkMeshInput input;
    MeshData mesh; // last emitted mesh
};

// Builds faces and mesh for a chunk with no cached faces (first time it is shown).
size_t MeshChunk(TerrainWorld& world, const ChunkCoord& cc, MeshCache& cache)
{
    if (Failed(world.BuildMeshInput(cc, cache.input)))
        return 0;
    ChunkFaces& faces = cache.faces[cc];
    const size_t n = BuildChunkFaces(cache.input, faces);
    EmitChunkMesh(faces, cache.mesh);
    return n;
}

// Rebuilds derived data (mesh) for every pending dirty chunk: the synchronous reference path. Chunks
// with cached faces are remeshed incrementally from their dirty region; others are meshed in full.
uint64_t FlushDirty(TerrainWorld& world, std::vector<ChunkCoord>& scratch, MeshCache& cache,
                    uint64_t* outFaces)
{
    world.TakeDirtyChunks(scratch);
    for (const ChunkCoord& cc : scratch)
    {
        Chunk* chunk = world.FindChunk(cc);
        if (!chunk)
            continue;
        const DirtyRegion region = chunk->ConsumeDirtyRegion();
        if (Failed(world.BuildMeshInput(cc, cache.input)))
            continue;
        ChunkFaces& faces = cache.faces[cc];
        const size_t n = region.any ? UpdateChunkFaces(cache.input, region.min, region.max, faces)
                                    : BuildChunkFaces(cache.input, faces);
        EmitChunkMesh(faces, cache.mesh);
        if (outFaces)
            *outFaces += n;
    }
    return scratch.size();
}

// --- Workload A: generation -------------------------------------------------------------------------

WorkloadResult RunGeneration(const Params& p)
{
    WorkloadResult r;
    r.name = "generation";
    r.params = p;
    const WorldConfig cfg = BenchConfig(p);
    const int32_t n = static_cast<int32_t>(p.region);
    const int32_t zLo = -2, zHi = 1; // 4 vertical chunks: surface band for default heights
    Series& perChunk = r.AddSeries("chunk_generation_ns");
    Series& perIter = r.AddSeries("iteration_total_ns");
    StateHash hash = 0;
    uint64_t chunks = 0;
    uint64_t memoryBytes = 0;
    uint64_t allocs = 0;
    for (uint32_t it = 0; it < p.warmup + p.iterations; ++it)
    {
        const bool measured = it >= p.warmup;
        TerrainWorld world(cfg);
        AllocDelta ad;
        ScopedTimer total;
        chunks = 0;
        for (int32_t z = zLo; z <= zHi; ++z)
            for (int32_t y = -n / 2; y < n - n / 2; ++y)
                for (int32_t x = -n / 2; x < n - n / 2; ++x)
                {
                    Chunk* c = nullptr;
                    ScopedTimer t;
                    world.EnsureChunk(ChunkCoord(x, y, z), c);
                    if (measured)
                        perChunk.samplesNs.push_back(static_cast<double>(t.ElapsedNs()));
                    ++chunks;
                }
        if (measured)
        {
            perIter.samplesNs.push_back(static_cast<double>(total.ElapsedNs()));
            memoryBytes = world.EstimateMemoryBytes();
            allocs = ad.Allocations();
            const StateHash h = world.ComputeLoadedChunkHash();
            if (hash != 0 && h != hash)
                r.checks["hash_stable_across_iterations"] = false;
            else
                r.checks.emplace("hash_stable_across_iterations", true);
            hash = h;
        }
    }
    r.integers["chunks_per_iteration"] = chunks;
    r.integers["voxels_per_iteration"] = chunks * cfg.VoxelsPerChunk();
    r.integers["estimated_world_bytes"] = memoryBytes;
    r.integers["allocations_per_iteration"] = allocs;
    r.hashes["loaded_chunk_hash"] = hash;
    if (!perIter.samplesNs.empty())
        r.numbers["chunks_per_second"] =
            static_cast<double>(chunks) / (ComputeStats(perIter.samplesNs).medianNs / 1e9);
    return r;
}

// --- Workload B: isolated crater ------------------------------------------------------------------

WorkloadResult RunIsolatedCrater(const Params& p)
{
    WorkloadResult r;
    r.name = "isolated_crater";
    r.params = p;
    WorldConfig cfg = BenchConfig(p);
    // Keep the surface in the middle of chunk z=0 so a modest crater never leaves it.
    cfg.baseHeight = static_cast<int64_t>(cfg.chunkSize / 2);
    cfg.heightAmplitude = 3;
    Series& apply = r.AddSeries("apply_ns");
    Series& dirty = r.AddSeries("dirty_flush_ns");
    const int64_t c = static_cast<int64_t>(cfg.chunkSize / 2);
    bool isolated = true;
    bool oracleOk = true;
    uint64_t changed = 0;
    uint64_t faces = 0;
    std::vector<ChunkCoord> scratch;
    MeshCache cache;
    for (uint32_t it = 0; it < p.warmup + p.iterations; ++it)
    {
        const bool measured = it >= p.warmup;
        TerrainWorld world(cfg);
        Chunk* chunk = nullptr;
        world.EnsureChunk(ChunkCoord(0, 0, 0), chunk);
        cache.faces.clear();
        MeshChunk(world, ChunkCoord(0, 0, 0), cache); // visible before the crater, untimed
        TerrainOperation op;
        op.operationId = 1;
        op.sequenceNumber = 1;
        op.worldId = cfg.worldId;
        op.type = OperationType::SubtractSphere;
        op.center = VoxelCoord(c, c, world.Generator().SurfaceHeight(c, c));
        op.radius = p.radius;
        ScopedTimer t;
        const OperationResult res = world.ApplyOperation(op);
        const uint64_t applyNs = t.ElapsedNs();
        ScopedTimer td;
        faces = 0;
        FlushDirty(world, scratch, cache, &faces);
        const uint64_t dirtyNs = td.ElapsedNs();
        if (measured)
        {
            apply.samplesNs.push_back(static_cast<double>(applyNs));
            dirty.samplesNs.push_back(static_cast<double>(dirtyNs));
        }
        isolated =
            isolated && res.Accepted() && res.candidateChunks.size() == 1 && res.modifiedChunks.size() == 1;
        changed = res.changedVoxels;
        if (it == 0)
        {
            // Oracle: every voxel in the box matches a per-voxel recomputation.
            const TerrainWorld fresh(cfg);
            const int64_t rr = static_cast<int64_t>(op.radius);
            for (int64_t z = op.center.z - rr; z <= op.center.z + rr && oracleOk; ++z)
                for (int64_t y = op.center.y - rr; y <= op.center.y + rr && oracleOk; ++y)
                    for (int64_t x = op.center.x - rr; x <= op.center.x + rr; ++x)
                    {
                        const VoxelCoord v(x, y, z);
                        Material expected = fresh.Generator().MaterialAt(v);
                        if (InsideSphere(x - op.center.x, y - op.center.y, z - op.center.z, op.radius) &&
                            IsSolid(expected) && !IsIndestructible(expected))
                            expected = Material::Air;
                        if (world.SampleMaterial(v) != expected)
                        {
                            oracleOk = false;
                            break;
                        }
                    }
        }
    }
    r.checks["single_chunk"] = isolated;
    r.checks["oracle_match"] = oracleOk;
    r.integers["changed_voxels"] = changed;
    r.integers["mesh_faces_after_flush"] = faces;
    return r;
}

// --- Workload C: boundary crater ------------------------------------------------------------------

WorkloadResult RunBoundaryCrater(const Params& p)
{
    WorkloadResult r;
    r.name = "boundary_crater";
    r.params = p;
    WorldConfig cfg = BenchConfig(p);
    cfg.baseHeight = static_cast<int64_t>(p.radius) + 8; // sphere at the origin is fully buried
    cfg.heightAmplitude = 3;
    Series& apply = r.AddSeries("apply_ns");
    Series& dirty = r.AddSeries("dirty_flush_ns");
    bool eightChunks = true;
    bool consistent = true;
    uint64_t changed = 0;
    uint64_t faces = 0;
    std::vector<ChunkCoord> scratch;
    MeshCache cache;
    TerrainOperation op;
    op.operationId = 1;
    op.sequenceNumber = 1;
    op.worldId = cfg.worldId;
    op.type = OperationType::SubtractSphere;
    op.center = VoxelCoord(0, 0, 0);
    op.radius = p.radius;
    for (uint32_t it = 0; it < p.warmup + p.iterations; ++it)
    {
        const bool measured = it >= p.warmup;
        TerrainWorld world(cfg);
        for (int32_t z = -1; z <= 0; ++z)
            for (int32_t y = -1; y <= 0; ++y)
                for (int32_t x = -1; x <= 0; ++x)
                {
                    Chunk* c = nullptr;
                    world.EnsureChunk(ChunkCoord(x, y, z), c);
                }
        cache.faces.clear();
        world.ForEachChunk([&](const Chunk& c) { MeshChunk(world, c.Coord(), cache); }); // untimed
        ScopedTimer t;
        const OperationResult res = world.ApplyOperation(op);
        const uint64_t applyNs = t.ElapsedNs();
        ScopedTimer td;
        faces = 0;
        FlushDirty(world, scratch, cache, &faces);
        const uint64_t dirtyNs = td.ElapsedNs();
        if (measured)
        {
            apply.samplesNs.push_back(static_cast<double>(applyNs));
            dirty.samplesNs.push_back(static_cast<double>(dirtyNs));
        }
        eightChunks = eightChunks && res.Accepted() && res.candidateChunks.size() == 8 &&
                      res.modifiedChunks.size() == 8;
        changed = res.changedVoxels;
        if (it == 0)
        {
            // Consistency: a world with a different chunk size must agree on every voxel in the box.
            WorldConfig other = cfg;
            other.chunkSize = cfg.chunkSize == 16 ? 32 : 16;
            TerrainWorld otherWorld(other);
            if (!otherWorld.ApplyOperation(op).Accepted())
                consistent = false;
            const int64_t rr = static_cast<int64_t>(op.radius) + 1;
            for (int64_t z = -rr; z <= rr && consistent; ++z)
                for (int64_t y = -rr; y <= rr && consistent; ++y)
                    for (int64_t x = -rr; x <= rr; ++x)
                        if (world.SampleMaterial(VoxelCoord(x, y, z)) !=
                            otherWorld.SampleMaterial(VoxelCoord(x, y, z)))
                        {
                            consistent = false;
                            break;
                        }
            if (otherWorld.ModifiedChunkCount() == 0)
                consistent = false;
        }
    }
    r.checks["eight_chunks_modified"] = eightChunks;
    r.checks["consistent_across_chunk_sizes"] = consistent;
    r.integers["changed_voxels"] = changed;
    r.integers["mesh_faces_after_flush"] = faces;
    return r;
}

// --- Workload D: repeated artillery ---------------------------------------------------------------

WorkloadResult RunArtillery(const Params& p)
{
    WorkloadResult r;
    r.name = "artillery";
    r.params = p;
    const WorldConfig cfg = BenchConfig(p);
    const std::vector<TerrainOperation> ops = ArtillerySequence(cfg, p.ops, p.radius, p.region, p.seed);
    Series& apply = r.AddSeries("apply_ns");
    Series& flush = r.AddSeries("dirty_flush_ns");
    Series& total = r.AddSeries("iteration_total_ns");
    constexpr uint32_t kFlushEvery = 16;
    StateHash hash = 0;
    bool hashStable = true;
    uint64_t changed = 0, rejected = 0, loaded = 0, modified = 0, memBytes = 0, allocs = 0, allocBytes = 0;
    uint64_t faces = 0;
    uint64_t flushedChunks = 0;
    uint64_t cpuNs = 0;
    MemoryInfo mem;
    std::vector<ChunkCoord> scratch;
    MeshCache cache;
    for (uint32_t it = 0; it < p.warmup + p.iterations; ++it)
    {
        const bool measured = it >= p.warmup;
        TerrainWorld world(cfg);
        cache.faces.clear();
        AllocDelta ad;
        const uint64_t cpuStart = CpuNs();
        ScopedTimer tt;
        changed = rejected = faces = flushedChunks = 0;
        for (size_t i = 0; i < ops.size(); ++i)
        {
            ScopedTimer t;
            const OperationResult res = world.ApplyOperation(ops[i]);
            const uint64_t ns = t.ElapsedNs();
            if (measured)
                apply.samplesNs.push_back(static_cast<double>(ns));
            if (res.Accepted())
                changed += res.changedVoxels;
            else
                ++rejected;
            if ((i + 1) % kFlushEvery == 0 || i + 1 == ops.size())
            {
                ScopedTimer tf;
                flushedChunks += FlushDirty(world, scratch, cache, &faces);
                if (measured)
                    flush.samplesNs.push_back(static_cast<double>(tf.ElapsedNs()));
            }
        }
        const uint64_t totalNs = tt.ElapsedNs();
        cpuNs = CpuNs() - cpuStart;
        if (measured)
        {
            total.samplesNs.push_back(static_cast<double>(totalNs));
            const StateHash h = world.ComputeStateHash();
            if (hash != 0 && h != hash)
                hashStable = false;
            hash = h;
            loaded = world.LoadedChunkCount();
            modified = world.ModifiedChunkCount();
            memBytes = world.EstimateMemoryBytes();
            allocs = ad.Allocations();
            allocBytes = ad.Bytes();
            mem = QueryMemory();
        }
    }
    r.checks["hash_stable_across_iterations"] = hashStable;
    r.checks["no_rejections"] = rejected == 0;
    r.integers["operations"] = ops.size();
    r.integers["changed_voxels"] = changed;
    r.integers["rejected_operations"] = rejected;
    r.integers["loaded_chunks"] = loaded;
    r.integers["modified_chunks"] = modified;
    r.integers["flushed_chunk_rebuilds"] = flushedChunks;
    r.integers["mesh_faces_total"] = faces;
    r.integers["estimated_world_bytes"] = memBytes;
    r.integers["allocations_per_iteration"] = allocs;
    r.integers["allocated_bytes_per_iteration"] = allocBytes;
    r.integers["cpu_ns_last_iteration"] = cpuNs;
    if (mem.valid)
    {
        r.integers["process_rss_bytes"] = mem.currentRssBytes;
        r.integers["process_peak_rss_bytes"] = mem.peakRssBytes;
    }
    r.hashes["state_hash"] = hash;
    if (!total.samplesNs.empty())
    {
        // Includes the synchronous mesh flush; apply_only excludes it.
        r.numbers["operations_per_second"] =
            static_cast<double>(ops.size()) / (ComputeStats(total.samplesNs).medianNs / 1e9);
        double applySum = 0;
        for (double v : apply.samplesNs)
            applySum += v;
        r.numbers["apply_only_operations_per_second"] =
            static_cast<double>(apply.samplesNs.size()) / (applySum / 1e9);
    }
    return r;
}

// --- Workload E: reconstruction -------------------------------------------------------------------

WorkloadResult RunReconstruction(const Params& p)
{
    WorkloadResult r;
    r.name = "reconstruction";
    r.params = p;
    const WorldConfig cfg = BenchConfig(p);
    const std::vector<TerrainOperation> ops = ArtillerySequence(cfg, p.ops, p.radius, p.region, p.seed);
    Series& build = r.AddSeries("initial_build_ns");
    Series& journalReplay = r.AddSeries("journal_replay_ns");
    Series& snapshotLoad = r.AddSeries("snapshot_load_ns");
    Series& compact = r.AddSeries("compact_ns");
    bool replayMatch = true, snapshotMatch = true, journalRoundTrip = true, compactedMatch = true;
    uint64_t journalBytes = 0, snapshotBytes = 0, compactedJournalBytes = 0, compactedSnapshotBytes = 0;
    StateHash hash = 0;
    for (uint32_t it = 0; it < p.warmup + p.iterations; ++it)
    {
        const bool measured = it >= p.warmup;
        TerrainWorld live(cfg);
        ScopedTimer tb;
        live.ReplayJournal(ops);
        if (measured)
            build.samplesNs.push_back(static_cast<double>(tb.ElapsedNs()));
        hash = live.ComputeStateHash();

        // Journal path: serialize journal, deserialize, regenerate from seed, replay.
        ByteWriter jw;
        live.SerializeJournal(jw);
        journalBytes = jw.Size();
        ScopedTimer tj;
        WorldConfig loadedCfg;
        std::vector<TerrainOperation> loadedOps;
        ByteReader jr(jw.Buffer());
        if (Failed(TerrainWorld::DeserializeJournal(jr, loadedCfg, loadedOps)) || loadedCfg != cfg)
            journalRoundTrip = false;
        TerrainWorld rebuilt(loadedCfg);
        rebuilt.ReplayJournal(loadedOps);
        if (measured)
            journalReplay.samplesNs.push_back(static_cast<double>(tj.ElapsedNs()));
        replayMatch =
            replayMatch && rebuilt.ComputeStateHash() == hash && rebuilt.Version() == live.Version();

        // Snapshot path: serialize modified chunks + journal, deserialize.
        ByteWriter sw;
        live.SerializeSnapshot(sw);
        snapshotBytes = sw.Size();
        ScopedTimer ts;
        std::unique_ptr<TerrainWorld> loaded;
        ByteReader sr(sw.Buffer());
        const ResultCode code = TerrainWorld::DeserializeSnapshot(sr, loaded);
        if (measured)
            snapshotLoad.samplesNs.push_back(static_cast<double>(ts.ElapsedNs()));
        snapshotMatch = snapshotMatch && Succeeded(code) && loaded && loaded->ComputeStateHash() == hash &&
                        loaded->ComputeJournalHash() == live.ComputeJournalHash();

        // Compaction path: drop the whole journal so the modified chunks are the checkpoint, then
        // round trip the compacted snapshot.
        ScopedTimer tc;
        live.CompactJournal(0);
        if (measured)
            compact.samplesNs.push_back(static_cast<double>(tc.ElapsedNs()));
        ByteWriter cw;
        live.SerializeSnapshot(cw);
        compactedSnapshotBytes = cw.Size();
        ByteWriter cjw;
        live.SerializeJournal(cjw);
        compactedJournalBytes = cjw.Size();
        std::unique_ptr<TerrainWorld> checkpoint;
        ByteReader cr(cw.Buffer());
        compactedMatch = compactedMatch && Succeeded(TerrainWorld::DeserializeSnapshot(cr, checkpoint)) &&
                         checkpoint && checkpoint->ComputeStateHash() == hash &&
                         checkpoint->Version() == live.Version();
    }
    r.checks["journal_round_trip"] = journalRoundTrip;
    r.checks["replay_hash_match"] = replayMatch;
    r.checks["snapshot_hash_match"] = snapshotMatch;
    r.checks["compacted_snapshot_hash_match"] = compactedMatch;
    r.integers["operations"] = ops.size();
    r.integers["journal_bytes"] = journalBytes;
    r.integers["snapshot_bytes"] = snapshotBytes;
    r.integers["compacted_journal_bytes"] = compactedJournalBytes;
    r.integers["compacted_snapshot_bytes"] = compactedSnapshotBytes;
    r.hashes["state_hash"] = hash;
    return r;
}

// --- Workload G: resolution sweep -----------------------------------------------------------------

// The same physical scene at successively finer voxels: level k uses voxels 2^k times smaller than
// 25 cm. Because the noise lattice spacing is a power of two in voxels, scaling noiseScaleLog2, the
// amplitude and the layer depths with the voxel size reproduces the same terrain, so every level
// measures the cost of resolution alone. The scene is a 16 m x 16 m patch with +/-2 m relief and
// `ops` craters (at most 8) of 1.5 m radius at fixed physical positions.
WorkloadResult RunResolution(const Params& p)
{
    WorkloadResult r;
    r.name = "resolution";
    r.params = p;
    bool allAccepted = true;
    for (uint32_t level = 0; level <= p.region; ++level)
    {
        const int64_t s = int64_t(1) << level;
        const int64_t halfSide = 32 * s;  // 8 m in voxels
        const int64_t halfDepth = 16 * s; // 4 m in voxels
        const int64_t chunk = static_cast<int64_t>(p.chunkSize);
        if (2 * halfSide < chunk)
            continue; // the patch is smaller than one chunk at this level
        char label[32];
        std::snprintf(label, sizeof(label), "%gcm", 25.0 / static_cast<double>(s));
        const std::string pre = std::string(label) + "_";

        WorldConfig cfg;
        cfg.seed = p.seed;
        cfg.chunkSize = p.chunkSize;
        cfg.voxelSizeMm = static_cast<uint32_t>(250 / s);
        const int32_t hc = static_cast<int32_t>(halfSide / chunk);
        const int32_t zc = static_cast<int32_t>(halfDepth >= chunk ? halfDepth / chunk : 1);
        cfg.minChunk = ChunkCoord(-hc, -hc, -zc);
        cfg.maxChunk = ChunkCoord(hc - 1, hc - 1, zc - 1);
        cfg.maxOperationRadius = static_cast<uint32_t>(64 * s);
        cfg.baseHeight = 0;
        cfg.heightAmplitude = static_cast<int32_t>(8 * s);
        cfg.noiseScaleLog2 = 6 + level;
        cfg.topsoilDepth = static_cast<uint32_t>(s);
        cfg.soilDepth = static_cast<uint32_t>(10 * s);
        cfg.bedrockHeight = -64 * s;
        TerrainWorld world(cfg);
        if (Failed(world.Status()))
        {
            r.notes.push_back(pre + "invalid configuration");
            allAccepted = false;
            continue;
        }

        AllocDelta ad;
        ScopedTimer tg;
        for (int32_t z = cfg.minChunk.z; z <= cfg.maxChunk.z; ++z)
            for (int32_t y = cfg.minChunk.y; y <= cfg.maxChunk.y; ++y)
                for (int32_t x = cfg.minChunk.x; x <= cfg.maxChunk.x; ++x)
                {
                    Chunk* c = nullptr;
                    world.EnsureChunk(ChunkCoord(x, y, z), c);
                }
        r.AddSeries(pre + "generate_all_ns").samplesNs.push_back(static_cast<double>(tg.ElapsedNs()));

        // First meshing of every chunk, as when the area becomes visible.
        MeshCache cache;
        uint64_t faces = 0, meshBytes = 0;
        ScopedTimer tm;
        world.ForEachChunk(
            [&](const Chunk& c)
            {
                faces += MeshChunk(world, c.Coord(), cache);
                meshBytes += MeshBytes(cache.mesh);
            });
        r.AddSeries(pre + "mesh_all_ns").samplesNs.push_back(static_cast<double>(tm.ElapsedNs()));
        // The same surface as greedy quads, emitted from the face cache a renderer already keeps.
        uint64_t quads = 0, greedyBytes = 0;
        MeshData greedy;
        ScopedTimer tq;
        for (const auto& kv : cache.faces)
        {
            quads += EmitGreedyChunkMesh(kv.second, greedy);
            greedyBytes += MeshBytes(greedy);
        }
        r.AddSeries(pre + "greedy_emit_all_ns").samplesNs.push_back(static_cast<double>(tq.ElapsedNs()));
        uint64_t faceCacheBytes = 0;
        for (const auto& kv : cache.faces)
            faceCacheBytes +=
                kv.second.faces.size() * sizeof(MeshFace) + kv.second.rowStart.size() * sizeof(uint32_t);

        // Craters at fixed physical positions (metres), each followed by an incremental flush.
        static const double kCratersM[8][2] = {{-4.5, -3.0}, {2.0, 4.0},  {0.0, 0.0},  {5.5, -5.0},
                                               {-6.0, 5.5},  {3.5, -1.0}, {-1.5, 2.5}, {6.5, 6.5}};
        Series& apply = r.AddSeries(pre + "apply_ns");
        Series& flush = r.AddSeries(pre + "flush_ns");
        std::vector<ChunkCoord> scratch;
        uint64_t changed = 0;
        const uint32_t craters = p.ops < 8 ? p.ops : 8;
        for (uint32_t i = 0; i < craters; ++i)
        {
            TerrainOperation op;
            op.operationId = 1 + i;
            op.sequenceNumber = 1 + i;
            op.worldId = cfg.worldId;
            op.type = OperationType::SubtractSphere;
            const double scale = 4.0 * static_cast<double>(s); // voxels per metre
            const int64_t x = static_cast<int64_t>(std::llround(kCratersM[i][0] * scale));
            const int64_t y = static_cast<int64_t>(std::llround(kCratersM[i][1] * scale));
            op.center = VoxelCoord(x, y, world.Generator().SurfaceHeight(x, y));
            op.radius = static_cast<uint32_t>(6 * s); // 1.5 m
            ScopedTimer ta;
            const OperationResult res = world.ApplyOperation(op);
            apply.samplesNs.push_back(static_cast<double>(ta.ElapsedNs()));
            allAccepted = allAccepted && res.Accepted();
            changed += res.changedVoxels;
            uint64_t flushedFaces = 0;
            ScopedTimer tf;
            FlushDirty(world, scratch, cache, &flushedFaces);
            flush.samplesNs.push_back(static_cast<double>(tf.ElapsedNs()));
        }
        uint64_t facesAfter = 0;
        for (const auto& kv : cache.faces)
            facesAfter += kv.second.faces.size();

        r.integers[pre + "voxels"] = world.LoadedChunkCount() * cfg.VoxelsPerChunk();
        r.integers[pre + "chunks"] = world.LoadedChunkCount();
        // Chunks stored as a single material without a voxel array.
        uint64_t uniform = 0;
        world.ForEachChunk([&](const Chunk& c) { uniform += c.IsUniform() ? 1u : 0u; });
        r.integers[pre + "uniform_chunks"] = uniform;
        r.integers[pre + "world_bytes"] = world.EstimateMemoryBytes();
        r.integers[pre + "faces_initial"] = faces;
        r.integers[pre + "faces_after_craters"] = facesAfter;
        r.integers[pre + "mesh_bytes"] = meshBytes;
        r.integers[pre + "face_cache_bytes"] = faceCacheBytes;
        r.integers[pre + "greedy_quads"] = quads;
        r.integers[pre + "greedy_mesh_bytes"] = greedyBytes;
        r.integers[pre + "changed_voxels"] = changed;
        r.integers[pre + "allocated_bytes"] = ad.Bytes();
        r.hashes[pre + "state_hash"] = world.ComputeStateHash();
    }
    r.checks["all_craters_accepted"] = allAccepted;
    return r;
}

// --- Workload F: extended stress ------------------------------------------------------------------

WorkloadResult RunStress(const Params& p)
{
    WorkloadResult r;
    r.name = "stress";
    r.params = p;
    const WorldConfig cfg = BenchConfig(p);
    const std::vector<TerrainOperation> ops = ArtillerySequence(cfg, p.ops, p.radius, p.region, p.seed);
    Series& apply = r.AddSeries("apply_ns");
    Series& flush = r.AddSeries("dirty_flush_ns");
    Series& memSamples = r.AddSeries("estimated_world_bytes_samples");
    constexpr uint32_t kFlushEvery = 32;
    const uint32_t sampleEvery = p.ops >= 20 ? p.ops / 20 : 1;
    TerrainWorld world(cfg);
    uint64_t rejected = 0, changed = 0, faces = 0;
    uint64_t firstSample = 0, lastSample = 0;
    std::vector<ChunkCoord> scratch;
    MeshCache cache;
    AllocDelta ad;
    const uint64_t cpuStart = CpuNs();
    ScopedTimer total;
    for (size_t i = 0; i < ops.size(); ++i)
    {
        ScopedTimer t;
        const OperationResult res = world.ApplyOperation(ops[i]);
        apply.samplesNs.push_back(static_cast<double>(t.ElapsedNs()));
        if (res.Accepted())
            changed += res.changedVoxels;
        else
            ++rejected;
        if ((i + 1) % kFlushEvery == 0 || i + 1 == ops.size())
        {
            ScopedTimer tf;
            FlushDirty(world, scratch, cache, &faces);
            flush.samplesNs.push_back(static_cast<double>(tf.ElapsedNs()));
        }
        if ((i + 1) % sampleEvery == 0)
        {
            const uint64_t bytes = world.EstimateMemoryBytes();
            memSamples.samplesNs.push_back(static_cast<double>(bytes));
            if (firstSample == 0)
                firstSample = bytes;
            lastSample = bytes;
        }
    }
    const uint64_t totalNs = total.ElapsedNs();
    const uint64_t cpuNs = CpuNs() - cpuStart;
    const StateHash hash = world.ComputeStateHash();
    // Integrity: a fresh replay of the journal must reproduce the hash.
    TerrainWorld replay(cfg);
    replay.ReplayJournal(world.Journal());
    const MemoryInfo mem = QueryMemory();
    r.checks["no_rejections"] = rejected == 0;
    r.checks["replay_hash_match"] = replay.ComputeStateHash() == hash;
    r.checks["journal_complete"] = world.Journal().size() == ops.size();
    r.integers["operations"] = ops.size();
    r.integers["changed_voxels"] = changed;
    r.integers["rejected_operations"] = rejected;
    r.integers["loaded_chunks"] = world.LoadedChunkCount();
    r.integers["modified_chunks"] = world.ModifiedChunkCount();
    r.integers["pending_dirty_chunks"] = world.PendingDirtyChunkCount();
    r.integers["mesh_faces_total"] = faces;
    r.integers["estimated_world_bytes_first_sample"] = firstSample;
    r.integers["estimated_world_bytes_final"] = lastSample;
    r.integers["journal_bytes_in_memory"] = world.Journal().size() * sizeof(TerrainOperation);
    r.integers["allocations_total"] = ad.Allocations();
    r.integers["allocated_bytes_total"] = ad.Bytes();
    r.integers["wall_ns_total"] = totalNs;
    r.integers["cpu_ns_total"] = cpuNs;
    if (mem.valid)
    {
        r.integers["process_rss_bytes"] = mem.currentRssBytes;
        r.integers["process_peak_rss_bytes"] = mem.peakRssBytes;
    }
    r.hashes["state_hash"] = hash;
    r.numbers["operations_per_second"] =
        static_cast<double>(ops.size()) / (static_cast<double>(totalNs) / 1e9);
    {
        double applySum = 0;
        for (double v : apply.samplesNs)
            applySum += v;
        r.numbers["apply_only_operations_per_second"] = static_cast<double>(ops.size()) / (applySum / 1e9);
    }
    if (firstSample > 0)
        r.numbers["memory_growth_ratio"] = static_cast<double>(lastSample) / static_cast<double>(firstSample);
    return r;
}

// --- Reporting --------------------------------------------------------------------------------------

void EmitStats(Json& j, const char* key, const LatencyStats& s)
{
    j.BeginObject(key);
    j.Integer("count", s.count);
    j.Number("min_ns", s.minNs);
    j.Number("median_ns", s.medianNs);
    j.Number("mean_ns", s.meanNs);
    j.Number("p95_ns", s.p95Ns);
    j.Number("p99_ns", s.p99Ns);
    j.Number("max_ns", s.maxNs);
    j.EndObject();
}

void PrintSummary(const WorkloadResult& r)
{
    std::printf("== %s (preset=%s iterations=%u warmup=%u ops=%u radius=%u region=%u chunk=%u) %s\n",
                r.name.c_str(), r.params.preset.c_str(), r.params.iterations, r.params.warmup, r.params.ops,
                r.params.radius, r.params.region, r.params.chunkSize,
                r.AllChecksPassed() ? "[checks OK]" : "[CHECKS FAILED]");
    for (const Series& s : r.series)
    {
        if (s.samplesNs.empty())
            continue;
        const LatencyStats st = ComputeStats(s.samplesNs);
        std::printf("   %-32s n=%-6zu median=%10.0f  p95=%10.0f  p99=%10.0f  max=%10.0f\n", s.name.c_str(),
                    st.count, st.medianNs, st.p95Ns, st.p99Ns, st.maxNs);
    }
    for (const auto& kv : r.integers)
        std::printf("   %-32s %llu\n", kv.first.c_str(), static_cast<unsigned long long>(kv.second));
    for (const auto& kv : r.numbers)
        std::printf("   %-32s %.3f\n", kv.first.c_str(), kv.second);
    for (const auto& kv : r.hashes)
        std::printf("   %-32s 0x%016llx\n", kv.first.c_str(), static_cast<unsigned long long>(kv.second));
    for (const auto& kv : r.checks)
        std::printf("   check %-26s %s\n", kv.first.c_str(), kv.second ? "pass" : "FAIL");
}

} // namespace

int main(int argc, char** argv)
{
    Args args = ParseArgs(argc, argv);
    // A workload config file holds "key=value" lines using the same keys as the command line; explicit
    // command-line options override it. See Benchmarks/workloads/.
    if (args.Has("config"))
    {
        Args fileArgs;
        if (!LoadConfigFile(args.Get("config", ""), fileArgs))
        {
            std::fprintf(stderr, "cannot read config file %s\n", args.Get("config", "").c_str());
            return 2;
        }
        for (const auto& kv : fileArgs.values)
            if (!args.Has(kv.first))
                args.values[kv.first] = kv.second;
    }
    const char* kAll[] = {"generation",     "isolated_crater", "boundary_crater", "artillery",
                          "reconstruction", "stress",          "resolution"};

    if (args.Has("list") || args.Has("help"))
    {
        std::printf("workloads: ");
        for (const char* w : kAll)
            std::printf("%s ", w);
        std::printf(
            "all\npresets: smoke default large\noptions: --iterations --warmup --seed --chunk-size --ops "
            "--radius --region --label --out\n");
        return 0;
    }

    Params base;
    base.preset = args.Get("preset", "default");
    base.seed = args.GetU64("seed", base.seed);
    base.chunkSize = static_cast<uint32_t>(args.GetU64("chunk-size", base.chunkSize));
    if (args.Has("iterations"))
    {
        base.iterations = static_cast<uint32_t>(args.GetU64("iterations", 0));
        base.explicitIterations = true;
    }
    if (args.Has("warmup"))
    {
        base.warmup = static_cast<uint32_t>(args.GetU64("warmup", 0));
        base.explicitWarmup = true;
    }
    if (args.Has("ops"))
    {
        base.ops = static_cast<uint32_t>(args.GetU64("ops", 0));
        base.explicitOps = true;
    }
    if (args.Has("radius"))
    {
        base.radius = static_cast<uint32_t>(args.GetU64("radius", 0));
        base.explicitRadius = true;
    }
    if (args.Has("region"))
    {
        base.region = static_cast<uint32_t>(args.GetU64("region", 0));
        base.explicitRegion = true;
    }
    if (Failed(BenchConfig(base).Validate()))
    {
        std::fprintf(stderr,
                     "invalid benchmark configuration (chunk-size must be a power of two in [8,128])\n");
        return 2;
    }

    std::vector<std::string> selected;
    const std::string wl = args.Get("workload", "all");
    if (wl == "all")
        for (const char* w : kAll)
            selected.push_back(w);
    else
    {
        size_t start = 0;
        while (start <= wl.size())
        {
            const size_t comma = wl.find(',', start);
            const std::string item =
                wl.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
            if (!item.empty())
                selected.push_back(item);
            if (comma == std::string::npos)
                break;
            start = comma + 1;
        }
    }

    std::vector<WorkloadResult> results;
    bool allChecks = true;
    for (const std::string& name : selected)
    {
        const Params p = Resolve(base, name);
        WorkloadResult r;
        if (name == "generation")
            r = RunGeneration(p);
        else if (name == "isolated_crater")
            r = RunIsolatedCrater(p);
        else if (name == "boundary_crater")
            r = RunBoundaryCrater(p);
        else if (name == "artillery")
            r = RunArtillery(p);
        else if (name == "reconstruction")
            r = RunReconstruction(p);
        else if (name == "stress")
            r = RunStress(p);
        else if (name == "resolution")
            r = RunResolution(p);
        else
        {
            std::fprintf(stderr, "unknown workload: %s\n", name.c_str());
            return 2;
        }
        PrintSummary(r);
        allChecks = allChecks && r.AllChecksPassed();
        results.push_back(r);
    }

    Json j;
    j.BeginObject();
    j.BeginObject("harness");
    j.String("name", "ob_bench");
    j.String("version", CoreVersionString());
    j.Integer("schema", 1);
    j.EndObject();
    j.BeginObject("build");
    j.String("commit", OB_GIT_COMMIT);
    j.String("build_type", OB_BUILD_TYPE);
    j.String("compiler", OB_CXX_COMPILER_ID);
    j.String("compiler_version", OB_CXX_COMPILER_VERSION);
    j.String("cxx_standard", OB_CXX_STANDARD);
    j.String("platform", PlatformName());
    j.String("arch", ArchName());
    j.EndObject();
    j.BeginObject("run");
    j.String("timestamp_utc", UtcTimestamp());
    j.String("label", args.Get("label", ""));
    j.String("preset", base.preset);
    j.Integer("seed", base.seed);
    j.Integer("chunk_size", base.chunkSize);
    j.Bool("all_checks_passed", allChecks);
    j.EndObject();
    j.BeginArray("workloads");
    for (const WorkloadResult& r : results)
    {
        j.BeginObject();
        j.String("name", r.name);
        j.BeginObject("params");
        j.String("preset", r.params.preset);
        j.Integer("iterations", r.params.iterations);
        j.Integer("warmup", r.params.warmup);
        j.Integer("ops", r.params.ops);
        j.Integer("radius", r.params.radius);
        j.Integer("region", r.params.region);
        j.Integer("chunk_size", r.params.chunkSize);
        j.Integer("seed", r.params.seed);
        j.EndObject();
        j.BeginObject("latency");
        for (const Series& s : r.series)
            if (!s.samplesNs.empty())
                EmitStats(j, s.name.c_str(), ComputeStats(s.samplesNs));
        j.EndObject();
        j.BeginObject("metrics");
        for (const auto& kv : r.integers)
            j.Integer(kv.first.c_str(), kv.second);
        for (const auto& kv : r.numbers)
            j.Number(kv.first.c_str(), kv.second);
        j.EndObject();
        j.BeginObject("hashes");
        for (const auto& kv : r.hashes)
            j.Hex64(kv.first.c_str(), kv.second);
        j.EndObject();
        j.BeginObject("checks");
        for (const auto& kv : r.checks)
            j.Bool(kv.first.c_str(), kv.second);
        j.EndObject();
        j.EndObject();
    }
    j.EndArray();
    j.EndObject();

    const std::string out = args.Get("out", "");
    if (!out.empty())
    {
        if (FILE* f = std::fopen(out.c_str(), "wb"))
        {
            std::fputs(j.Str().c_str(), f);
            std::fputs("\n", f);
            std::fclose(f);
            std::printf("wrote %s\n", out.c_str());
        }
        else
        {
            std::fprintf(stderr, "cannot write %s\n", out.c_str());
            return 2;
        }
    }
    return allChecks ? 0 : 1;
}
