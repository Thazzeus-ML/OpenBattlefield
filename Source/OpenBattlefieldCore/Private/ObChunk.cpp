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
      materials_(static_cast<size_t>(chunkSize) * static_cast<size_t>(chunkSize) *
                     static_cast<size_t>(chunkSize),
                 static_cast<uint8_t>(Material::Air))
{
}

bool Chunk::SetRaw(const LocalCoord& c, Material m)
{
    uint8_t& slot = materials_[LocalIndex(c, size_)];
    const uint8_t v = static_cast<uint8_t>(m);
    if (slot == v)
        return false;
    slot = v;
    return true;
}

bool Chunk::Modify(const LocalCoord& c, Material m)
{
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
    uint32_t n = 0;
    for (uint8_t m : materials_)
        n += IsSolid(static_cast<Material>(m)) ? 1u : 0u;
    solidCount_ = n;
}

void Chunk::HashContentInto(Hasher& h) const
{
    h.Chunk(coord_);
    h.U32(size_);
    h.Bytes(materials_.data(), materials_.size());
}

void Chunk::Serialize(ByteWriter& w) const
{
    w.Bytes(kChunkMagic, 4);
    w.U16(kSchemaVersion);
    w.Chunk(coord_);
    w.U32(size_);
    w.U64(version_);

    // Run-length encode: (material u8, run length u32). Count runs first for a fixed-size prefix.
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
    if (runCount == 0 || runCount > out.materials_.size())
        return ResultCode::SerializationCorrupt;

    out.coord_ = coord;
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
    return ResultCode::Ok;
}

} // namespace ob
