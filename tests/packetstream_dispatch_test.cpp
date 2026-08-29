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
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "applicationtrace.h"
#include "packetinfo.h"
#include "packetcommon.h"
#include "packetstream.h"
#include "rustsession.h"

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
    using EQPacketStream::dispatchPacketAt;

    void cacheThenDrain(EQProtocolPacket& packet)
    {
        setCache(packet.arqSeq(), packet);
        m_arqSeqExp = packet.arqSeq();
        processCache();
    }

    void cache(EQProtocolPacket& packet) { setCache(packet.arqSeq(), packet); }
    void expect(uint16_t sequence) { m_arqSeqExp = sequence; }
    void process(EQProtocolPacket& packet) { processPacket(packet, false); }
    void drain() { processCache(); }
};

seq::rust::SessionBackend backend()
{
#if defined(SEQ_TARGET_LIVE)
    return seq::rust::SessionBackend::Live;
#elif defined(SEQ_TARGET_TEST)
    return seq::rust::SessionBackend::Test;
#elif defined(SEQ_TARGET_EQL)
    return seq::rust::SessionBackend::Eql;
#endif
}

QString backendName()
{
#if defined(SEQ_TARGET_LIVE)
    return QStringLiteral("live");
#elif defined(SEQ_TARGET_TEST)
    return QStringLiteral("test");
#elif defined(SEQ_TARGET_EQL)
    return QStringLiteral("eql");
#endif
}

QString tracePath(const QString& prefix)
{
    return prefix + QStringLiteral("-part-0001.trace.json");
}

QByteArray sequencedApplication(uint16_t sequence, uint16_t opcode,
                                TestStream& stream)
{
    QByteArray bytes(9, '\0');
    bytes[0] = 0x00; bytes[1] = 0x09; // OP_Packet
    bytes[2] = 0x00;                  // flags
    bytes[3] = char(sequence >> 8);
    bytes[4] = char(sequence & 0xff);
    bytes[5] = char(opcode & 0xff);
    bytes[6] = char(opcode >> 8);
    EQProtocolPacket packet(reinterpret_cast<uint8_t*>(bytes.data()),
                            uint32_t(bytes.size()));
    const uint16_t crc = stream.calculateCRC(packet);
    bytes[7] = char(crc >> 8);
    bytes[8] = char(crc & 0xff);
    return bytes;
}

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
    void completionHookRunsAfterLegacyOrMutedDispatch();
    void rejectedApplicationStopsObserversAndLegacy();
    void cachedApplicationKeepsOriginalMetadata();
    void arqTimestampRegressionDisablesOnlyTrace();
    void crossStreamTimestampRegressionDisablesOnlyTrace();

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
                 EQPacketFlowKey, bool, uintptr_t) -> bool {
            Q_ASSERT(order == 0);
            Q_ASSERT(streamId == zone2client);
            Q_ASSERT(direction == uint8_t(DIR_Server));
            Q_ASSERT(opcode == uint16_t(1));
            Q_ASSERT(length == size_t(0));
            Q_ASSERT(timestamp == int64_t(1234));
            order = 1;
            return true;
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
                     int64_t, EQPacketFlowKey, bool, uintptr_t) -> bool {
            ++hookCalls;
            return true;
        },
        [] { return int64_t(0); });
    stream.setMuted(true);

    stream.dispatchPacket(nullptr, 0, 1, m_opcodeDB.find(uint16_t(1)));
    QCOMPARE(hookCalls, 1);
    QCOMPARE(handlerCalls, 0);
}

void PacketStreamDispatchTest::completionHookRunsAfterLegacyOrMutedDispatch()
{
    TestStream stream(zone2client, DIR_Server, 32, m_opcodeDB);
    QStringList order;
    QVERIFY(stream.on(QStringLiteral("OP_Multi"), "uint8_t", SZC_None,
                      [&order](const uint8_t*, size_t, uint8_t) {
                          order.push_back(QStringLiteral("legacy"));
                      }));
    stream.setApplicationPacketHook(
        [&order](EQStreamID, uint8_t, uint16_t, const uint8_t*, size_t,
                 int64_t, EQPacketFlowKey, bool, uintptr_t) -> bool {
            order.push_back(QStringLiteral("rust"));
            return true;
        },
        [] { return int64_t(0); },
        [&order](bool dispatched) {
            order.push_back(dispatched ? QStringLiteral("complete")
                                       : QStringLiteral("muted-complete"));
        });
    stream.dispatchPacket(nullptr, 0, 1, m_opcodeDB.find(uint16_t(1)));
    QCOMPARE(order, QStringList({QStringLiteral("rust"),
                                 QStringLiteral("legacy"),
                                 QStringLiteral("complete")}));

    order.clear();
    stream.setMuted(true);
    stream.dispatchPacket(nullptr, 0, 1, m_opcodeDB.find(uint16_t(1)));
    QCOMPARE(order, QStringList({QStringLiteral("rust"),
                                 QStringLiteral("muted-complete")}));
}

