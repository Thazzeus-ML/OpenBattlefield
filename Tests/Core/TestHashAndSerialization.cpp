// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
#include "ObTest.h"

#include "OpenBattlefieldCore/ObChunk.h"
#include "OpenBattlefieldCore/ObHash.h"
#include "OpenBattlefieldCore/ObSerialization.h"
#include "OpenBattlefieldCore/ObTerrainOperation.h"
#include "OpenBattlefieldCore/ObWorldConfig.h"

using namespace ob;

OB_TEST(ByteWriterIsLittleEndianAndReaderRoundTrips)
{
    ByteWriter w;
    w.U8(0xAB);
    w.U16(0x1234);
    w.U32(0x89ABCDEFu);
    w.U64(0x0123456789ABCDEFull);
    w.I32(-2);
    w.I64(-3);
    const std::vector<uint8_t>& b = w.Buffer();
    OB_REQUIRE(b.size() == 1 + 2 + 4 + 8 + 4 + 8);
    OB_CHECK_EQ(b[0], 0xAB);
    OB_CHECK_EQ(b[1], 0x34);
    OB_CHECK_EQ(b[2], 0x12);
    OB_CHECK_EQ(b[3], 0xEF);
    OB_CHECK_EQ(b[6], 0x89);
    OB_CHECK_EQ(b[7], 0xEF);
    OB_CHECK_EQ(b[14], 0x01);

    ByteReader r(b);
    uint8_t u8 = 0;
    uint16_t u16 = 0;
    uint32_t u32 = 0;
    uint64_t u64 = 0;
    int32_t i32 = 0;
    int64_t i64 = 0;
    OB_CHECK(r.U8(u8) && u8 == 0xAB);
    OB_CHECK(r.U16(u16) && u16 == 0x1234);
    OB_CHECK(r.U32(u32) && u32 == 0x89ABCDEFu);
    OB_CHECK(r.U64(u64) && u64 == 0x0123456789ABCDEFull);
    OB_CHECK(r.I32(i32) && i32 == -2);
    OB_CHECK(r.I64(i64) && i64 == -3);
    OB_CHECK(r.AtEnd());
    OB_CHECK(!r.U8(u8));
    OB_CHECK(!r.Ok());
}

OB_TEST(FnvHasherMatchesKnownVectors)
{
    // FNV-1a 64 test vectors.
    OB_CHECK_EQ(HashBytes("", 0), 0xCBF29CE484222325ull);
    OB_CHECK_EQ(HashBytes("a", 1), 0xAF63DC4C8601EC8Cull);
    OB_CHECK_EQ(HashBytes("foobar", 6), 0x85944171F73967E8ull);
    Hasher h;
    h.U32(0x61626364u); // "abcd" little endian -> bytes 'd','c','b','a'
    OB_CHECK_EQ(h.Finish(), HashBytes("dcba", 4));
}

OB_TEST(Mix64IsDeterministicAndNonTrivial)
{
    OB_CHECK(Mix64(0) != 0);
    OB_CHECK(Mix64(1) != Mix64(2));
    OB_CHECK_EQ(Mix64(0), 0xE220A8397B1DCDAFull); // splitmix64 first output for seed 0
}

OB_TEST(WorldConfigSerializationRoundTrips)
{
    WorldConfig cfg;
    cfg.seed = 0xDEADBEEF;
    cfg.chunkSize = 16;
    cfg.minChunk = ChunkCoord(-3, -2, -1);
    cfg.maxChunk = ChunkCoord(3, 2, 1);
    cfg.bedrockHeight = -7;
    OB_REQUIRE(Succeeded(cfg.Validate()));

    ByteWriter w;
    cfg.Serialize(w);
    ByteReader r(w.Buffer());
    WorldConfig back;
    OB_REQUIRE(Succeeded(back.Deserialize(r)));
    OB_CHECK(back == cfg);
    OB_CHECK(r.AtEnd());

    // Truncation is reported, not crashed.
    std::vector<uint8_t> cut(w.Buffer().begin(), w.Buffer().end() - 3);
    ByteReader rc(cut);
    OB_CHECK_EQ(back.Deserialize(rc), ResultCode::SerializationTruncated);

    // Invalid content (chunk size 7) is rejected.
    std::vector<uint8_t> bad = w.Buffer();
    bad[2 + 8 + 8 + 4] = 7;
    ByteReader rb(bad);
    OB_CHECK_EQ(back.Deserialize(rb), ResultCode::SerializationCorrupt);
}

