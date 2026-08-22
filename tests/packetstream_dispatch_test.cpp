/*
 *  packetstream_dispatch_test.cpp
 *  Tier-1 unit test for EQPacketStream::on()/dispatchFor payload resolution.
 *
 *  Pinned by this test: a handler is bound ONLY when an opcode payload
 *  matches the requested (dir, typename, sizechecktype) exactly. A mismatch
 *  must register nothing and return false — the historical behavior was to
 *  fall through to the last-iterated payload and silently bind the handler
 *  to the wrong dispatcher (a wiring/TOML typename drift went undetected).
 */

#include <QtTest/QtTest>
#include <QFile>
#include <QTemporaryDir>

#include "packetinfo.h"
#include "packetcommon.h"
#include "packetstream.h"

namespace {

// Writes `toml` into a freshly-created file under `dir`. Returns the path.
QString writeFixture(const QTemporaryDir& dir, const char* basename,
                     const QByteArray& toml)
{
    const QString path = dir.filePath(QString::fromLatin1(basename));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        qFatal("could not open %s for writing", qUtf8Printable(path));
    }
    f.write(toml);
    f.close();
    return path;
}

PacketHandler noopHandler()
{
    return [](const uint8_t*, size_t, uint8_t) {};
}

class TestStream : public EQPacketStream
{
public:
    using EQPacketStream::EQPacketStream;
    using EQPacketStream::dispatchPacket;

    void cacheThenDrain(EQProtocolPacket& packet)
    {
        setCache(packet.arqSeq(), packet);
        m_arqSeqExp = packet.arqSeq();
        processCache();
    }
};

} // namespace

class PacketStreamDispatchTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void exactMatchBinds();
    void typenameMismatchDoesNotBind();
    void sizeCheckMismatchDoesNotBind();
    void directionMismatchDoesNotBind();
    void firstOfMultiplePayloadsBinds();
    void unknownOpcodeDoesNotBind();
    void applicationHookRunsBeforeLegacyHandler();
    void applicationHookRunsWhileMuted();
    void cachedApplicationKeepsOriginalMetadata();

private:
    QTemporaryDir m_tmp;
    EQPacketTypeDB m_typeDB;
    EQPacketOPCodeDB m_opcodeDB{QStringLiteral("zone")};
};

void PacketStreamDispatchTest::initTestCase()
{
    QVERIFY(m_tmp.isValid());

    // OP_Multi carries two server payloads; the typename-mismatch cases below
    // request names/types matching NEITHER, which used to bind to the second.
    QByteArray toml =
        "[[zone]]\n"
        "id      = \"0001\"\n"
        "name    = \"OP_Multi\"\n"
        "\n"
        "  [[zone.payloads]]\n"
        "  dir           = \"server\"\n"
        "  typename      = \"opCodeStruct\"\n"
        "  sizechecktype = \"match\"\n"
        "\n"
        "  [[zone.payloads]]\n"
        "  dir           = \"server\"\n"
        "  typename      = \"uint8_t\"\n"
        "  sizechecktype = \"none\"\n"
        "\n"
        "[[zone]]\n"
        "id      = \"0002\"\n"
        "name    = \"OP_ClientOnly\"\n"
        "\n"
        "  [[zone.payloads]]\n"
        "  dir           = \"client\"\n"
        "  typename      = \"opCodeStruct\"\n"
        "  sizechecktype = \"match\"\n";

    const QString path = writeFixture(m_tmp, "dispatch.toml", toml);
    QVERIFY(m_opcodeDB.load(m_typeDB, path));
}

// The happy path: exact (dir, typename, szt) match registers the handler.
void PacketStreamDispatchTest::exactMatchBinds()
{
    EQPacketStream stream(zone2client, DIR_Server, 32, m_opcodeDB);
    QVERIFY(stream.on(QStringLiteral("OP_Multi"), "opCodeStruct", SZC_Match,
                      noopHandler()));
}

// A typename present in NO payload must not bind. Before the fix this
// returned true, silently attached to the last payload (uint8_t/SZC_None).
void PacketStreamDispatchTest::typenameMismatchDoesNotBind()
{
    EQPacketStream stream(zone2client, DIR_Server, 32, m_opcodeDB);
    QVERIFY(!stream.on(QStringLiteral("OP_Multi"), "noSuchStruct", SZC_Match,
                       noopHandler()));
}

// Right typename, wrong sizechecktype — also no bind.
void PacketStreamDispatchTest::sizeCheckMismatchDoesNotBind()
{
    EQPacketStream stream(zone2client, DIR_Server, 32, m_opcodeDB);
    QVERIFY(!stream.on(QStringLiteral("OP_Multi"), "opCodeStruct", SZC_None,
                       noopHandler()));
}

// A server-direction stream must not bind to a client-only payload.
void PacketStreamDispatchTest::directionMismatchDoesNotBind()
{
    EQPacketStream stream(zone2client, DIR_Server, 32, m_opcodeDB);
    QVERIFY(!stream.on(QStringLiteral("OP_ClientOnly"), "opCodeStruct",
                       SZC_Match, noopHandler()));
}

