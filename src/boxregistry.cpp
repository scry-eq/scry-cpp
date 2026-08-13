#include "boxregistry.h"

#include "diagnosticmessages.h"

#include <algorithm>

#include <QCryptographicHash>
#include <QHash>
#include <QHostAddress>
#include <QSet>

namespace {
// Stable label for --list-boxes / summary().
const char* stateName(SessionState s)
{
    switch (s) {
        case SessionState::Pending:    return "pending";
        case SessionState::Attached:   return "attached";
        case SessionState::Superseded: return "superseded";
        case SessionState::Reaped:     return "reaped";
    }
    return "?";
}
}  // namespace

BoxRegistry::BoxRegistry(QObject* parent) : QObject(parent) {}

QString Box::summary() const
{
    const QString ip = QHostAddress(ntohl(client_ip)).toString();
    const char* primary = is_primary ? "*" : " ";
    // box_id already encodes identity (a hash of the character name once
    // promoted); never print the raw display_name — keep EQ character names
    // out of logs. promoted= conveys the same recon signal name-safely.
    return QStringLiteral("%1box %2  ip=%3  ports=%4->%5  pkts=%6  promoted=%7  state=%8")
        .arg(primary)
        .arg(box_id)
        .arg(ip)
        .arg(ntohs(client_world_port))
        .arg(ntohs(server_world_port))
        .arg(packet_count)
        .arg(display_name.isEmpty() ? QStringLiteral("no") : QStringLiteral("yes"))
        .arg(QString::fromLatin1(stateName(state)));
}

Box* BoxRegistry::observe(in_addr_t client_ip,
                          in_addr_t server_ip,
                          in_port_t client_world_port,
                          in_port_t server_world_port,
                          qint64    now_ms)
{
    if (Box* hit = lookupByWorld(client_ip,
                                 client_world_port,
                                 server_world_port)) {
        hit->last_seen_ms = now_ms;
        hit->server_world_ip = server_ip;
        ++hit->packet_count;
        return hit;
    }

    auto b = std::make_unique<Box>();
    b->client_ip = client_ip;
    b->client_world_port = client_world_port;
    b->server_world_port = server_world_port;
    b->server_world_ip = server_ip;
    b->first_seen_ms = now_ms;
    b->last_seen_ms = now_ms;
    b->packet_count = 1;
    b->is_primary = m_boxes.empty();
    // Placeholder box_id; NamePromoter overwrites once OP_EnterWorld
    // arrives (Stage 2).
    b->box_id = QStringLiteral("p-%1-%2-%3")
        .arg(client_ip, 8, 16, QChar('0'))
        .arg(ntohs(client_world_port), 4, 16, QChar('0'))
        .arg(ntohs(server_world_port), 4, 16, QChar('0'));

    Box* raw = b.get();
    m_boxes.push_back(std::move(b));

    seqInfo("BoxRegistry: new box %s%s  ip=%s  client_port=%u  server_port=%u",
            raw->is_primary ? "*" : "",
            qUtf8Printable(raw->box_id),
            qUtf8Printable(QHostAddress(ntohl(raw->client_ip)).toString()),
            ntohs(raw->client_world_port),
            ntohs(raw->server_world_port));

    if (m_hook) m_hook(*raw);
    emit boxCreated(raw);

    // First non-merged Box becomes active by default. Subsequent
    // creations only fire changed() — active stays put until a client
    // explicitly switches via SetActiveBox.
    if (m_activeCharacterId.isEmpty()) m_activeCharacterId = raw->box_id;

    emit changed();
    return raw;
}

Box* BoxRegistry::findById(const QString& box_id)
{
    // A character's sessions all share box_id (the name hash), so the first
    // match is its identity anchor (the earliest-observed session).
    for (auto& b : m_boxes)
        if (b->box_id == box_id) return b.get();
    return nullptr;
}

bool BoxRegistry::setActiveCharacterId(const QString& box_id)
{
    Box* target = findById(box_id);
    if (!target) return false;
    if (m_activeCharacterId == box_id) return true;
    Box* old = findById(m_activeCharacterId);
    m_activeCharacterId = box_id;
    emit activeCharacterChanged(old, target);
    emit changed();
    return true;
}

void BoxRegistry::onPromoted(Box* box, const QString& old_box_id)
{
    if (!box) return;
    if (m_activeCharacterId == old_box_id)
        m_activeCharacterId = box->box_id;
    // A re-handshake promotes to a box_id that collides with an existing
    // session of the same character; m_activeCharacterId may already point at
    // it — leave it, the changed() re-emit gives the client a fresh picture.

    // Maintain the Character store here — the ONE hook both promotion paths
    // funnel through (NamePromoter's OP_EnterWorld path and BoxRegistry::
    // promoteByName each set box_id/display_name then call onPromoted). A
    // character's sessions share box_id (the name hash), so it keys the store.
    upsertCharacter(box->box_id, box->display_name, box);

    emit changed();
}

