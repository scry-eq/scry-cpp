#include "wsserver.h"

#include <QByteArray>
#include <QLoggingCategory>
#include <QTimer>
#include <QUuid>
#include <QWebSocket>
#include <QWebSocketServer>

#include "envelopesink.h"
#include "lootstore.h"
#include "sessionadapter.h"

#include "seq/v1/client.pb.h"
#include "seq/v1/events.pb.h"

namespace {

// Production sink: serialize the envelope and push it across the socket as
// a single binary WebSocket frame. One per connected session; lifetime
// tracked alongside the SessionAdapter in WsServer::m_sessions.
class WebSocketSink : public IEnvelopeSink {
public:
    explicit WebSocketSink(QWebSocket* sock) : m_sock(sock) {}

    void send(const seq::v1::Envelope& env) override
    {
        QByteArray buf;
        buf.resize(static_cast<int>(env.ByteSizeLong()));
        env.SerializeToArray(buf.data(), buf.size());
        m_sock->sendBinaryMessage(buf);
    }

private:
    QWebSocket* m_sock;
};

// Detached sink: the SessionAdapter keeps consuming state-manager
// signals while a session is between WebSockets, but its envelopes go
// nowhere until a resume swaps in a fresh WebSocketSink. The ring
// buffer in SessionAdapter still receives them for replay.
class NoopSink : public IEnvelopeSink {
public:
    void send(const seq::v1::Envelope&) override {}
};

constexpr int kPruneAfterMs = 30 * 1000;

} // namespace

WsServer::WsServer(QObject* parent)
    : QObject(parent)
    , m_server(std::make_unique<QWebSocketServer>(
          QStringLiteral("scryd"),
          QWebSocketServer::NonSecureMode))
{
    connect(m_server.get(), &QWebSocketServer::newConnection,
            this, &WsServer::onNewConnection);
}

WsServer::~WsServer()
{
    // m_server is declared before the socket-bookkeeping hashes, so in reverse
    // member-destruction order it outlives them: destroying it deletes its child
    // QWebSockets, each emitting disconnected -> onSocketDisconnected, which
    // indexes m_pending / m_socketToSession (already destroyed) — a use-after-
    // free that SIGSEGVs on shutdown whenever a ws client is still connected.
    // Sever every live socket's signals to this first so teardown is silent.
    const auto pendingSocks = m_pending.keys();
    for (QWebSocket* sock : pendingSocks) sock->disconnect(this);
    const auto boundSocks = m_socketToSession.keys();
    for (QWebSocket* sock : boundSocks) sock->disconnect(this);

    for (auto& s : m_sessions) {
        delete s.liveSink;
        if (s.pruneTimer) s.pruneTimer->deleteLater();
    }
}

void WsServer::setState(SpawnShell* ss, ZoneMgr* zm, Player* p, MapData* md,
                        MessageShell* ms, GroupMgr* gm, GuildShell* gus,
                        SpellShell* sps,
                        CombatRouter* cr, CategoryMgr* cm, FilterMgr* fm,
                        PrefsBroker* pb, SpawnMonitor* sm, ItemCache* ic,
                        DateTimeMgr* dtm, ZoneServerMgr* zsm,
                        BoxRegistry* boxes)
{
    m_spawnShell    = ss;
    m_zoneMgr       = zm;
    m_player        = p;
    m_mapData       = md;
    m_messageShell  = ms;
    m_groupMgr      = gm;
    m_guildShell    = gus;
    m_spellShell    = sps;
    m_combatRouter  = cr;
    m_categoryMgr   = cm;
    m_filterMgr     = fm;
    m_prefsBroker   = pb;
    m_spawnMonitor  = sm;
    m_itemCache     = ic;
    m_dateTimeMgr   = dtm;
    m_zoneServerMgr = zsm;
    m_boxes         = boxes;
}

bool WsServer::listen(const QHostAddress& host, quint16 port)
{
    return m_server->listen(host, port);
}

