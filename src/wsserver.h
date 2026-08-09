#pragma once

#include <QHash>
#include <QSet>
#include <QHostAddress>
#include <QObject>
#include <QString>
#include <memory>

class QTimer;
class QWebSocket;
class QWebSocketServer;
class CategoryMgr;
class CombatRouter;
class DateTimeMgr;
class FilterMgr;
class GroupMgr;
class GuildShell;
class IEnvelopeSink;
class ItemCache;
class MapData;
class MessageShell;
class Player;
class PrefsBroker;
class BoxRegistry;
class SessionAdapter;
class IMapPackageHost;
class ManagerSetProvider;
class SpawnMonitor;
class SpawnShell;
class LootStore;
class SpellShell;
class ZoneMgr;
class ZoneServerMgr;

namespace seq::v1 { class Envelope; }

// WsServer owns a QWebSocketServer on the same event loop as the capture
// pipeline. It also brokers session resume:
//
//   - Each accepted socket starts in "pending" state. The first inbound
//     binary frame must be a Subscribe; WsServer parses it to decide
//     whether to attach to an existing detached SessionAdapter (resume)
//     or spin up a fresh one.
//   - On WebSocket disconnect, the SessionAdapter is NOT destroyed —
//     its sink swaps to a NoopSink and a 30s prune timer starts. The
//     adapter keeps consuming state-manager signals into its ring
//     buffer so a quick reconnect can replaySince(last_seq).
//   - On 30s timer fire (or explicit shutdown), the adapter is
//     destroyed.
class WsServer : public QObject {
    Q_OBJECT
public:
    explicit WsServer(QObject* parent = nullptr);
    ~WsServer() override;

    // Called by DaemonApp after the state managers are constructed. The
    // pointers must outlive the WsServer; they're not owned.
    void setState(SpawnShell* ss, ZoneMgr* zm, Player* p, MapData* md,
                  MessageShell* ms, GroupMgr* gm, GuildShell* gus,
                  SpellShell* sps,
                  CombatRouter* cr, CategoryMgr* cm, FilterMgr* fm,
                  PrefsBroker* pb, SpawnMonitor* sm, ItemCache* ic,
                  DateTimeMgr* dtm, ZoneServerMgr* zsm,
                  BoxRegistry* boxes);

    // Borrowed map-package host (DaemonApp). Forwarded to each
    // SessionAdapter so clients can list/select packages. Set after
    // setState, before listen().
    void setMapPackageHost(IMapPackageHost* host) { m_mapPackageHost = host; }
    // Backs the /loot path. Null (or a closed store, as under --replay) makes a
    // LootQuery answer with an empty page rather than fail.
    void setLootStore(LootStore* store) { m_lootStore = store; }
    void setManagerProvider(ManagerSetProvider* p) { m_managerProvider = p; }

    // Fan a server->client Envelope out to every connected session's
    // adapter. Used for daemon-global state changes (e.g. active map
    // package) that all clients must see. Each adapter stamps its own
    // seq + server_ts_ms.
    void broadcast(const seq::v1::Envelope& env);

    bool listen(const QHostAddress& host, quint16 port);

signals:
    // Fires exactly once, on the first client to attach a
    // SessionAdapter (either fresh attachNewSession or resume).
    // Used by --replay --wait-for-client to gate playback start
    // on a real consumer being wired up.
    void firstClientSubscribed();

private slots:
    void onNewConnection();

private:
    // Per-active-socket state. Until the first Subscribe lands, `adapter`
    // is null and only the socket is tracked.
    struct PendingSocket {
        QWebSocket*    sock;
    };

    // Per-session state, keyed by session_id. Outlives any one
    // QWebSocket so a brief disconnect can be resumed.
    struct Session {
        SessionAdapter* adapter;
        IEnvelopeSink*  liveSink;     // current sink (WebSocketSink while connected, NoopSink while detached)
        QWebSocket*     sock;         // null while detached
        QTimer*         pruneTimer;   // non-null while detached
    };

    void onPendingFirstBinary(QWebSocket* sock, const QByteArray& bytes);
    // /loot connections are query-only: no SessionAdapter, no resume, no
    // envelope stream. A loot client should not have to take the spawn
    // firehose to read history.
    void onLootBinary(QWebSocket* sock, const QByteArray& bytes);
    void onSocketDisconnected(QWebSocket* sock);
    void resumeSession(Session& s, QWebSocket* sock, quint64 lastSeq);
    void attachNewSession(QWebSocket* sock, const QString& sessionId);
    SessionAdapter* makeAdapter(IEnvelopeSink* sink, QObject* parent);
    QString generateSessionId() const;
    void pruneSession(const QString& id);

    std::unique_ptr<QWebSocketServer>   m_server;
    QHash<QWebSocket*, PendingSocket>   m_pending;
    QHash<QString, Session>             m_sessions;
    QHash<QWebSocket*, QString>         m_socketToSession;
    QSet<QWebSocket*>                   m_lootSockets;

    SpawnShell*    m_spawnShell    = nullptr;
    ZoneMgr*       m_zoneMgr       = nullptr;
    Player*        m_player        = nullptr;
    MapData*       m_mapData       = nullptr;
    MessageShell*  m_messageShell  = nullptr;
    GroupMgr*      m_groupMgr      = nullptr;
    GuildShell*    m_guildShell    = nullptr;
    SpellShell*    m_spellShell    = nullptr;
    CombatRouter*  m_combatRouter  = nullptr;
    CategoryMgr*   m_categoryMgr   = nullptr;
    FilterMgr*     m_filterMgr     = nullptr;
    PrefsBroker*   m_prefsBroker   = nullptr;
    SpawnMonitor*  m_spawnMonitor  = nullptr;
    ItemCache*     m_itemCache     = nullptr;
    DateTimeMgr*   m_dateTimeMgr   = nullptr;
    ZoneServerMgr* m_zoneServerMgr = nullptr;
    BoxRegistry*   m_boxes         = nullptr;
    bool           m_firstClientFired = false;
    IMapPackageHost* m_mapPackageHost = nullptr;
    LootStore*     m_lootStore     = nullptr;
    ManagerSetProvider* m_managerProvider = nullptr;
};