void BoxRegistry::notifyChanged()
{
    emit changed();
}

int BoxRegistry::evictStale(qint64 now_ms, qint64 ttl_ms)
{
    if (ttl_ms <= 0) return 0;
    const qint64 cutoff = now_ms - ttl_ms;

    // Per-character session lifecycle. Two boxes are never reaped: the primary
    // (its streams are the global ones) and the session the user is actively
    // viewing (a parked character must survive even when idle). Every other box
    // is reclaimed the moment it goes silent past the TTL — a character's
    // superseded old-zone sessions as soon as they're idle, and the character
    // itself (its live session) once even that is idle (logged off / camped).
    // The Character store owns identity by the box_id name-hash, so freeing a
    // stale session never orphans a character that still has a live one.
    Box* activeLive = currentBoxFor(m_activeCharacterId);

    std::vector<Box*> victims;
    for (auto& up : m_boxes) {
        Box* b = up.get();
        if (b->is_primary)             continue;   // the global-stream box: never
        if (b == activeLive)           continue;   // actively-viewed: never
        if (b->last_seen_ms >= cutoff) continue;   // still talking
        victims.push_back(b);
    }

    if (victims.empty()) return 0;

    // Let subscribers (EQPacket streams/observers, DaemonApp ManagerSets)
    // release their per-box resources while the Box is still valid.
    for (Box* v : victims) {
        seqInfo("BoxRegistry: evicting idle box %s", qUtf8Printable(v->box_id));
        v->state = SessionState::Reaped;   // state machine: terminal
        emit boxAboutToBeRemoved(v);
    }

    // Now free the Boxes. Erase by identity — the per-victim erase is O(N)
    // but victim counts are small (a handful per sweep at most).
    for (Box* v : victims) {
        m_boxes.erase(
            std::remove_if(m_boxes.begin(), m_boxes.end(),
                [v](const std::unique_ptr<Box>& up) { return up.get() == v; }),
            m_boxes.end());
    }

    // Drop Characters whose live session was reaped. evictStale protects a
    // character's active-viewed session, and a still-fresh live session isn't
    // idle, so this only fires when a character logged off / camped. Pointer
    // comparison only — the Box is freed but never dereferenced here.
    m_characters.erase(
        std::remove_if(m_characters.begin(), m_characters.end(),
            [&victims](const Character& c) {
                return std::find(victims.begin(), victims.end(), c.session)
                       != victims.end();
            }),
        m_characters.end());

    emit changed();
    return int(victims.size());
}

Box* BoxRegistry::primary()
{
    return m_boxes.empty() ? nullptr : m_boxes.front().get();
}

Box* BoxRegistry::lookupByWorld(in_addr_t client_ip,
                                in_port_t client_world_port,
                                in_port_t server_world_port)
{
    for (auto& b : m_boxes) {
        if (b->client_ip == client_ip &&
            b->client_world_port == client_world_port &&
            b->server_world_port == server_world_port) {
            return b.get();
        }
    }
    return nullptr;
}

Box* BoxRegistry::lookupByName(const QString& name, const Box* exclude)
{
    if (name.isEmpty()) return nullptr;
    for (auto& b : m_boxes) {
        if (b.get() == exclude) continue;
        if (b->display_name == name) return b.get();
    }
    return nullptr;
}