void WsServer::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QWebSocket* sock = m_server->nextPendingConnection();

        // /loot is a query-only path: LootQuery in, LootPage out, no session
        // and no envelope stream. Reading history should not require taking
        // the spawn firehose, and it keeps loot off the session's seq counter.
        if (sock->requestUrl().path() == QLatin1String("/loot")) {
            m_lootSockets.insert(sock);
            connect(sock, &QWebSocket::binaryMessageReceived,
                    this, [this, sock](const QByteArray& b) {
                        onLootBinary(sock, b);
                    });
            connect(sock, &QWebSocket::disconnected,
                    this, [this, sock] { onSocketDisconnected(sock); });
            qInfo("ws loot client connected");
            continue;
        }

        m_pending.insert(sock, PendingSocket{sock});

        // First binary frame routes through onPendingFirstBinary so we
        // can decide resume vs. fresh from Subscribe.session_id. After
        // that, signals reroute to the bound adapter — see
        // attachNewSession / resumeSession.
        connect(sock, &QWebSocket::binaryMessageReceived,
                this, [this, sock](const QByteArray& b) {
                    onPendingFirstBinary(sock, b);
                });
        connect(sock, &QWebSocket::disconnected,
                this, [this, sock] { onSocketDisconnected(sock); });

        qInfo("ws client connected (pending Subscribe)");
    }
}

// /loot: one LootQuery in, one LootPage back, on this socket only. Stateless —
// the connection can stay open and re-query, or close after one.
void WsServer::onLootBinary(QWebSocket* sock, const QByteArray& bytes)
{
    seq::v1::ClientEnvelope env;
    if (!env.ParseFromArray(bytes.constData(), bytes.size())) {
        qWarning("loot: malformed ClientEnvelope (%lld bytes); dropping connection",
                 static_cast<long long>(bytes.size()));
        sock->close();
        return;
    }
    if (!env.has_loot_query()) {
        qWarning("loot: expected LootQuery; dropping connection");
        sock->close();
        return;
    }

    seq::v1::Envelope reply;
    auto* page = reply.mutable_loot_page();
    // No store (or a closed one, as under --replay) answers an empty page: the
    // client renders "nothing recorded", which beats a dropped connection.
    if (m_lootStore) {
        for (const LootRowRec& r : m_lootStore->recent(env.loot_query().limit())) {
            auto* row = page->add_rows();
            row->set_ts(r.ts);
            row->set_source(r.source.toStdString());
            row->set_item_name(r.itemName.toStdString());
            row->set_item_id(r.itemId);
            row->set_icon(r.icon);
            row->set_qty(r.qty);
            row->set_mob_name(r.mobName.toStdString());
            row->set_mob_norm(r.mobNorm.toStdString());
            row->set_corpse_id(r.corpseId);
            row->set_zone_short(r.zoneShort.toStdString());
            row->set_zone_base(r.zoneBase.toStdString());
            row->set_instance(r.instance.toStdString());
            row->set_sold(r.sold);
            row->set_money_copper(r.moneyCopper);
            row->set_disposition(r.disposition.toStdString());
            row->set_looter(r.looter.toStdString());
        }
    }

    const std::string buf = reply.SerializeAsString();
    sock->sendBinaryMessage(QByteArray(buf.data(), static_cast<int>(buf.size())));
}

