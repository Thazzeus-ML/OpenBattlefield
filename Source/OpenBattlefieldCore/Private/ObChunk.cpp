// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
#include "OpenBattlefieldCore/ObChunk.h"

#include "OpenBattlefieldCore/ObHash.h"
#include "OpenBattlefieldCore/ObSerialization.h"

namespace ob
{

namespace
{
constexpr uint8_t kChunkMagic[4] = {'O', 'B', 'C', 'H'};
}

Chunk::Chunk(const ChunkCoord& coord, uint32_t chunkSize)
    : coord_(coord), size_(chunkSize),
      voxelCount_(static_cast<size_t>(chunkSize) * static_cast<size_t>(chunkSize) *
                  static_cast<size_t>(chunkSize)),
      uniform_(static_cast<uint8_t>(Material::Air))
{
    // Starts uniform Air: no voxel array until a voxel differs.
}

void Chunk::Materialize()
{
    materials_.assign(voxelCount_, uniform_);
}

bool Chunk::Compact()
{
    if (materials_.empty())
        return true;
    const uint8_t first = materials_[0];
    for (uint8_t m : materials_)
        if (m != first)
            return false;
    uniform_ = first;
    std::vector<uint8_t>().swap(materials_); // release the memory, not just the size
    return true;
}

bool Chunk::SetRaw(const LocalCoord& c, Material m)
{
    if (materials_.empty())
    {
        if (static_cast<uint8_t>(m) == uniform_)
            return false;
        Materialize();
    }
    uint8_t& slot = materials_[LocalIndex(c, size_)];
    const uint8_t v = static_cast<uint8_t>(m);
    if (slot == v)
        return false;
    slot = v;
    return true;
}

bool Chunk::Modify(const LocalCoord& c, Material m)
{
    if (materials_.empty())
    {
        if (static_cast<uint8_t>(m) == uniform_)
            return false;
        Materialize();
    }
    uint8_t& slot = materials_[LocalIndex(c, size_)];
    const uint8_t v = static_cast<uint8_t>(m);
    if (slot == v)
        return false;
    const bool wasSolid = IsSolid(static_cast<Material>(slot));
    const bool isSolid = IsSolid(m);
    if (wasSolid && !isSolid)
        --solidCount_;
    else if (!wasSolid && isSolid)
        ++solidCount_;
    slot = v;
    dirty_.Include(c);
    return true;
}

DirtyRegion Chunk::ConsumeDirtyRegion()
{
    const DirtyRegion r = dirty_;
    dirty_.Reset();
    return r;
}

void Chunk::RecountSolids()
{
    if (materials_.empty())
    {
        solidCount_ = IsSolid(static_cast<Material>(uniform_)) ? static_cast<uint32_t>(voxelCount_) : 0u;
        return;
    }
    uint32_t n = 0;
    for (uint8_t m : materials_)
        n += IsSolid(static_cast<Material>(m)) ? 1u : 0u;
    solidCount_ = n;
}

void Chunk::HashContentInto(Hasher& h) const
{
    h.Chunk(coord_);
    h.U32(size_);
    if (!materials_.empty())
    {
        h.Bytes(materials_.data(), materials_.size());
        return;
    }
    // The same byte sequence a dense array of the uniform material would hash.
    uint8_t block[4096];
    for (uint8_t& b : block)
        b = uniform_;
    for (size_t left = voxelCount_; left > 0;)
    {
        const size_t n = left < sizeof(block) ? left : sizeof(block);
        h.Bytes(block, n);
        left -= n;
    }
}

void Chunk::Serialize(ByteWriter& w) const
{
    w.Bytes(kChunkMagic, 4);
    w.U16(kSchemaVersion);
    w.Chunk(coord_);
    w.U32(size_);
    w.U64(version_);

    // Run-length encode: (material u8, run length u32). Count runs first for a fixed-size prefix.
    if (materials_.empty())
    {
        w.U32(1); // a uniform chunk is exactly one run, as its dense array would encode
        w.U8(uniform_);
        w.U32(static_cast<uint32_t>(voxelCount_));
        return;
    }
    uint32_t runCount = 0;
    {
        size_t i = 0;
        while (i < materials_.size())
        {
            const uint8_t m = materials_[i];
            size_t j = i + 1;
            while (j < materials_.size() && materials_[j] == m)
                ++j;
            ++runCount;
            i = j;
        }
    }
    w.U32(runCount);
    size_t i = 0;
    while (i < materials_.size())
    {
        const uint8_t m = materials_[i];
        size_t j = i + 1;
        while (j < materials_.size() && materials_[j] == m)
            ++j;
        w.U8(m);
        w.U32(static_cast<uint32_t>(j - i));
        i = j;
    }
}

ResultCode Chunk::Deserialize(ByteReader& r, uint32_t expectedChunkSize, Chunk& out)
{
    uint8_t magic[4] = {0, 0, 0, 0};
    if (!r.Bytes(magic, 4))
        return ResultCode::SerializationTruncated;
    for (int i = 0; i < 4; ++i)
        if (magic[i] != kChunkMagic[i])
            return ResultCode::SerializationBadMagic;

    uint16_t schema = 0;
    if (!r.U16(schema))
        return ResultCode::SerializationTruncated;
    if (schema != kSchemaVersion)
        return ResultCode::SerializationVersionMismatch;

    ChunkCoord coord;
    uint32_t size = 0;
    uint64_t version = 0;
    uint32_t runCount = 0;
    if (!r.Chunk(coord) || !r.U32(size) || !r.U64(version) || !r.U32(runCount))
        return ResultCode::SerializationTruncated;
    if (size != expectedChunkSize || size != out.size_)
        return ResultCode::SerializationCorrupt;
    if (runCount == 0 || runCount > out.voxelCount_)
        return ResultCode::SerializationCorrupt;

    out.coord_ = coord;
    if (runCount == 1)
    {
        // A single run covering the chunk stays uniform; no voxel array is allocated.
        uint8_t m = 0;
        uint32_t len = 0;
        if (!r.U8(m) || !r.U32(len))
            return ResultCode::SerializationTruncated;
        if (!IsValidMaterial(m) || len != out.voxelCount_)
            return ResultCode::SerializationCorrupt;
        std::vector<uint8_t>().swap(out.materials_);
        out.uniform_ = m;
        out.version_ = version;
        out.dirty_.Reset();
        out.RecountSolids();
        return ResultCode::Ok;
    }
    out.materials_.assign(out.voxelCount_, static_cast<uint8_t>(Material::Air));
    size_t pos = 0;
    for (uint32_t run = 0; run < runCount; ++run)
    {
        uint8_t m = 0;
        uint32_t len = 0;
        if (!r.U8(m) || !r.U32(len))
            return ResultCode::SerializationTruncated;
        if (!IsValidMaterial(m) || len == 0 || len > out.materials_.size() - pos)
            return ResultCode::SerializationCorrupt;
        for (uint32_t k = 0; k < len; ++k)
            out.materials_[pos + k] = m;
        pos += len;
    }
    if (pos != out.materials_.size())
        return ResultCode::SerializationCorrupt;

    out.version_ = version;
    out.dirty_.Reset();
    out.RecountSolids();
    out.Compact(); // several runs can still be one material
    return ResultCode::Ok;
}

} // namespace ob
