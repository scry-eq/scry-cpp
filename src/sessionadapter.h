#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <cstdint>
#include <deque>

#include "seq/v1/events.pb.h"

class IEnvelopeSink;
class BoxRegistry;
class CategoryMgr;
class CombatRouter;
class DateTimeMgr;
class FilterMgr;
class GroupMgr;
class GuildShell;
class Item;
class ItemCache;
class MapData;
class MessageShell;
class Player;
class PrefsBroker;
class Spawn;
class SpawnMonitor;
class SpawnPoint;
class SpawnShell;
class SpellItem;
class SpellShell;
class ZoneMgr;
class ZoneServerMgr;
class IMapPackageHost;
class ManagerSetProvider;
struct inspectDataStruct;

// SessionAdapter is the per-client bridge between in-process QObject signals
// (SpawnShell::addItem, ZoneMgr::zoneChanged, Player::posChanged, ...) and
// the seq.v1 protobuf stream a connected client consumes.
//
// The transport (QWebSocket in production, a collecting buffer in tests) is
// abstracted behind IEnvelopeSink so the adapter has no direct Qt-network
// dependency. WsServer owns the socket-side wiring; tests instantiate the
// adapter directly with a fake sink.
//
// Snapshot/tail race — read before editing:
// When a client subscribes, we MUST connect signals FIRST (with handlers
// buffering to m_buffered, not sending), THEN iterate SpawnShell state into
// a Snapshot, THEN drain m_buffered, THEN flip m_liveTailing. Any other
// order loses or duplicates spawns added during the iteration. See
// startStreaming() for the implementation.
class SessionAdapter : public QObject {
    Q_OBJECT
public:
    SessionAdapter(IEnvelopeSink* sink,
                   SpawnShell*    spawnShell,
                   ZoneMgr*       zoneMgr,
                   Player*        player,
                   MapData*       mapData,
                   MessageShell*  messageShell,
                   GroupMgr*      groupMgr,
                   GuildShell*    guildShell,
                   SpellShell*    spellShell,
                   CombatRouter*  combatRouter,
                   CategoryMgr*   categoryMgr,
                   FilterMgr*     filterMgr,
                   PrefsBroker*   prefsBroker,
                   SpawnMonitor*  spawnMonitor,
                   ItemCache*     itemCache,
                   DateTimeMgr*   dateTimeMgr,
                   ZoneServerMgr* zoneServerMgr,
                   BoxRegistry*   boxes,
                   QObject*       parent = nullptr);
    ~SessionAdapter() override;

    // Inbound client traffic. WsServer wires QWebSocket::textMessageReceived
    // and binaryMessageReceived to these. Tests call them directly with the
    // serialized ClientEnvelope they want to deliver.
    void handleClientText(const QString& text);
    void handleClientBinary(const QByteArray& bytes);

    // Session resume API used by WsServer.
    //
    // sessionId(): the id this adapter is keyed by in WsServer's session
    //   map. Empty until the daemon assigns one (on first Subscribe).
    // setSessionId(): seed before the first Subscribe so a Snapshot
    //   emitted during startStreaming() carries it.
    // setSink(): swap the output sink (e.g. WebSocket → Noop on
    //   disconnect, Noop → new WebSocket on resume) without tearing
    //   down signal subscriptions or losing the ring buffer.
    // replaySince(): re-emit every buffered envelope with seq > last_seq
    //   through the current sink, in order. Used after a sink swap on
    //   resume to fill the gap between the client's last_seq and the
    //   live stream.
    QString sessionId() const { return m_sessionId; }
    void setSessionId(const QString& id) { m_sessionId = id; }
    void setSink(IEnvelopeSink* sink) { m_sink = sink; }
    void replaySince(uint64_t lastSeq);

    // When set, encoders strip wall-clock fields (currently
    // SpawnPoint.spawn_time_s/death_time_s/diff_time_s) so the
    // tier-2 byte-cmp regression harness produces stable bytes
    // across runs. DaemonApp turns this on for the --record-golden
    // adapter only; live client adapters keep wall-clock data.
    void setDeterministic(bool on) { m_deterministic = on; }

    // Borrowed map-package host (DaemonApp), used to answer ListMapPackages
    // and apply SetMapPackage. Set by WsServer on connect; may be null in
    // unit tests / smoke-test modes (handlers then no-op gracefully).
    void setMapPackageHost(IMapPackageHost* host) { m_mapPackageHost = host; }

    // Resolver for the active character's ManagerSet. Set by WsServer on
    // connect (and by DaemonApp for the golden adapter). Read once at Subscribe
    // to re-point to the active character's managers if it was promoted between
    // construction and Subscribe. Null in unit tests / smoke modes — no-ops.
    void setManagerProvider(ManagerSetProvider* p) { m_managerProvider = p; }