Box* BoxRegistry::promoteByName(Box* box, const QString& name)
{
    if (!box || name.isEmpty()) return nullptr;
    if (box->display_name == name) return nullptr;  // already promoted

    const QString old_box_id = box->box_id;
    box->display_name = name;
    // Stable, scrub-safe id = hash of the character name. Every session of the
    // same character (a re-handshake) hashes to the SAME box_id, so the shared
    // id groups them — one picker entry, one Character store row; display_name
    // carries the human-readable label.
    const QByteArray digest = QCryptographicHash::hash(
        name.toLatin1(), QCryptographicHash::Sha256);
    box->box_id =
        QStringLiteral("b-") + QString::fromLatin1(digest.left(8).toHex());

    // A prior session of this character already around => this is a zone-change
    // re-handshake (the newest session); else a brand-new character.
    Box* prior = lookupByName(name, box);
    seqInfo(prior ? "BoxRegistry: box promoted, re-handshake of character %s"
                  : "BoxRegistry: box promoted to character %s",
            qUtf8Printable(box->box_id));
    onPromoted(box, old_box_id);   // upserts the Character (keyed by box_id)

    // If the currently-active box is a stale, never-promoted placeholder
    // (the first-seen box was a partial/dead session that never resolved a
    // character — common when a capture/daemon starts mid-session), adopt
    // this newly-decoded DISTINCT character as the active one. Otherwise
    // SessionAdapter would stream an empty box while the real data lives in
    // a non-primary box.
    if (!prior) {
        Box* active = findById(m_activeCharacterId);
        if (!active || active->display_name.isEmpty()) {
            m_activeCharacterId = box->box_id;
        }
    }

    // If this box belongs to the ACTIVE character, its current decode box just
    // rolled to this newest zone session — nudge listeners so map/geometry
    // follow the character into its new zone.
    if (m_activeCharacterId == box->box_id) {
        emit activeCharacterChanged(nullptr, box);
    }

    // State machine: reclassify this character's sessions (all share box_id) —
    // the live one (currentBoxFor) is ATTACHED, every other is SUPERSEDED.
    Box* live = currentBoxFor(box->box_id);
    for (auto& up : m_boxes) {
        Box* b = up.get();
        if (b->box_id != box->box_id) continue;   // a different character
        b->state = (b == live) ? SessionState::Attached : SessionState::Superseded;
    }

    emit changed();
    return prior;
}

Box* BoxRegistry::currentBoxFor(const QString& box_id)
{
    // Character-owned: the live session is the store's session (maintained by
    // upsertCharacter as sessions promote — the newest zone session wins).
    for (const Character& c : m_characters)
        if (c.id == box_id)
            return c.session;
    // Not a promoted character (a Pending placeholder id, or unknown): resolve
    // the box itself.
    return findById(box_id);
}

Box* BoxRegistry::lookupBoundZone(in_addr_t client_ip,
                                  in_port_t client_zone_port,
                                  in_port_t server_zone_port)
{
    for (auto& b : m_boxes) {
        // Routing keys on the unique zone 5-tuple, NEVER on identity. A
        // re-handshake/zone-change box is identified (named + folded into its
        // character) at OP_PlayerProfile, moments after its zone session binds.
        // That box keeps its OWN streams and is still actively decoding, so it
        // must stay routable by its tuple — skipping it would drop the spawn
        // burst and ongoing position updates that follow the profile (or hand
        // the session to another same-host box). Identity grouping (one picker
        // entry per character) is handled separately by the Character store.
        if (b->client_ip == client_ip &&
            b->zone_client_port == client_zone_port &&
            b->zone_server_port_bound == server_zone_port) {
            return b.get();
        }
    }
    return nullptr;
}

bool BoxRegistry::hasClient(in_addr_t client_ip) const
{
    for (const auto& b : m_boxes)
        if (b->client_ip == client_ip) return true;
    return false;
}

bool BoxRegistry::anyZoneServerAnnounced() const
{
    for (const auto& b : m_boxes)
        if (b->expected_zone_server_port != 0) return true;
    return false;
}

bool BoxRegistry::looksLikeZoneServer(in_addr_t server_ip) const
{
    for (const auto& b : m_boxes) {
        if (b->server_world_ip == 0) continue;
        if (((b->server_world_ip ^ server_ip) & htonl(0xFFFFFF00)) == 0)
            return true;
    }
    return false;
}

Box* BoxRegistry::lookupByRecentWorld(in_addr_t client_ip,
                                      in_addr_t server_ip,
                                      qint64 now_ms,
                                      qint64 window_ms)
{
    Box* best = nullptr;
    for (auto& b : m_boxes) {
        if (b->client_ip != client_ip) continue;
        // Same guard as the expected-zone path: never steal a box that is
        // already decoding a live zone session.
        if (b->zone_client_port != 0) continue;
        // Must have actually done a world handshake — that is what makes it a
        // box about to zone rather than an artifact.
        if (b->client_world_port == 0) continue;
        // An EQ client opens several unrelated SOE sessions (patcher,
        // telemetry) that also start with a SessionRequest. The zone server
        // shares the world server's subnet; those services do not. Without
        // this the fallback binds the first one it sees and burns the box's
        // binding, so the real zone session is dropped when it opens.
        if (b->server_world_ip == 0) continue;
        if (((b->server_world_ip ^ server_ip) & htonl(0xFFFFFF00)) != 0) continue;
        if (now_ms - b->last_seen_ms > window_ms) continue;
        if (!best || b->last_seen_ms > best->last_seen_ms) best = b.get();
    }
    return best;
}

