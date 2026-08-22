#include "namepromoter.h"

#include "boxregistry.h"
#include "diagnosticmessages.h"
#include "packetcommon.h"
#include "packetinfo.h"
#include "packetstream.h"

#include <QByteArray>
#include <QCryptographicHash>

namespace {
constexpr size_t   kEnterWorldClientLen = 72;
constexpr size_t   kCharNameSlot = 64;
}

NamePromoter::NamePromoter(Box* box, BoxRegistry* registry,
                           EQPacketStream* world_c2s, QObject* parent)
    : QObject(parent), m_box(box), m_registry(registry)
{
    Q_ASSERT(box);
    Q_ASSERT(registry);
    Q_ASSERT(world_c2s);
    // The 3-arg decodedPacket overload is the "all packets, no
    // unknown-flag" variant — matches connectStream() in packet.cpp
    // and gives us every dispatched payload.
    connect(world_c2s,
            SIGNAL(decodedPacket(const uint8_t*, size_t, uint8_t,
                                 uint16_t, const EQPacketOPCode*)),
            this,
            SLOT(onDecodedPacket(const uint8_t*, size_t, uint8_t,
                                 uint16_t, const EQPacketOPCode*)));
}

void NamePromoter::onDecodedPacket(const uint8_t* data, size_t len,
                                   uint8_t dir, uint16_t /*opcode*/,
                                   const EQPacketOPCode* entry)
{
    // Match by opcode NAME, not a numeric id — OP_EnterWorld's wire opcode
    // drifts every patch (was 0x0839, now 0x9bdc for the C>S form). The
    // C>S variant carries the char name; the 72-byte length filter selects
    // it over the server's shorter echo variants that share the name.
    if (!entry || entry->name() != QLatin1String("OP_EnterWorld")) return;
    if (dir != DIR_Client) return;
    if (len != kEnterWorldClientLen) return;
    if (m_mutationGuard && !m_mutationGuard()) return;

    // Char name slot is the leading 64 bytes (zero-padded). Treat as a
    // C-string but cap at 64 in case the slot is unterminated.
    const char* raw = reinterpret_cast<const char*>(data);
    const size_t nameLen = qstrnlen(raw, kCharNameSlot);
    if (nameLen == 0) return;

    const QString name = QString::fromLatin1(raw, int(nameLen));
    if (m_promotedObserver) m_promotedObserver(name);
    if (!m_box->display_name.isEmpty()) return;  // already promoted

    const QString old_box_id = m_box->box_id;
    m_box->display_name = name;

    // Replace placeholder box_id with a stable hash of the name. Wire-
    // format box_id stays scrub-safe; display_name carries the label
    // and can be zeroed for redaction without breaking continuity.
    const QByteArray digest = QCryptographicHash::hash(
        name.toLatin1(), QCryptographicHash::Sha256);
    m_box->box_id =
        QStringLiteral("b-") + QString::fromLatin1(digest.left(8).toHex());

    // A single client opens a fresh world socket per zone change, producing N
    // boxes for the same character; each promotes to the SAME box_id (the name
    // hash), which is what groups them into one Character. onPromoted upserts
    // that Character and points its live session at this newest box.
    seqInfo("NamePromoter: box %s promoted from OP_EnterWorld",
            qUtf8Printable(m_box->box_id));
    m_registry->onPromoted(m_box, old_box_id);
}
