// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
#pragma once

#include "OpenBattlefieldCore/ObCoordinates.h"
#include "OpenBattlefieldCore/ObTypes.h"

#include <vector>

namespace ob
{

class ByteWriter;
class ByteReader;
class Hasher;

/// Inclusive local-coordinate box of voxels whose mesh faces may have changed since the region was
/// last consumed: voxels changed in this chunk, plus boundary voxels facing a change in a
/// face-adjacent chunk (TerrainWorld marks those, see MarkFaceNeighboursDirty).
struct DirtyRegion
{
    bool any = false;
    LocalCoord min;
    LocalCoord max;

    void Reset()
    {
        any = false;
        min = LocalCoord();
        max = LocalCoord();
    }

    void Include(const LocalCoord& c)
    {
        if (!any)
        {
            any = true;
            min = c;
            max = c;
            return;
        }
        if (c.x < min.x)
            min.x = c.x;
        if (c.y < min.y)
            min.y = c.y;
        if (c.z < min.z)
            min.z = c.z;
        if (c.x > max.x)
            max.x = c.x;
        if (c.y > max.y)
            max.y = c.y;
        if (c.z > max.z)
            max.z = c.z;
    }
};

/// Material grid for one chunk (see Docs/terrain.md). A chunk whose voxels all hold the same material
/// is stored as that one value with no voxel array ("uniform"); the first change that breaks the
/// uniformity allocates the dense array, and Compact() frees it again. Content, hashes and the
/// serialized form do not depend on which representation is in use.
///
/// Ownership and lifecycle: chunks are owned exclusively by a TerrainWorld. They are never shared
/// between threads; worker threads receive immutable ChunkMeshInput snapshots instead.
class OBCORE_API Chunk
{
public:
    /// Serialization schema version of the chunk format.
    static constexpr uint16_t kSchemaVersion = 1;

    Chunk(const ChunkCoord& coord, uint32_t chunkSize);

    const ChunkCoord& Coord() const { return coord_; }
    uint32_t Size() const { return size_; }
    size_t VoxelCount() const { return voxelCount_; }

    /// Number of times this chunk's authoritative content has changed since generation.
    ChunkVersion Version() const { return version_; }
    /// True if any operation has ever changed a voxel in this chunk.
    bool IsModified() const { return version_ != 0; }

    Material Get(const LocalCoord& c) const
    {
        return static_cast<Material>(materials_.empty() ? uniform_ : materials_[LocalIndex(c, size_)]);
    }
    Material GetAt(size_t index) const
    {
        return static_cast<Material>(materials_.empty() ? uniform_ : materials_[index]);
    }

    /// True while every voxel holds UniformMaterial() and no voxel array is allocated.
    bool IsUniform() const { return materials_.empty(); }
    /// The material of every voxel while IsUniform().
    Material UniformMaterial() const { return static_cast<Material>(uniform_); }
    /// Frees the voxel array if every voxel holds the same material. Returns true if the chunk is
    /// uniform afterwards. Called after generation and deserialization.
    bool Compact();

    /// Sets a voxel without touching version or dirty state. Used by the generator. Returns true if
    /// the stored value changed.
    bool SetRaw(const LocalCoord& c, Material m);

    /// Sets a voxel as part of an authoritative modification: updates the dirty region and the solid
    /// count. Does NOT bump the version; callers bump once per operation via CommitModification.
    /// Returns true if the stored value changed.
    bool Modify(const LocalCoord& c, Material m);

    /// Increments the version after one or more Modify calls changed content.
    void CommitModification() { ++version_; }

    /// Number of non-Air voxels.
    uint32_t SolidCount() const { return solidCount_; }
    bool IsAllAir() const { return solidCount_ == 0; }
    bool IsAllSolid() const { return solidCount_ == voxelCount_; }

    const DirtyRegion& Dirty() const { return dirty_; }
    bool HasDirtyRegion() const { return dirty_.any; }
    /// Returns the dirty region and clears it.
    DirtyRegion ConsumeDirtyRegion();
    /// Grows the dirty region by an inclusive box without changing content or version. Used when a
    /// neighbour's change alters this chunk's boundary faces.
    void IncludeInDirtyRegion(const LocalCoord& min, const LocalCoord& max)
    {
        dirty_.Include(min);
        dirty_.Include(max);
    }

    /// Direct access to the dense material array (X fastest), or nullptr while IsUniform(). Intended
    /// for snapshot creation; mutation through this pointer bypasses bookkeeping and is forbidden.
    const uint8_t* Data() const { return materials_.empty() ? nullptr : materials_.data(); }

    /// Recomputes the solid count from the material array. Called after bulk generation.
    void RecountSolids();

    /// Approximate heap footprint in bytes.
    size_t MemoryBytes() const { return materials_.capacity() + sizeof(Chunk); }

    /// Canonical content hash input: coordinate, size and every material. Version is excluded so the
    /// hash depends only on reconstructable content.
    void HashContentInto(Hasher& h) const;

    /// Serializes coordinate, version and run-length-encoded materials.
    void Serialize(ByteWriter& w) const;

    /// Deserializes a chunk written by Serialize. On failure the chunk is left in an unspecified but
    /// valid state and a non-Ok code is returned.
    static ResultCode Deserialize(ByteReader& r, uint32_t expectedChunkSize, Chunk& out);

    /// Overwrites the version. Only used by deserialization and tests.
    void SetVersionForDeserialization(ChunkVersion v) { version_ = v; }

private:
    /// Allocates the dense array filled with the uniform material, before a change that breaks it.
    void Materialize();

    ChunkCoord coord_;
    uint32_t size_ = 0;
    size_t voxelCount_ = 0;
    uint8_t uniform_ = 0; // Material::Air; meaningful only while materials_ is empty
    ChunkVersion version_ = 0;
    uint32_t solidCount_ = 0;
    DirtyRegion dirty_;
    std::vector<uint8_t> materials_;
};

} // namespace ob
