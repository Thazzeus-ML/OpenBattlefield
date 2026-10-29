// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
#pragma once

#include "OpenBattlefieldCore/ObTypes.h"

#include <vector>

namespace ob
{

/// Canonical little-endian byte writer. All persistent and hashed state passes through this class so
/// that the on-disk / on-wire representation is identical on every platform.
class OBCORE_API ByteWriter
{
public:
    ByteWriter() = default;

    void U8(uint8_t v) { buffer_.push_back(v); }
    void U16(uint16_t v);
    void U32(uint32_t v);
    void U64(uint64_t v);
    void I32(int32_t v) { U32(static_cast<uint32_t>(v)); }
    void I64(int64_t v) { U64(static_cast<uint64_t>(v)); }
    void Bytes(const void* data, size_t size);

    void Voxel(const VoxelCoord& c)
    {
        I64(c.x);
        I64(c.y);
        I64(c.z);
    }
    void Chunk(const ChunkCoord& c)
    {
        I32(c.x);
        I32(c.y);
        I32(c.z);
    }

    const std::vector<uint8_t>& Buffer() const { return buffer_; }
    std::vector<uint8_t>&& Take() { return static_cast<std::vector<uint8_t>&&>(buffer_); }
    size_t Size() const { return buffer_.size(); }
    void Reserve(size_t bytes) { buffer_.reserve(bytes); }
    void Clear() { buffer_.clear(); }

private:
    std::vector<uint8_t> buffer_;
};

/// Bounds-checked little-endian byte reader. Every read reports truncation through a sticky error
/// flag; callers check Ok() (or the returned ResultCode of higher-level deserializers).
class OBCORE_API ByteReader
{
public:
    ByteReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}
    explicit ByteReader(const std::vector<uint8_t>& buffer) : data_(buffer.data()), size_(buffer.size()) {}

    bool U8(uint8_t& v);
    bool U16(uint16_t& v);
    bool U32(uint32_t& v);
    bool U64(uint64_t& v);
    bool I32(int32_t& v);
    bool I64(int64_t& v);
    bool Bytes(void* out, size_t size);
    bool Skip(size_t size);

    bool Voxel(VoxelCoord& c) { return I64(c.x) && I64(c.y) && I64(c.z); }
    bool Chunk(ChunkCoord& c) { return I32(c.x) && I32(c.y) && I32(c.z); }

    bool Ok() const { return !truncated_; }
    bool AtEnd() const { return pos_ == size_; }
    size_t Remaining() const { return size_ - pos_; }
    size_t Position() const { return pos_; }

private:
    bool Need(size_t n);

    const uint8_t* data_ = nullptr;
    size_t size_ = 0;
    size_t pos_ = 0;
    bool truncated_ = false;
};

} // namespace ob