OB_TEST(WorldConfigValidationRejectsBadInput)
{
    WorldConfig cfg;
    OB_CHECK(Succeeded(cfg.Validate()));
    cfg.chunkSize = 24;
    OB_CHECK_EQ(cfg.Validate(), ResultCode::InvalidChunkSize);
    cfg.chunkSize = 4;
    OB_CHECK_EQ(cfg.Validate(), ResultCode::InvalidChunkSize);
    cfg.chunkSize = 32;
    cfg.minChunk = ChunkCoord(1, 0, 0);
    cfg.maxChunk = ChunkCoord(0, 0, 0);
    OB_CHECK_EQ(cfg.Validate(), ResultCode::InvalidWorldBounds);
    cfg = WorldConfig();
    cfg.maxOperationRadius = 0;
    OB_CHECK_EQ(cfg.Validate(), ResultCode::InvalidConfig);
    cfg = WorldConfig();
    cfg.worldId = 0;
    OB_CHECK_EQ(cfg.Validate(), ResultCode::InvalidConfig);
}

OB_TEST(OperationSerializationRoundTrips)
{
    TerrainOperation op;
    op.operationId = 42;
    op.type = OperationType::AddSphere;
    op.worldId = 1;
    op.sequenceNumber = 7;
    op.center = VoxelCoord(-5, 6, -7);
    op.radius = 3;
    op.material = Material::Rock;
    ByteWriter w;
    op.Serialize(w);
    OB_CHECK_EQ(w.Size(), size_t(8 + 2 + 1 + 8 + 8 + 24 + 4 + 1));
    ByteReader r(w.Buffer());
    TerrainOperation back;
    OB_REQUIRE(Succeeded(back.Deserialize(r)));
    OB_CHECK(back == op);

    std::vector<uint8_t> bad = w.Buffer();
    bad[10] = 99; // operation type byte
    ByteReader rb(bad);
    OB_CHECK_EQ(back.Deserialize(rb), ResultCode::SerializationCorrupt);
}

OB_TEST(ChunkSerializationRoundTripsWithRunLengthEncoding)
{
    Chunk chunk(ChunkCoord(-2, 3, -4), 16);
    for (uint32_t z = 0; z < 8; ++z)
        for (uint32_t y = 0; y < 16; ++y)
            for (uint32_t x = 0; x < 16; ++x)
                chunk.SetRaw(LocalCoord(x, y, z), Material::Soil);
    chunk.Modify(LocalCoord(3, 3, 3), Material::Air);
    chunk.Modify(LocalCoord(5, 5, 12), Material::Rock);
    chunk.CommitModification();
    chunk.RecountSolids();

    ByteWriter w;
    chunk.Serialize(w);
    // Half solid, half air with two single-voxel runs splitting them: far smaller than 4096 bytes.
    OB_CHECK(w.Size() < 100);

    Chunk back(ChunkCoord(), 16);
    ByteReader r(w.Buffer());
    OB_REQUIRE(Succeeded(Chunk::Deserialize(r, 16, back)));
    OB_CHECK(back.Coord() == chunk.Coord());
    OB_CHECK_EQ(back.Version(), chunk.Version());
    OB_CHECK_EQ(back.SolidCount(), chunk.SolidCount());
    for (size_t i = 0; i < chunk.VoxelCount(); ++i)
        OB_CHECK(back.GetAt(i) == chunk.GetAt(i));

    Hasher h1;
    Hasher h2;
    chunk.HashContentInto(h1);
    back.HashContentInto(h2);
    OB_CHECK_EQ(h1.Finish(), h2.Finish());

    // Wrong expected chunk size is rejected.
    Chunk wrongSize(ChunkCoord(), 32);
    ByteReader r2(w.Buffer());
    OB_CHECK_EQ(Chunk::Deserialize(r2, 32, wrongSize), ResultCode::SerializationCorrupt);

    // Corrupt run length that overflows the chunk is rejected.
    std::vector<uint8_t> bad = w.Buffer();
    bad[4 + 2 + 12 + 4 + 8 + 4 + 1] = 0xFF; // first run length low byte
    ByteReader r3(bad);
    OB_CHECK_EQ(Chunk::Deserialize(r3, 16, back), ResultCode::SerializationCorrupt);
}

OB_TEST_MAIN()