void WsServer::onPendingFirstBinary(QWebSocket* sock, const QByteArray& bytes)
{
    auto it = m_pending.find(sock);
    if (it == m_pending.end()) {
        // Already promoted to a session — defensively ignore (shouldn't
        // happen because we disconnect this lambda after promotion).
        return;
    }

    seq::v1::ClientEnvelope env;
    if (!env.ParseFromArray(bytes.constData(), bytes.size())) {
        qWarning("first ClientEnvelope malformed (%lld bytes); dropping connection",
                 static_cast<long long>(bytes.size()));
        sock->close();
        return;
    }
    if (!env.has_subscribe()) {
        qWarning("first ClientEnvelope must be Subscribe; dropping connection");
        sock->close();
        return;
    }
    const auto& sub = env.subscribe();
    const QString clientSessionId = QString::fromStdString(sub.session_id());
    const quint64 lastSeq = sub.last_seq();

    // Resume path: client sent a session_id we still have buffered.
    if (!clientSessionId.isEmpty()) {
        auto sit = m_sessions.find(clientSessionId);
        if (sit != m_sessions.end() && sit->sock == nullptr) {
            qInfo("ws resume: session=%s last_seq=%llu",
                  qUtf8Printable(clientSessionId),
                  static_cast<unsigned long long>(lastSeq));
            m_pending.erase(it);
            resumeSession(*sit, sock, lastSeq);
            return;
        }
        if (sit != m_sessions.end() && sit->sock != nullptr) {
            qWarning("ws resume rejected: session=%s already attached to a socket",
                     qUtf8Printable(clientSessionId));
            // Fall through to fresh session below.
        }
    }

    // Fresh session.
    QString id = clientSessionId.isEmpty() ? generateSessionId()
                                           : clientSessionId;
    while (m_sessions.contains(id)) {
        // Honor the client's request to not reuse a stale id; if
        // there's a collision (extremely unlikely with UUIDs), generate
        // a new one and let the client learn it from Snapshot.
        id = generateSessionId();
    }
    m_pending.erase(it);
    attachNewSession(sock, id);

    // Hand the (already-validated) Subscribe to the freshly-created
    // adapter so it triggers the snapshot/tail handshake.
    m_sessions.find(id)->adapter->handleClientBinary(bytes);
}

void WsServer::resumeSession(Session& s, QWebSocket* sock, quint64 lastSeq)
{
    // Cancel the prune timer; we've reattached.
    if (s.pruneTimer) {
        s.pruneTimer->stop();
        s.pruneTimer->deleteLater();
        s.pruneTimer = nullptr;
    }

    // Swap sink: NoopSink → fresh WebSocketSink. The old NoopSink is
    // owned by us; delete after the swap.
    auto* oldSink = s.liveSink;
    s.liveSink = new WebSocketSink(sock);
    s.adapter->setSink(s.liveSink);
    delete oldSink;

    s.sock = sock;
    m_socketToSession.insert(sock, s.adapter->sessionId());

    // Reroute the socket's binary-message stream straight to the
    // adapter, replacing the pending-first-binary lambda.
    sock->disconnect(this);
    connect(sock, &QWebSocket::textMessageReceived,
            s.adapter, [a = s.adapter](const QString& t) {
                a->handleClientText(t);
            });
    connect(sock, &QWebSocket::binaryMessageReceived,
            s.adapter, [a = s.adapter](const QByteArray& b) {
                a->handleClientBinary(b);
            });
    connect(sock, &QWebSocket::disconnected,
            this, [this, sock] { onSocketDisconnected(sock); });

    // Replay everything in the ring buffer past the client's last_seq.
    // If the buffer has been pruned past lastSeq, the client will see
    // a gap — they're expected to compare last_seq against the seq of
    // the first replayed envelope to detect that. (For now we just
    // replay what we have; a richer scheme can ship later if a real
    // user trips it.)
    s.adapter->replaySince(lastSeq);

    if (!m_firstClientFired) {
        m_firstClientFired = true;
        emit firstClientSubscribed();
    }
}

void WsServer::attachNewSession(QWebSocket* sock, const QString& sessionId)
{
    auto* sink = new WebSocketSink(sock);
    auto* adapter = makeAdapter(sink, this);
    adapter->setSessionId(sessionId);

    m_sessions.insert(sessionId, Session{adapter, sink, sock, nullptr});
    m_socketToSession.insert(sock, sessionId);

    // Disconnect the pending lambda; reroute to the adapter.
    sock->disconnect(this);
    connect(sock, &QWebSocket::textMessageReceived,
            adapter, [adapter](const QString& t) {
                adapter->handleClientText(t);
            });
    connect(sock, &QWebSocket::binaryMessageReceived,
            adapter, [adapter](const QByteArray& b) {
                adapter->handleClientBinary(b);
            });
    connect(sock, &QWebSocket::disconnected,
            this, [this, sock] { onSocketDisconnected(sock); });

    qInfo("ws session opened: id=%s (%lld active)",
          qUtf8Printable(sessionId), (long long)m_sessions.size());

    if (!m_firstClientFired) {
        m_firstClientFired = true;
        emit firstClientSubscribed();
    }
}