    // Deliver a daemon-broadcast Envelope to this client (emit if live-
    // tailing, else buffer through the snapshot). Used by WsServer::broadcast
    // for daemon-global state changes like the active map package.
    void deliverBroadcast(const seq::v1::Envelope& env);

private slots:
    // Signal handlers wired to the shared state managers. Before
    // m_liveTailing flips true, these push envelopes into m_buffered
    // instead of sending them.
    void onAddItem(const Item* item);
    void onDelItem(const Item* item);
    void onChangeItem(const Item* item, uint32_t changeType);
    void onKillSpawn(const Item* deceased, const Item* killer,
                     uint16_t killerId);
    void onZoneBegin(const QString& shortName);
    void onZoneChanged(const QString& shortName);
    // Coalesces the various Player::*Changed signals into a single
    // PlayerStats envelope. Slot signatures are deliberately broad so we
    // can connect any Player signal to it without per-signal adapters.
    void onPlayerStatsChanged();
    // Re-issues a Snapshot when the player's spawn ID changes. Snapshot
    // carries player_id, so this is the simplest way to keep the
    // client's player tracking in sync with the daemon's current view.
    void onPlayerIdChanged();
    // Emits a seq.v1.ChatMessage envelope from a MessageShell::chatMessage
    // signal.
    void onChatMessage(uint32_t channel, const QString& from,
                       const QString& target, const QString& text,
                       uint32_t chatColor,
                       const QString& channelName = QString());
    // Emits a seq.v1.LootTransaction envelope per completed corpse-loot.
    void onLootTransaction(uint32_t corpseId, uint32_t itemId,
                           uint32_t quantity, uint32_t coinCopper);
    // Re-emits the full group state on any GroupMgr add/remove/clear.
    void onGroupChanged();
    // Re-emits the whole roster; it is replaced wholesale, never diffed, so
    // GuildShell::loaded() connects straight here.
    void sendGuildRoster();
    // Re-emits the guild MOTD on GuildShell::motdChanged().
    void sendGuildMotd();
    // Re-emits the full active-buff list on any SpellShell change.
    void onBuffsChanged();
    void onEffectsChanged();
    // Forwards a CombatRouter combatEvent signal as a CombatEvent envelope.
    void onCombatEvent(uint32_t sourceId, const QString& sourceName,
                       uint32_t targetId, const QString& targetName,
                       uint32_t type, int32_t damage,
                       uint32_t spellId, const QString& spellName);
    // Forwards a CombatRouter spawnCast signal as a SpawnCast envelope (eql).
    void onSpawnCast(uint32_t casterId, const QString& casterName,
                     uint32_t spellId, const QString& spellName,
                     uint32_t castTimeMs);
    // Re-emits the full category list on any CategoryMgr change.
    void onCategoriesChanged();
    // Re-emits the full filter rule set on any FilterMgr filtersChanged.
    void onFilterRulesChanged();
    // Forwards a PrefsBroker prefChanged signal as a PrefChanged envelope.
    void onPrefChanged(const seq::v1::Pref& pref);
    // Forwards SpawnShell::spawnConsidered as a Considered envelope.
    void onSpawnConsidered(const Item* item);
    // Forwards SpawnShell::targetSpawn as a Targeted envelope.
    void onTargetSpawn(uint32_t spawnId);
    // SpawnMonitor signal handlers — pushed as
    // SpawnPointAdded/Updated/Removed/Cleared envelopes. Reads from the
    // SpawnPoint* directly inside the slot; the pointer must not be
    // stored across slot returns (deleted slots delete the SpawnPoint
    // synchronously after emitting).
    void onSpawnPointAdded(const SpawnPoint* sp);
    void onSpawnPointUpdated(const SpawnPoint* sp);
    void onSpawnPointDeleted(const SpawnPoint* sp);
    void onSpawnPointsCleared();
    // ItemCache::itemLearned handler. Pushes the item out as a single
    // ItemLearned envelope.
    void onItemLearned(uint32_t itemId);

    // ItemCache::wornSlotsChanged handler. Pushes a fresh WornSet plus
    // a recomputed ItemCacheTotals (totals are worn-only, see proto).
    void onWornSlotsChanged();

    // DateTimeMgr::syncDateTime handler. Caches the most recent OP_TimeOfDay
    // and emits an EqTimeSync envelope. We deliberately do NOT subscribe to
    // updatedDateTime — that signal fires on the daemon's internal timer
    // and would flood every connected client; clients extrapolate locally.
    void onEqTimeSync(const QDateTime& dt);

    // ZoneServerMgr::zoneServerChanged handler. Caches the new endpoint
    // and emits a ZoneServer envelope.
    void onZoneServerChanged(const QString& host, quint16 port);

    // MessageShell::inspectReceived handler. Pushes an InspectAnswer envelope
    // to the client with item names and bio for the inspected spawn.
    void onInspectAnswer(const inspectDataStruct* data);
    void onLootDrops(uint32_t corpseId, const QString& corpseName,
                     const QStringList& names, const QVector<uint32_t>& icons,
                     const QVector<uint32_t>& itemIds);

