#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <cstddef>
#include <cstdint>
#include <vector>

class EQPacket;
class EQPacketOPCode;

// OpcodeStatsLogger taps EQPacket's decoded-packet signals and keeps a
// per-opcode tally of (count, direction, payload sizes). At destruction
// it writes a sorted report to the configured path. Wired by --opcode-stats
// FILE on the daemon CLI; primarily a patch-day diagnostic for finding
// the still-unresolved (id="ffff") opcodes by matching their payload
// sizes against known-struct sizes from everquest.h.
//
// Output format is plain text — designed to be eyeballed and grepped, not
// parsed. The trailing "Candidate matches" section pairs each unknown
// opcode with any known-struct it could plausibly be by size.
class OpcodeStatsLogger : public QObject {
    Q_OBJECT
public:
    // `allSessions` records whether --dump-all-sessions was in effect, so the
    // report can tell "nothing decoded" apart from "scoped away from the box
    // that decoded it" (see writeReport).
    OpcodeStatsLogger(EQPacket* packet, const QString& outPath,
                      bool allSessions = false, QObject* parent = nullptr);
    ~OpcodeStatsLogger() override;

    // Writes the accumulated report. Called automatically from the
    // destructor; exposed so DaemonApp can also flush on aboutToQuit
    // before Qt children get torn down.
    void writeReport();

private slots:
    void onDecodedZonePacket(const uint8_t* data, size_t len, uint8_t dir,
                             uint16_t opcode, const EQPacketOPCode* entry);
    void onDecodedWorldPacket(const uint8_t* data, size_t len, uint8_t dir,
                              uint16_t opcode, const EQPacketOPCode* entry);

private:
    EQPacket* m_packet = nullptr;
    bool      m_allSessions = false;

    struct Sample {
        QByteArray bytes;       // up to kSampleBytes captured prefix
        int        size  = 0;   // full payload size as observed
        uint8_t    dir   = 0;
    };

    struct OpcodeStat {
        QString          name;        // empty = unknown
        QHash<uint8_t,int> dirCounts; // DIR_Server / DIR_Client
        QHash<int,int>   sizeCounts;  // payload size → count
        int              total = 0;
        // Captured payload samples for unknown opcodes only — up to
        // kMaxSamples distinct prefixes. Modern EQ tends to fire the
        // same opcode multiple times with different inner payloads
        // (e.g. one OP_GroupUpdate per member), so a single first-seen
        // sample loses the per-member variation. Dedupe by exact
        // prefix bytes so we don't waste slots on identical retransmits.
        std::vector<Sample> samples;
    };

    void record(QHash<uint16_t, OpcodeStat>& bucket, uint16_t opcode,
                const uint8_t* data, size_t len, uint8_t dir,
                const EQPacketOPCode* entry);

    QString                       m_outPath;
    QHash<uint16_t, OpcodeStat>   m_zone;
    QHash<uint16_t, OpcodeStat>   m_world;
    bool                          m_written = false;
};
