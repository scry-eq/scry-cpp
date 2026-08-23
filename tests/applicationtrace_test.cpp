#include <QtTest/QtTest>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include "applicationtrace.h"
#include "rustsession.h"

namespace {

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

QString tracePath(const QString& prefix, int part = 1)
{
    return QStringLiteral("%1-part-%2.trace.json")
        .arg(prefix).arg(part, 4, 10, QLatin1Char('0'));
}

QJsonObject readObject(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        qFatal("could not read %s", qUtf8Printable(path));
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        qFatal("invalid JSON in %s: %s", qUtf8Printable(path),
               qUtf8Printable(error.errorString()));
    return document.object();
}

QByteArray runSeqTrace(const QStringList& arguments)
{
    QProcess process;
    QStringList cargo{
        QStringLiteral("run"), QStringLiteral("--quiet"),
        QStringLiteral("--target-dir"),
        QStringLiteral(SEQ_TRACE_TARGET_DIR),
        QStringLiteral("--manifest-path"),
        QDir(QStringLiteral(SEQ_DECODER_RS_DIR_PATH)).filePath(
            QStringLiteral("Cargo.toml")),
        QStringLiteral("-p"), QStringLiteral("seq-trace"),
        QStringLiteral("--no-default-features"), QStringLiteral("--features"),
        QStringLiteral("backend-") + backendName(), QStringLiteral("--")};
    cargo.append(arguments);
    process.start(QStringLiteral("cargo"), cargo);
    if (!process.waitForFinished(300000)) {
        process.kill();
        qFatal("seq-trace timed out");
    }
    const QByteArray output = process.readAllStandardOutput() +
                              process.readAllStandardError();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        qFatal("seq-trace failed: %s", output.constData());
    return output;
}

} // namespace

class ApplicationTraceTest : public QObject
{
    Q_OBJECT

private slots:
    void writesStrictSyntheticTraceAtomically();
    void rotatesPartsAndRejectsDecreasingTimestamps();
    void rustCliChecksSessionTrace();
};

void ApplicationTraceTest::writesStrictSyntheticTraceAtomically()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString prefix = directory.filePath(QStringLiteral("synthetic"));
    seq::shadow::ProtocolRegistry registry;
    seq::shadow::ApplicationTraceWriter writer(
        prefix, backendName(),
        QString::fromStdString(registry.contentHash(backend())), true);
    const uint8_t payload[]{0x00, 0x7f, 0xa5, 0xff};

    writer.push(seq::shadow::Stream::World, 23129,
                seq::shadow::Direction::ClientToServer,
                payload, sizeof(payload), 1700000000000);
    QVERIFY(!QFile::exists(tracePath(prefix)));
    writer.finalize();
    QVERIFY(QFile::exists(tracePath(prefix)));

    const QJsonObject trace = readObject(tracePath(prefix));
    QCOMPARE(trace.keys(), QStringList({QStringLiteral("backend"),
                                        QStringLiteral("catalog_hash"),
                                        QStringLiteral("format"),
                                        QStringLiteral("packets"),
                                        QStringLiteral("synthetic"),
                                        QStringLiteral("version")}));
    QCOMPARE(trace.value(QStringLiteral("format")).toString(),
             QStringLiteral("seq-app-packet-trace"));
    QCOMPARE(trace.value(QStringLiteral("version")).toInt(), 1);
    QCOMPARE(trace.value(QStringLiteral("backend")).toString(), backendName());
    QCOMPARE(trace.value(QStringLiteral("synthetic")).toBool(), true);
    const QJsonArray packets = trace.value(QStringLiteral("packets")).toArray();
    QCOMPARE(packets.size(), 1);
    const QJsonObject packet = packets.first().toObject();
    QCOMPARE(packet.value(QStringLiteral("stream")).toString(),
             QStringLiteral("world"));
    QCOMPARE(packet.value(QStringLiteral("opcode_id")).toInt(), 23129);
    QCOMPARE(packet.value(QStringLiteral("direction")).toString(),
             QStringLiteral("client_to_server"));
    QCOMPARE(packet.value(QStringLiteral("payload")).toString(),
             QStringLiteral("007fa5ff"));
    QCOMPARE(packet.value(QStringLiteral("timestamp")).toVariant().toLongLong(),
             qint64(1700000000000));
}

void ApplicationTraceTest::rotatesPartsAndRejectsDecreasingTimestamps()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString prefix = directory.filePath(QStringLiteral("parts"));
    seq::shadow::ProtocolRegistry registry;
    seq::shadow::ApplicationTraceWriter writer(
        prefix, backendName(),
        QString::fromStdString(registry.contentHash(backend())), true);

    writer.push(seq::shadow::Stream::Zone, 65535,
                seq::shadow::Direction::ServerToClient, nullptr, 0, 20);
    QVERIFY_EXCEPTION_THROWN(
        writer.push(seq::shadow::Stream::Zone, 65535,
                    seq::shadow::Direction::ServerToClient, nullptr, 0, 19),
        std::invalid_argument);
    writer.finalize();
    writer.push(seq::shadow::Stream::Zone, 65535,
                seq::shadow::Direction::ServerToClient, nullptr, 0, 10);
    writer.finalize();

    QVERIFY(QFile::exists(tracePath(prefix, 1)));
    QVERIFY(QFile::exists(tracePath(prefix, 2)));
    QCOMPARE(writer.packetCount(), uint64_t(2));
    QCOMPARE(writer.finalizedPartCount(), uint32_t(2));
}

void ApplicationTraceTest::rustCliChecksSessionTrace()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString prefix = directory.filePath(QStringLiteral("session"));
    const QString trace = tracePath(prefix);
    const QString golden = directory.filePath(QStringLiteral("session.golden.json"));
    seq::shadow::ProtocolRegistry registry;
    seq::shadow::Session session(registry, backend());
    session.setTraceWriter(std::make_unique<seq::shadow::ApplicationTraceWriter>(
        prefix, backendName(),
        QString::fromStdString(registry.contentHash(backend())), true));
    const uint8_t payload[]{0xde, 0xad, 0xbe, 0xef};

    session.decode(seq::shadow::Stream::World, 65535,
                   seq::shadow::Direction::ServerToClient,
                   payload, sizeof(payload), 1700000000000);
    session.flush(seq::shadow::FlushReason::ReplayEnd);

    const QJsonObject document = readObject(trace);
    QCOMPARE(document.value(QStringLiteral("synthetic")).toBool(), true);
    QCOMPARE(document.value(QStringLiteral("packets")).toArray().size(), 1);
    runSeqTrace({QStringLiteral("replay"), trace,
                 QStringLiteral("-o"), golden});
    const QByteArray result = runSeqTrace(
        {QStringLiteral("check"), trace, golden});
    QVERIFY(result.contains("ok: 1 exact decode batches match"));
}

QTEST_APPLESS_MAIN(ApplicationTraceTest)
#include "applicationtrace_test.moc"
