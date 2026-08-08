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

QTEST_APPLESS_MAIN(PacketStreamDispatchTest)
#include "packetstream_dispatch_test.moc"
