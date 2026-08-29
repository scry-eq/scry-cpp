#include "applicationtrace.h"

#include "diagnosticmessages.h"
#include "rustsession.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <unistd.h>
#include <utility>

namespace seq::shadow {
namespace {

const char* streamName(Stream stream)
{
    switch (stream) {
    case Stream::World: return "world";
    case Stream::Zone: return "zone";
    }
    throw std::logic_error("unknown application trace stream");
}

const char* directionName(Direction direction)
{
    switch (direction) {
    case Direction::ServerToClient: return "server_to_client";
    case Direction::ClientToServer: return "client_to_server";
    }
    throw std::logic_error("unknown application trace direction");
}

QByteArray encodeHex(const uint8_t* payload, size_t payloadSize)
{
    static constexpr char digits[] = "0123456789abcdef";
    QByteArray result;
    result.resize(qsizetype(payloadSize * 2));
    for (size_t i = 0; i < payloadSize; ++i) {
        result[qsizetype(i * 2)] = digits[payload[i] >> 4];
        result[qsizetype(i * 2 + 1)] = digits[payload[i] & 0x0f];
    }
    return result;
}

} // namespace

ApplicationTraceWriter::ApplicationTraceWriter(
    QString outputPrefix, QString backend, QString catalogHash,
    bool synthetic)
    : m_outputPrefix(std::move(outputPrefix))
    , m_backend(std::move(backend))
    , m_catalogHash(std::move(catalogHash))
    , m_synthetic(synthetic)
{
    if (m_outputPrefix.isEmpty())
        throw std::invalid_argument("application trace output prefix is empty");
    if (m_backend != QLatin1String("live") &&
        m_backend != QLatin1String("test") &&
        m_backend != QLatin1String("eql"))
        throw std::invalid_argument("application trace backend is invalid");
    if (m_catalogHash.size() != 64)
        throw std::invalid_argument("application trace catalog hash is invalid");
    for (const QChar value : m_catalogHash) {
        const ushort code = value.unicode();
        if (!((code >= '0' && code <= '9') ||
              (code >= 'a' && code <= 'f')))
            throw std::invalid_argument(
                "application trace catalog hash is invalid");
    }
}

ApplicationTraceWriter::~ApplicationTraceWriter() = default;

QString ApplicationTraceWriter::currentPath() const
{
    const uint32_t part = m_openPart ? m_openPart : m_nextPart;
    return QStringLiteral("%1-part-%2.trace.json")
        .arg(m_outputPrefix)
        .arg(part, 4, 10, QLatin1Char('0'));
}

void ApplicationTraceWriter::openPart()
{
    if (m_file) return;
    m_openPart = m_nextPart++;
    const QString path = currentPath();
    const QFileInfo target(path);
    const QString temporaryTemplate = target.dir().filePath(
        QStringLiteral(".%1.tmp.XXXXXX").arg(target.fileName()));
    m_file = std::make_unique<QTemporaryFile>(temporaryTemplate);
    m_file->setAutoRemove(true);
    if (!m_file->open()) {
        const QString error = m_file->errorString();
        m_file.reset();
        m_openPart = 0;
        throw std::runtime_error(
            QStringLiteral("could not open application trace %1: %2")
                .arg(path, error).toStdString());
    }
    write(QByteArray("{\n  \"format\": \"seq-app-packet-trace\",\n"
                     "  \"version\": 1,\n  \"backend\": \"") +
          m_backend.toUtf8() + QByteArray("\",\n  \"catalog_hash\": \"") +
          m_catalogHash.toUtf8() + QByteArray("\",\n  \"synthetic\": ") +
          (m_synthetic ? QByteArray("true") : QByteArray("false")) +
          QByteArray(",\n  \"packets\": [\n"));
    m_partPacketCount = 0;
    m_lastTimestamp.reset();
}

void ApplicationTraceWriter::write(const QByteArray& bytes)
{
    if (!m_file || m_file->write(bytes) != bytes.size()) {
        const QString error = m_file ? m_file->errorString()
                                     : QStringLiteral("file is not open");
        throw std::runtime_error(
            QStringLiteral("could not write application trace: %1")
                .arg(error).toStdString());
    }
    m_bytesWritten += uint64_t(bytes.size());
}

void ApplicationTraceWriter::push(
    Stream stream, uint16_t opcode, Direction direction,
    const uint8_t* payload, size_t payloadSize, int64_t timestamp)
{
    if (payloadSize && !payload)
        throw std::invalid_argument("application trace payload is null");
    if (m_capped) return;
    if (m_bytesWritten >= kByteCap) {
        // A trace is a diagnostic; cap it before it fills the disk.
        m_capped = true;
        finalize();
        seqWarn("Application trace %s reached %llu MiB; recording stopped",
                qUtf8Printable(m_outputPrefix),
                (unsigned long long)(kByteCap >> 20));
        return;
    }
    if (m_lastTimestamp && timestamp < *m_lastTimestamp)
        throw std::invalid_argument(
            "application trace timestamps must not decrease");
    openPart();

    QByteArray packet;
    if (m_partPacketCount) packet += ",\n";
    packet += QByteArray("    {\n      \"stream\": \"") + streamName(stream) +
              QByteArray("\",\n      \"opcode_id\": ") +
              QByteArray::number(opcode) +
              QByteArray(",\n      \"direction\": \"") +
              directionName(direction) +
              QByteArray("\",\n      \"payload\": \"") +
              encodeHex(payload, payloadSize) +
              QByteArray("\",\n      \"timestamp\": ") +
              QByteArray::number(timestamp) + QByteArray("\n    }");
    write(packet);
    m_lastTimestamp = timestamp;
    ++m_partPacketCount;
    ++m_packetCount;
}

void ApplicationTraceWriter::finalize()
{
    if (!m_file) return;
    const QString path = currentPath();
    try {
        write(QByteArray("\n  ]\n}\n"));
        if (!m_file->flush()) {
            const QString error = m_file->errorString();
            throw std::runtime_error(
                QStringLiteral("could not flush application trace %1: %2")
                    .arg(path, error).toStdString());
        }
        if (::fsync(m_file->handle()) != 0) {
            const int error = errno;
            throw std::runtime_error(
                QStringLiteral("could not sync application trace %1: %2")
                    .arg(path, QString::fromLocal8Bit(std::strerror(error)))
                    .toStdString());
        }
        const QByteArray temporaryName = QFile::encodeName(m_file->fileName());
        const QByteArray finalName = QFile::encodeName(path);
        if (::link(temporaryName.constData(), finalName.constData()) != 0) {
            const int error = errno;
            const QString detail = error == EEXIST
                ? QStringLiteral("destination already exists")
                : QString::fromLocal8Bit(std::strerror(error));
            throw std::runtime_error(
                QStringLiteral("could not publish application trace %1: %2")
                    .arg(path, detail).toStdString());
        }
    } catch (...) {
        m_file.reset();
        m_openPart = 0;
        throw;
    }
    m_file.reset();
    m_openPart = 0;
    ++m_finalizedPartCount;
    m_partPacketCount = 0;
    m_lastTimestamp.reset();
}

} // namespace seq::shadow