SessionAdapter* WsServer::makeAdapter(IEnvelopeSink* sink, QObject* parent)
{
    auto* adapter = new SessionAdapter(sink, m_spawnShell, m_zoneMgr, m_player,
                              m_mapData, m_messageShell, m_groupMgr,
                              m_guildShell,
                              m_spellShell, m_combatRouter, m_categoryMgr,
                              m_filterMgr, m_prefsBroker, m_spawnMonitor,
                              m_itemCache, m_dateTimeMgr, m_zoneServerMgr,
                              m_boxes,
                              parent);
    adapter->setMapPackageHost(m_mapPackageHost);
    adapter->setManagerProvider(m_managerProvider);
    return adapter;
}

void WsServer::broadcast(const seq::v1::Envelope& env)
{
    // Fan to every session's adapter. Each adapter stamps its own
    // seq/server_ts_ms and honors its own snapshot-vs-live buffering
    // (deliverBroadcast no-ops on the deterministic golden adapter, which
    // isn't tracked in m_sessions anyway).
    for (auto& sess : m_sessions) {
        if (sess.adapter)
            sess.adapter->deliverBroadcast(env);
    }
}

QString WsServer::generateSessionId() const
{
    // QUuid::createUuid() is random (v4) and uses /dev/urandom on Linux.
    // Strip braces + dashes for a compact string the client can stash
    // in localStorage without escaping.
    return QUuid::createUuid().toString(QUuid::Id128);
}

void WsServer::onSocketDisconnected(QWebSocket* sock)
{
    if (m_lootSockets.remove(sock)) {
        sock->deleteLater();
        qInfo("ws loot client disconnected");
        return;
    }

    // Pending socket that died before sending Subscribe — just clean up.
    if (m_pending.contains(sock)) {
        m_pending.remove(sock);
        sock->deleteLater();
        qInfo("ws pending client disconnected before Subscribe");
        return;
    }

    auto sit = m_socketToSession.find(sock);
    if (sit == m_socketToSession.end()) {
        sock->deleteLater();
        return;
    }
    const QString id = sit.value();
    m_socketToSession.erase(sit);

    auto sessIt = m_sessions.find(id);
    if (sessIt == m_sessions.end()) {
        sock->deleteLater();
        return;
    }
    Session& s = *sessIt;

    // Detach: swap to NoopSink so the adapter keeps consuming signals
    // (filling its ring buffer) but produces nothing externally. Start
    // the prune timer; a resume within 30s reattaches a real sink.
    auto* oldSink = s.liveSink;
    s.liveSink = new NoopSink();
    s.adapter->setSink(s.liveSink);
    delete oldSink;
    s.sock = nullptr;

    s.pruneTimer = new QTimer(this);
    s.pruneTimer->setSingleShot(true);
    connect(s.pruneTimer, &QTimer::timeout,
            this, [this, id] { pruneSession(id); });
    s.pruneTimer->start(kPruneAfterMs);

    sock->deleteLater();
    qInfo("ws session detached: id=%s (%lld active, %dms to prune)",
          qUtf8Printable(id), (long long)m_sessions.size(), kPruneAfterMs);
}

void WsServer::pruneSession(const QString& id)
{
    auto it = m_sessions.find(id);
    if (it == m_sessions.end()) return;
    if (it->sock != nullptr) {
        // Reattached between timer schedule and fire — leave it alone.
        return;
    }
    if (it->adapter) it->adapter->deleteLater();
    delete it->liveSink;
    if (it->pruneTimer) it->pruneTimer->deleteLater();
    m_sessions.erase(it);
    qInfo("ws session pruned: id=%s (%lld remaining)",
          qUtf8Printable(id), (long long)m_sessions.size());
}
