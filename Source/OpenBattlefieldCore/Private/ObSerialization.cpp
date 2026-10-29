// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
#include "OpenBattlefieldCore/ObSerialization.h"

#include <cstring>

namespace ob
{

void ByteWriter::U16(uint16_t v)
{
    buffer_.push_back(static_cast<uint8_t>(v & 0xFF));
    buffer_.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void ByteWriter::U32(uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        buffer_.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
}

void ByteWriter::U64(uint64_t v)
{
    for (int i = 0; i < 8; ++i)
        buffer_.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
}

void ByteWriter::Bytes(const void* data, size_t size)
{
    const uint8_t* p = static_cast<const uint8_t*>(data);
    buffer_.insert(buffer_.end(), p, p + size);
}

bool ByteReader::Need(size_t n)
{
    if (truncated_ || n > size_ - pos_)
    {
        truncated_ = true;
        return false;
    }
    return true;
}

bool ByteReader::U8(uint8_t& v)
{
    if (!Need(1))
        return false;
    v = data_[pos_++];
    return true;
}

bool ByteReader::U16(uint16_t& v)
{
    if (!Need(2))
        return false;
    v = static_cast<uint16_t>(data_[pos_] | (static_cast<uint16_t>(data_[pos_ + 1]) << 8));
    pos_ += 2;
    return true;
}

bool ByteReader::U32(uint32_t& v)
{
    if (!Need(4))
        return false;
    uint32_t r = 0;
    for (int i = 0; i < 4; ++i)
        r |= static_cast<uint32_t>(data_[pos_ + static_cast<size_t>(i)]) << (8 * i);
    pos_ += 4;
    v = r;
    return true;
}

bool ByteReader::U64(uint64_t& v)
{
    if (!Need(8))
        return false;
    uint64_t r = 0;
    for (int i = 0; i < 8; ++i)
        r |= static_cast<uint64_t>(data_[pos_ + static_cast<size_t>(i)]) << (8 * i);
    pos_ += 8;
    v = r;
    return true;
}

bool ByteReader::I32(int32_t& v)
{
    uint32_t u = 0;
    if (!U32(u))
        return false;
    v = static_cast<int32_t>(u);
    return true;
}

bool ByteReader::I64(int64_t& v)
{
    uint64_t u = 0;
    if (!U64(u))
        return false;
    v = static_cast<int64_t>(u);
    return true;
}

bool ByteReader::Bytes(void* out, size_t size)
{
    if (!Need(size))
        return false;
    if (size > 0)
        std::memcpy(out, data_ + pos_, size);
    pos_ += size;
    return true;
}

bool ByteReader::Skip(size_t size)
{
    if (!Need(size))
        return false;
    pos_ += size;
    return true;
}

} // namespace ob