Box* BoxRegistry::lookupByExpectedZone(in_addr_t client_ip,
                                       in_addr_t server_ip,
                                       in_port_t server_zone_port)
{
    // Among boxes that match (client_ip, server_port) and are NOT already
    // bound to a live zone session, pick the one notified most recently by
    // OP_ZoneServerInfo. The client connects immediately after being told
    // where to zone, so when two same-host boxes await the same server port
    // the newest notification owns the SessionRequest arriving now. Without
    // this tiebreak the first box in the list (always the primary) would
    // grab every session and the others would never bind.
    Box* best = nullptr;
    for (auto& b : m_boxes) {
        // Identity grouping never gates routing: a box can reuse its world
        // socket on a re-zone (ZoneServerObserver clears its binding to await
        // the next SessionRequest) after it's already been identified. It must
        // still be able to rebind its own session. The `zone_client_port != 0`
        // guard below already excludes boxes bound to a live session, so this
        // only ever re-binds a genuinely-available box. (See lookupBoundZone.)
        if (b->client_ip != client_ip) continue;
        // Skip boxes already bound to a live zone session — ZoneServerObserver
        // clears the binding on a fresh OP_ZoneServerInfo so a re-zoning box
        // becomes available again.
        if (b->zone_client_port != 0) continue;
        // ZoneServerObserver defers hostname→IP resolution (sets only the
        // port), so expected_zone_server_ip is usually 0. Match on
        // (client_ip, server_port) in that case; only enforce the IP when
        // it's actually known.
        if (b->expected_zone_server_ip != 0 &&
            b->expected_zone_server_ip != server_ip) continue;
        if (b->expected_zone_server_port != server_zone_port) continue;
        if (!best || b->zone_await_ms > best->zone_await_ms) best = b.get();
    }
    return best;
}

size_t BoxRegistry::distinctCount() const
{
    // A character's sessions share box_id (the name hash); an anonymous box has
    // a unique placeholder id. So the distinct-id count IS the identity count.
    QSet<QString> ids;
    for (const auto& b : m_boxes) ids.insert(b->box_id);
    return size_t(ids.size());
}

QString BoxRegistry::dumpString() const
{
    if (m_boxes.empty())
        return QStringLiteral("BoxRegistry: empty");

    // Sessions per character (they share box_id) so each anchor line can show
    // "(+N re-handshakes)".
    QHash<QString, int> perId;
    for (const auto& b : m_boxes) ++perId[b->box_id];

    QString out = QStringLiteral(
        "BoxRegistry: %1 distinct, %2 total (*=primary, +N=re-handshakes)\n")
        .arg(distinctCount())
        .arg(m_boxes.size());
    QSet<QString> shown;
    for (const auto& b : m_boxes) {
        if (shown.contains(b->box_id)) continue;   // fold aliases into the anchor
        shown.insert(b->box_id);
        out += QStringLiteral("  ") + b->summary();
        const int extras = perId.value(b->box_id, 1) - 1;
        if (extras > 0)
            out += QStringLiteral("  (+%1 re-handshakes)").arg(extras);
        out += QChar('\n');
    }
    return out;
}

// The name-keyed read API for the picker. One Character per distinct box_id (a
// character's sessions share it; an anonymous box has a unique placeholder id,
// so it surfaces as its own still-nameless entry). The live session is
// currentBoxFor; aliasCount is the character's other (superseded) sessions.
std::vector<Character> BoxRegistry::characters()
{
    std::vector<Character> out;
    QSet<QString> seen;
    for (auto& up : m_boxes) {
        Box* b = up.get();
        if (seen.contains(b->box_id)) continue;   // an already-emitted character
        seen.insert(b->box_id);
        Character c;
        c.name    = b->display_name;
        c.id      = b->box_id;
        c.session = currentBoxFor(b->box_id);     // the ATTACHED live session
        for (auto& a : m_boxes)
            if (a.get() != b && a->box_id == b->box_id) ++c.aliasCount;
        out.push_back(std::move(c));
    }
    return out;
}

void BoxRegistry::upsertCharacter(const QString& id, const QString& name,
                                  Box* newSession)
{
    for (Character& c : m_characters) {
        if (c.id == id) {
            c.name = name;
            // Adopt the newer session; a late-promoting OLDER box must not steal
            // the live session from a newer one (guard on first_seen).
            if (!c.session ||
                newSession->first_seen_ms >= c.session->first_seen_ms)
                c.session = newSession;
            return;
        }
    }
    Character c;
    c.name    = name;
    c.id      = id;
    c.session = newSession;
    m_characters.push_back(std::move(c));
}

Box* BoxRegistry::currentSessionFor(const QString& name)
{
    if (name.isEmpty()) return nullptr;
    // Resolve via the Character store — pin to the character, not the last-zoned
    // box. c.session is maintained (newest zone session wins) by upsertCharacter.
    for (const Character& c : m_characters)
        if (c.name == name)
            return c.session;
    return nullptr;
}
