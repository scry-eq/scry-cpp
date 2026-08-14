/*
 * rustdecode_test.cpp — smoke test that the seq::rust::decode_* FFI works across
 * the cxx ABI from C++. Deliberately minimal: the parsers have exhaustive Rust
 * unit tests (`cargo test` in scry-decoder-rs) and the tier-2 replay goldens
 * cover full Live decode byte-identically. This guards the C++-side bridge in
 * CI, where the developer-local replay goldens don't run.
 */
#include <cstring>

#include <QtTest/QtTest>

#include "seq-bridge-cxx/lib.h"

namespace {
void putU32(uint8_t* b, size_t off, uint32_t v) { std::memcpy(b + off, &v, 4); }
void putI32(uint8_t* b, size_t off, int32_t v)  { std::memcpy(b + off, &v, 4); }
}

class RustDecodeTest : public QObject
{
    Q_OBJECT
private slots:
    void deleteSpawn();
    void clientTarget();
    void consider();
    void rejectsBadLength();
};

void RustDecodeTest::deleteSpawn()
{
    uint8_t buf[4];
    putU32(buf, 0, 0x12345678u);
    auto out = seq::rust::decode_delete_spawn(
        rust::Slice<const uint8_t>{buf, sizeof(buf)});
    QVERIFY(out.ok);
    QCOMPARE(out.spawn_id, uint32_t(0x12345678u));
}

void RustDecodeTest::clientTarget()
{
    uint8_t buf[4];
    putU32(buf, 0, 4242u);
    auto out = seq::rust::decode_client_target(
        rust::Slice<const uint8_t>{buf, sizeof(buf)});
    QVERIFY(out.ok);
    QCOMPARE(out.new_target, uint32_t(4242u));
}

void RustDecodeTest::consider()
{
    // decode_consider is backend-specific: live/test parse the 32B
    // considerStruct; eql's parse_legends_consider parses a 24B Legends payload
    // {u32 self, u32 target, u32 faction, u32 =7, pad, pad} where level is not on
    // the wire (the client reads it from the spawn), so it decodes to 0.
#if defined(SEQ_TARGET_EQL)
    uint8_t buf[24] = {0};
    putU32(buf, 0, 100u);  // player_id (self)
    putU32(buf, 4, 200u);  // target_id
    putI32(buf, 8, 4);     // faction (amiably)
    putU32(buf, 12, 7u);   // constant tail word
    auto out = seq::rust::decode_consider(
        rust::Slice<const uint8_t>{buf, sizeof(buf)});
    QVERIFY(out.ok);
    QCOMPARE(out.player_id, uint32_t(100u));
    QCOMPARE(out.target_id, uint32_t(200u));
    QCOMPARE(out.faction, int32_t(4));
    QCOMPARE(out.level, int32_t(0));
#else
    // Mirrors seq-decode's consider.rs `parses_fields` vector.
    uint8_t buf[32] = {0};
    putU32(buf, 0, 100u);  // player_id
    putU32(buf, 4, 200u);  // target_id
    putI32(buf, 8, -1);    // faction
    putI32(buf, 12, 50);   // level
    auto out = seq::rust::decode_consider(
        rust::Slice<const uint8_t>{buf, sizeof(buf)});
    QVERIFY(out.ok);
    QCOMPARE(out.player_id, uint32_t(100u));
    QCOMPARE(out.target_id, uint32_t(200u));
    QCOMPARE(out.faction, int32_t(-1));
    QCOMPARE(out.level, int32_t(50));
#endif
}

void RustDecodeTest::rejectsBadLength()
{
    // Wrong-size payload → decoder returns ok=false. SZC dispatch guards this in
    // the daemon, but the FFI must still refuse cleanly rather than over-read.
    uint8_t buf[3] = {0};
    auto out = seq::rust::decode_delete_spawn(
        rust::Slice<const uint8_t>{buf, sizeof(buf)});
    QVERIFY(!out.ok);
}

QTEST_GUILESS_MAIN(RustDecodeTest)
#include "rustdecode_test.moc"