    // Player::expGained handler. Pushes an ExperienceTick envelope for each
    // kill-attributed XP gain.
    void onExpGained(const QString& mobName, int mobLevel,
                     long xpGained, const QString& zoneName);

private:
    void startStreaming();
    // Connect this adapter's slots to the eight managers it points at
    // (m_spawnShell, m_zoneMgr, m_player, m_messageShell, m_groupMgr,
    // m_spellShell, m_combatRouter, m_spawnMonitor). startStreaming() calls
    // connectPerBox() once. The daemon-global managers (ItemCache, CategoryMgr,
    // FilterMgr, PrefsBroker, DateTimeMgr, ZoneServerMgr) are wired directly in
    // startStreaming() and never rebind. Truebox model: the managers are the
    // one persistent per-character ManagerSet, so there is no live re-bind.
    void connectPerBox();
    void sendSnapshot();
    void sendPlayerStats();
    void sendGroupUpdate();
    void sendBuffsUpdate();
    void sendEffectsUpdate();
    void sendCategoriesUpdate();
    void sendFilterRulesUpdate();
    void sendPrefsSnapshot();
    // Stage 4 of multibox-sessions: emit BoxListUpdated to this
    // client. Wired to BoxRegistry::changed in the ctor; also called
    // explicitly on Subscribe so the client gets an initial picker
    // state before any registry mutation.
    void sendBoxList();
    // Stage 3c: clear singleton state managers and re-emit a fresh
    // Snapshot when the active box flips. The new box's state
    // populates as its traffic flows in (which started moments before
    // the switch, since per-box streams stay unmuted for the active
    // box and muted for everyone else).
    void sendMapPackages();
    void emitEnvelope(seq::v1::Envelope&& env);
    void sendOrBuffer(seq::v1::Envelope&& env);

    IEnvelopeSink*               m_sink         = nullptr;
    SpawnShell*                  m_spawnShell   = nullptr;
    ZoneMgr*                     m_zoneMgr      = nullptr;
    Player*                      m_player       = nullptr;
    MapData*                     m_mapData      = nullptr;
    MessageShell*                m_messageShell = nullptr;
    GroupMgr*                    m_groupMgr     = nullptr;
    GuildShell*                  m_guildShell   = nullptr;
    SpellShell*                  m_spellShell   = nullptr;
    CombatRouter*                m_combatRouter = nullptr;
    CategoryMgr*                 m_categoryMgr  = nullptr;
    FilterMgr*                   m_filterMgr    = nullptr;
    PrefsBroker*                 m_prefsBroker  = nullptr;
    SpawnMonitor*                m_spawnMonitor = nullptr;
    ItemCache*                   m_itemCache    = nullptr;
    DateTimeMgr*                 m_dateTimeMgr  = nullptr;
    ZoneServerMgr*               m_zoneServerMgr = nullptr;
    BoxRegistry*                 m_boxes         = nullptr;
    IMapPackageHost*             m_mapPackageHost = nullptr;
    ManagerSetProvider*          m_managerProvider = nullptr;

    QString                      m_sessionId;
    // Short name of the zone the last Snapshot carried. onZoneChanged re-sends a
    // Snapshot once the new zone resolves (a zone-in Snapshot fires early, on
    // self-id adoption, with the OLD zone name); this dedupes that so the eql
    // double-signal + same-zone map-package re-emits don't double-send.
    QString                      m_lastSnapshotZone;
    bool                         m_subscribed = false;
    bool                         m_liveTailing = false;
    bool                         m_deterministic = false;
    // Coalesces bursts of SpellShell effect signals (a mob's OP_BuffList fires
    // one signal per buff) into a single SpawnEffectsUpdate per event-loop turn.
    // Live only — deterministic (golden) mode emits synchronously.
    bool                         m_effectsDirty = false;
    uint64_t                     m_seq = 0;
    QList<seq::v1::Envelope>     m_buffered;

    // Resume ring buffer. Every emitted envelope is appended (with its
    // populated seq + server_ts_ms) and pruned from the front by a
    // 30-second time window plus a hard 10k-entry cap. WsServer keeps
    // the adapter alive past WebSocket disconnect (via setSink → noop)
    // so this buffer continues filling and a quick reconnect can
    // replaySince(last_seq) without losing intermediate events.
    std::deque<seq::v1::Envelope> m_ringBuffer;

    // Per-session leaky-bucket rate limit on inbound ClientEnvelopes.
    // Trusted-LAN tool, so the limit is "polite human" — a runaway loop
    // or fuzzing client trips it but a normal UI session doesn't notice.
    qint64                       m_bucketLastMs = 0;
    double                       m_bucketTokens = 0.0;
    bool                         m_rateLimitWarned = false;
};