// Matching the SECOND payload of a multi-payload opcode still works — the
// mismatch guard must not over-trigger when an earlier payload differs.
void PacketStreamDispatchTest::firstOfMultiplePayloadsBinds()
{
    EQPacketStream stream(zone2client, DIR_Server, 32, m_opcodeDB);
    QVERIFY(stream.on(QStringLiteral("OP_Multi"), "uint8_t", SZC_None,
                      noopHandler()));
}

// Unknown opcode name — existing behavior, still no bind.
void PacketStreamDispatchTest::unknownOpcodeDoesNotBind()
{
    EQPacketStream stream(zone2client, DIR_Server, 32, m_opcodeDB);
    QVERIFY(!stream.on(QStringLiteral("OP_Nonexistent"), "opCodeStruct",
                       SZC_Match, noopHandler()));
}

void PacketStreamDispatchTest::applicationHookRunsBeforeLegacyHandler()
{
    TestStream stream(zone2client, DIR_Server, 32, m_opcodeDB);
    int order = 0;
    QVERIFY(stream.on(QStringLiteral("OP_Multi"), "uint8_t", SZC_None,
                      [&order](const uint8_t*, size_t, uint8_t) {
                          QCOMPARE(order, 1);
                          order = 2;
                      }));

    stream.setApplicationPacketHook(
        [&order](EQStreamID streamId, uint8_t direction, uint16_t opcode,
                 const uint8_t*, size_t length, int64_t timestamp,
                 EQPacketFlowKey, uintptr_t) {
            QCOMPARE(order, 0);
            QCOMPARE(streamId, zone2client);
            QCOMPARE(direction, uint8_t(DIR_Server));
            QCOMPARE(opcode, uint16_t(1));
            QCOMPARE(length, size_t(0));
            QCOMPARE(timestamp, int64_t(1234));
            order = 1;
        },
        [] { return int64_t(1234); });

    const EQPacketOPCode* opcode = m_opcodeDB.find(uint16_t(1));
    stream.dispatchPacket(nullptr, 0, 1, opcode);
    QCOMPARE(order, 2);
}

void PacketStreamDispatchTest::applicationHookRunsWhileMuted()
{
    TestStream stream(zone2client, DIR_Server, 32, m_opcodeDB);
    int hookCalls = 0;
    int handlerCalls = 0;
    QVERIFY(stream.on(QStringLiteral("OP_Multi"), "uint8_t", SZC_None,
                      [&handlerCalls](const uint8_t*, size_t, uint8_t) {
                          ++handlerCalls;
                      }));
    stream.setApplicationPacketHook(
        [&hookCalls](EQStreamID, uint8_t, uint16_t, const uint8_t*, size_t,
                     int64_t, EQPacketFlowKey, uintptr_t) { ++hookCalls; },
        [] { return int64_t(0); });
    stream.setMuted(true);

    stream.dispatchPacket(nullptr, 0, 1, m_opcodeDB.find(uint16_t(1)));
    QCOMPARE(hookCalls, 1);
    QCOMPARE(handlerCalls, 0);
}

void PacketStreamDispatchTest::cachedApplicationKeepsOriginalMetadata()
{
    TestStream stream(zone2client, DIR_Server, 32, m_opcodeDB);
    QByteArray bytes(9, '\0');
    bytes[0] = 0x00; bytes[1] = 0x09; // OP_Packet
    bytes[2] = 0x00;                  // flags
    bytes[3] = 0x00; bytes[4] = 0x07; // ARQ sequence 7
    bytes[5] = 0x01; bytes[6] = 0x00; // application opcode 1

    EQProtocolPacket packet(reinterpret_cast<uint8_t*>(bytes.data()),
                            uint32_t(bytes.size()));
    packet.setCaptureTimeMs(12345);
    const EQPacketFlowKey flow{11, 22};
    packet.setFlowKey(flow);
    packet.setAttributionToken(77);
    const uint16_t crc = stream.calculateCRC(packet);
    bytes[7] = char(crc >> 8);
    bytes[8] = char(crc & 0xff);

    int calls = 0;
    stream.setApplicationPacketHook(
        [&calls, flow](EQStreamID, uint8_t, uint16_t opcode,
                       const uint8_t*, size_t, int64_t timestamp,
                       EQPacketFlowKey actualFlow, uintptr_t token) {
            ++calls;
            QCOMPARE(opcode, uint16_t(1));
            QCOMPARE(timestamp, int64_t(12345));
            QVERIFY(actualFlow == flow);
            QCOMPARE(token, uintptr_t(77));
        },
        [] { return int64_t(99999); });

    stream.cacheThenDrain(packet);
    QCOMPARE(calls, 1);
}

QTEST_APPLESS_MAIN(PacketStreamDispatchTest)
#include "packetstream_dispatch_test.moc"
