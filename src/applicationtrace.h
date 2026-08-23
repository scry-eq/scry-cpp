#ifndef APPLICATIONTRACE_H
#define APPLICATIONTRACE_H

#include <QString>
#include <QTemporaryFile>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace seq::shadow {

enum class Stream;
enum class Direction;

// Writes strict seq-app-packet-trace version 1 documents. Each part stays in
// a same-directory temporary file until finalize() publishes it atomically
// without replacing an existing trace.
class ApplicationTraceWriter {
public:
    ApplicationTraceWriter(QString outputPrefix, QString backend,
                           QString catalogHash, bool synthetic);
    ~ApplicationTraceWriter();

    ApplicationTraceWriter(const ApplicationTraceWriter&) = delete;
    ApplicationTraceWriter& operator=(const ApplicationTraceWriter&) = delete;

    void push(Stream stream, uint16_t opcode, Direction direction,
              const uint8_t* payload, size_t payloadSize,
              int64_t timestamp);
    void finalize();

    bool hasOpenPart() const { return bool(m_file); }
    uint64_t packetCount() const { return m_packetCount; }
    uint32_t finalizedPartCount() const { return m_finalizedPartCount; }
    QString currentPath() const;

private:
    void openPart();
    void write(const QByteArray& bytes);

    QString m_outputPrefix;
    QString m_backend;
    QString m_catalogHash;
    bool m_synthetic;
    uint32_t m_nextPart = 1;
    uint32_t m_openPart = 0;
    uint32_t m_finalizedPartCount = 0;
    uint64_t m_packetCount = 0;
    uint64_t m_partPacketCount = 0;
    std::optional<int64_t> m_lastTimestamp;
    std::unique_ptr<QTemporaryFile> m_file;
};

} // namespace seq::shadow

#endif