void PacketStreamDispatchTest::rejectedApplicationStopsObserversAndLegacy()
{
    TestStream stream(zone2client, DIR_Server, 32, m_opcodeDB);
    int handlerCalls = 0;
    int observerCalls = 0;
    bool completedAsDispatched = true;
    QVERIFY(stream.on(QStringLiteral("OP_Multi"), "uint8_t", SZC_None,
                      [&handlerCalls](const uint8_t*, size_t, uint8_t) {
                          ++handlerCalls;
                      }));
    connect(&stream,
            qOverload<const uint8_t*, size_t, uint8_t, uint16_t,
                      const EQPacketOPCode*>(&EQPacketStream::decodedPacket),
            this, [&observerCalls](const uint8_t*, size_t, uint8_t, uint16_t,
                                   const EQPacketOPCode*) {
                ++observerCalls;
            });
    stream.setApplicationPacketHook(
        [](EQStreamID, uint8_t, uint16_t, const uint8_t*, size_t, int64_t,
           EQPacketFlowKey, bool, uintptr_t) { return false; },
        [] { return int64_t(0); },
        [&completedAsDispatched](bool dispatched) {
            completedAsDispatched = dispatched;
        });

    stream.dispatchPacket(nullptr, 0, 1, m_opcodeDB.find(uint16_t(1)));
    QCOMPARE(observerCalls, 0);
    QCOMPARE(handlerCalls, 0);
    QVERIFY(!completedAsDispatched);
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
    packet.setSourceIsLow(true);
    packet.setAttributionToken(77);
    const uint16_t crc = stream.calculateCRC(packet);
    bytes[7] = char(crc >> 8);
    bytes[8] = char(crc & 0xff);

    int calls = 0;
    stream.setApplicationPacketHook(
        [&calls, flow](EQStreamID, uint8_t, uint16_t opcode,
                       const uint8_t*, size_t, int64_t timestamp,
                       EQPacketFlowKey actualFlow, bool sourceIsLow,
                       uintptr_t token) -> bool {
            ++calls;
            Q_ASSERT(opcode == uint16_t(1));
            Q_ASSERT(timestamp == int64_t(12345));
            Q_ASSERT(actualFlow == flow);
            Q_ASSERT(sourceIsLow);
            Q_ASSERT(token == uintptr_t(77));
            return true;
        },
        [] { return int64_t(99999); });

    stream.cacheThenDrain(packet);
    QCOMPARE(calls, 1);
}

void PacketStreamDispatchTest::arqTimestampRegressionDisablesOnlyTrace()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString prefix = directory.filePath(QStringLiteral("arq"));
    TestStream stream(zone2client, DIR_Server, 32, m_opcodeDB);
    seq::shadow::ProtocolRegistry registry;
    seq::shadow::Session session(registry, backend());
    session.setTraceWriter(std::make_unique<seq::shadow::ApplicationTraceWriter>(
        prefix, backendName(),
        QString::fromStdString(registry.contentHash(backend())), true));
    int calls = 0;
    stream.setApplicationPacketHook(
        [&session, &calls](EQStreamID, uint8_t, uint16_t opcode,
                           const uint8_t* payload, size_t size,
                           int64_t timestamp, EQPacketFlowKey,
                           bool, uintptr_t) -> bool {
            ++calls;
            session.decode(seq::shadow::Stream::Zone, opcode,
                           seq::shadow::Direction::ServerToClient,
                           payload, size, timestamp);
            return true;
        }, {});

    QByteArray laterBytes = sequencedApplication(8, 0xffff, stream);
    EQProtocolPacket later(
        reinterpret_cast<uint8_t*>(laterBytes.data()),
        uint32_t(laterBytes.size()));
    later.setCaptureTimeMs(100);
    QByteArray expectedBytes = sequencedApplication(7, 0xffff, stream);
    EQProtocolPacket expected(
        reinterpret_cast<uint8_t*>(expectedBytes.data()),
        uint32_t(expectedBytes.size()));
    expected.setCaptureTimeMs(200);

    stream.cache(later);
    stream.expect(7);
    stream.process(expected);
    stream.drain();

    QCOMPARE(calls, 2);
    QCOMPARE(session.recordCount(), uint64_t(2));
    QVERIFY(!session.traceEnabled());
    QVERIFY(!QFile::exists(tracePath(prefix)));
    QCOMPARE(QDir(directory.path()).entryList(
                 QStringList{QStringLiteral(".*.tmp.*")},
                 QDir::Files | QDir::Hidden),
             QStringList());
}

void PacketStreamDispatchTest::crossStreamTimestampRegressionDisablesOnlyTrace()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString prefix = directory.filePath(QStringLiteral("streams"));
    TestStream world(world2client, DIR_Server, 32, m_opcodeDB);
    TestStream zone(zone2client, DIR_Server, 32, m_opcodeDB);
    seq::shadow::ProtocolRegistry registry;
    seq::shadow::Session session(registry, backend());
    session.setTraceWriter(std::make_unique<seq::shadow::ApplicationTraceWriter>(
        prefix, backendName(),
        QString::fromStdString(registry.contentHash(backend())), true));
    int calls = 0;
    auto hook = [&session, &calls](EQStreamID streamId, uint8_t,
                                   uint16_t opcode, const uint8_t* payload,
                                   size_t size, int64_t timestamp,
                                   EQPacketFlowKey, bool, uintptr_t) -> bool {
        ++calls;
        session.decode(streamId == world2client
                           ? seq::shadow::Stream::World
                           : seq::shadow::Stream::Zone,
                       opcode, seq::shadow::Direction::ServerToClient,
                       payload, size, timestamp);
        return true;
    };
    world.setApplicationPacketHook(hook, {});
    zone.setApplicationPacketHook(hook, {});

    world.dispatchPacketAt(nullptr, 0, 0xffff, nullptr, 200, {}, false, 0);
    zone.dispatchPacketAt(nullptr, 0, 0xffff, nullptr, 100, {}, false, 0);

    QCOMPARE(calls, 2);
    QCOMPARE(session.recordCount(), uint64_t(2));
    QVERIFY(!session.traceEnabled());
    QVERIFY(!QFile::exists(tracePath(prefix)));
    QCOMPARE(QDir(directory.path()).entryList(
                 QStringList{QStringLiteral(".*.tmp.*")},
                 QDir::Files | QDir::Hidden),
             QStringList());
}

QTEST_APPLESS_MAIN(PacketStreamDispatchTest)
#include "packetstream_dispatch_test.moc"
