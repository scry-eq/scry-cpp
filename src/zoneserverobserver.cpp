#include "zoneserverobserver.h"

#include "boxregistry.h"
#include "diagnosticmessages.h"
#include "everquest.h"
#include "packetcommon.h"
#include "packetinfo.h"
#include "packetstream.h"

#include <QDateTime>

#include <utility>

namespace {
constexpr size_t kZsiLen = sizeof(zoneServerInfoStruct); // 130
}

ZoneServerObserver::ZoneServerObserver(Box* box, EQPacketStream* world_s2c,
                                       std::function<qint64()> nowFn,
                                       QObject* parent)
    : QObject(parent), m_box(box), m_nowFn(std::move(nowFn))
{
    Q_ASSERT(box);
    Q_ASSERT(world_s2c);
    connect(world_s2c,
            SIGNAL(decodedPacket(const uint8_t*, size_t, uint8_t,
                                 uint16_t, const EQPacketOPCode*)),
            this,
            SLOT(onDecodedPacket(const uint8_t*, size_t, uint8_t,
                                 uint16_t, const EQPacketOPCode*)));
}

void ZoneServerObserver::onDecodedPacket(const uint8_t* data, size_t len,
                                         uint8_t dir, uint16_t /*opcode*/,
                                         const EQPacketOPCode* entry)
{
    // Match by opcode NAME, not a numeric id — the wire opcode drifts every
    // EQ patch (OP_ZoneServerInfo was 0x55e6, then 0xf21f). The name comes
    // from the loaded opcode XML, so this stays correct across patches.
    if (!entry || entry->name() != QLatin1String("OP_ZoneServerInfo")) return;
    if (dir != DIR_Server) return;
    if (len != kZsiLen) return;
    if (m_mutationGuard && !m_mutationGuard()) return;

    const auto* zsi = reinterpret_cast<const zoneServerInfoStruct*>(data);
    const size_t hostLen = qstrnlen(zsi->host, sizeof(zsi->host));
    if (m_observedCallback)
        m_observedCallback(QString::fromLatin1(zsi->host, int(hostLen)),
                           zsi->port);
    m_box->expected_zone_server_port = htons(zsi->port);
    // This box is about to open a NEW zone session, so clear any prior
    // zone binding: it's once again "awaiting" a SessionRequest. This lets
    // dispatchPacket's lookupByExpectedZone skip boxes that are already
    // bound to a live session, so when two same-host boxes zone to the same
    // server PORT the second SessionRequest binds the second box instead of
    // re-matching the first.
    m_box->zone_client_port = 0;
    m_box->zone_server_port_bound = 0;
    m_box->zone_await_ms = m_nowFn ? m_nowFn()
                                   : QDateTime::currentMSecsSinceEpoch();
    // Hostname-to-IP resolution is async + costly; defer. dispatchPacket
    // matches on (client_ip, server_port) — for distinct ports per zone
    // it's enough. Two boxes zoning to the same server port simultaneously
    // would collide; deferred to a later stage if it crops up in practice.
    seqInfo("ZoneServerObserver: box %s expects zone server port %u",
            qUtf8Printable(m_box->box_id),
            ntohs(m_box->expected_zone_server_port));
}
