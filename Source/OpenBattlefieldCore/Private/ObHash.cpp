// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
#include "OpenBattlefieldCore/ObHash.h"

namespace ob
{

void Hasher::Bytes(const void* data, size_t size)
{
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i)
        Byte(p[i]);
}

void Hasher::U16(uint16_t v)
{
    Byte(static_cast<uint8_t>(v & 0xFF));
    Byte(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void Hasher::U32(uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        Byte(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
}

void Hasher::U64(uint64_t v)
{
    for (int i = 0; i < 8; ++i)
        Byte(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
}

StateHash HashBytes(const void* data, size_t size)
{
    Hasher h;
    h.Bytes(data, size);
    return h.Finish();
}

} // namespace ob
