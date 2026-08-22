#pragma once

#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <memory>
#include <unordered_map>
#include <vector>

#include "managerset.h"
#include "mappackagehost.h"
#include "rustsession.h"

// Forward declarations of the extracted showeq types. We keep them out of
// this header to minimize the include footprint for files that only need the
// DaemonApp shape.
class Box;
class DataLocationMgr;
class DateTimeMgr;
class EQPacket;
class EQPacketStream;
class EQStr;
class DbStrings;
class SpellMessages;
class CategoryMgr;
class CombatRouter;
class FileSink;
class FilterMgr;
class ItemCache;
class LootStore;
class OpcodeStatsLogger;
class GroupMgr;
class GuildShell;
class GuildMgr;
class MapData;
class MessageFilters;
class MessageShell;
class Messages;
class Player;
class PrefsBroker;
class SessionAdapter;
class Spells;
class SpawnMonitor;
class SpawnShell;
class SpellShell;
class WsServer;
class ZoneMgr;
class ZoneServerMgr;

// DaemonApp is the top-level wiring hub for the daemon. It owns the packet
// capture + decoder + state managers and the WebSocket server that exposes
// them to clients.
//
// Phase 1 scope: minimum object graph to stream spawn positions to a client.
// That means EQPacket + ZoneMgr + Player + SpawnShell + their dependencies.
// Spell, group, chat, experience, combat, filter-notification subsystems are
// deferred to later phases.
class DaemonApp : public QObject, public IMapPackageHost,
                  public ManagerSetProvider {
    Q_OBJECT
public:
    struct Config {
        QString      device;
        // EQ client IP filter for the pcap BPF. Empty = read from XML
        // pref (Network/IP) which itself defaults to AUTOMATIC_CLIENT_IP
        // (auto-detect on first session handshake). Set explicitly when
        // multiple EQ clients share a LAN — the auto-detect path locks
        // onto whichever client's session it sees first.
        QString      ip;
        // If set ("host:port"), capture frames from a remote scry-agent over TCP
        // instead of a local libpcap device — the daemon dials the agent and
        // requests a BPF filter. Mutually exclusive with a live --device;
        // ignored when replaying. Needs no cap_net_raw (the agent captures).
        QString      agent;
        QString      replay;
        // If true, `replay` holds a raw pcap/tcpdump file (--replay-pcap) fed
        // through libpcap's offline reader, rather than a .vpk recorded by this
        // daemon (--replay). Selects PLAYBACK_FORMAT_TCPDUMP over
        // PLAYBACK_FORMAT_SEQ; every other aspect of a replay session (no
        // device, quit at EOF, persistence disabled) is identical.
        bool         replayIsPcap = false;
        // If set, opcode XML and other shared data is read from this
        // directory. Convenient for running against the in-tree conf/
        // without installing to PKGDATADIR.
        QString      configDir;
        // Overrides the zone-map search directory. Empty string triggers
        // the default cascade: ~/.scry/maps → $configDir/maps.
        QString      mapsDir;
        // Selects the active map package (subdirectory under a maps root,
        // or "default" for the flat root). Empty = restore the persisted
        // value (TomlPreferences [Maps] Package), itself defaulting to
        // "default". A CLI --map-package value overrides the persisted one.
        QString      mapPackage;
        // If set, raw EQ packets are recorded to this .vpk path while
        // capturing — wraps the legacy TomlPreferences `[VPacket]` mode.
        QString      recordVpk;
        // If set, an internal SessionAdapter writes the envelope stream
        // it would have sent to a client into this file as length-
        // delimited seq.v1.Envelope protobuf. With --replay set, the
        // daemon exits at EOF (golden generation workflow).
        QString      recordGolden;
        // If set, OpcodeStatsLogger taps EQPacket's decoded-packet
        // signals and writes a per-opcode tally to this file at
        // shutdown — patch-day diagnostic for finding ffff opcodes.
        QString      opcodeStats;
        // If non-empty, OpcodePayloadDumper writes the raw payload of
        // matching zone opcodes to disk. Each entry is "OPCODE:PATH"
        // (opcode in 0xHEX form). Recon tool: pair two captures and
        // byte-diff the dumps to locate where field X lives.
        QStringList  dumpPayload;
        // If set, EventLogger writes one line per decoded packet to this
        // path (timestamp + dir + opcode + size + name). Time-correlation
        // recon: which C>S fired right before the OP_PlayerProfile that
        // showed the new aa_spent value? — slice externally with awk/grep.
        QString      listEvents;
        // If true, BoxRegistry is dumped to stderr every 5s. Stage 1
        // of the multibox feature (docs/MULTIBOX_PLAN.md). Pairs with
        // --no-listen for client-less recon.
        bool         listBoxes = false;
        // If true, forward EVERY box's decoded packets to the recon signals
        // (--dump-payload / --opcode-stats / --list-events), not just the
        // primary box's. Multibox/multi-zone captures otherwise only surface
        // the first world session seen, so a later zone's opcodes are missed
        // (the documented primary-box recon limitation). Recon-only: does not
        // affect proto output or goldens.
        bool         dumpAllSessions = false;
        // If non-empty, restrict the recon signals to ONE box/session.
        // Selector: a character name (case-insensitive, matched when the
        // name resolves — OP_PlayerProfile on live, the player's own
        // ZoneSpawns entry on eql), a 1-based session index in discovery
        // order, or "first" (= index 1). Overrides --dump-all-sessions.
        // Recon-only: does not affect proto output or goldens.
        QString      onlySession;
        // Immutable template copied into every Rust Session. "legacy" keeps
        // host lifecycle handlers, "shadow" compares them, and "rust" lets
        // only the typed Rust lifecycle path mutate state.
        QString      lifecycleDecoder = QStringLiteral("shadow");
        // Immutable entity-family owner. Legacy remains the default until
        // capture-derived Phase 5 soak fixtures are available for both hosts.
        QString      entityDecoder = QStringLiteral("legacy");
        QString      playerDecoder = QStringLiteral("legacy");
        // --replay --wait-for-client: pause the .vpk playback until
        // the first WebSocket client attaches a SessionAdapter (so
        // early envelopes aren't dropped), and don't quit at EOF so
        // the user can inspect final state in the UI. Useful for
        // manual verification of UI features against recorded
        // captures.
        bool         waitForClient = false;
        // True to skip the WebSocket server entirely (--no-listen on
        // the CLI). Useful for capture / replay / diagnostic runs
        // where no client connects and the listen port is just a
        // collision risk against the user's main daemon instance.
        bool         noListen = false;
        // --strict-gate-sizes: exit non-zero when the backend gate-size
        // audit flags any mapped SZC_Match opcode still gating on an
        // inherited Live sizeof (see warnUndeclaredBackendGateSizes).
        // Default off (warn-only); CI's per-target opcode-load smoke
        // sets it so an undeclared gate size fails the build.
        bool         strictGateSizes = false;
        // Idle TTL (ms) after which BoxRegistry::evictStale reclaims a box
        // whose EQ session has gone silent — reaping the per-zone Box churn
        // a long multibox session accumulates. --box-idle-ttl SECONDS sets
        // it; 0 disables eviction entirely. Not applied to --replay runs
        // (their wall-clock last_seen stays fresh over a short playback).
        qint64       boxIdleTtlMs = 10 * 60 * 1000;
        // B2: the box->ManagerSet model is now SINGLE (model B / truebox): every
        // non-primary Box wires into the ONE persistent m_activeManagers, refreshed
        // at enterWorld (see onBoxCreated). The per-box-set + active-box-rebind path
        // (model A / Live same-host multibox) and its --multibox flag are removed.
        QHostAddress listenHost;
        quint16      listenPort = 9090;
    };

    explicit DaemonApp(Config cfg, QObject* parent = nullptr);
    ~DaemonApp() override;

    // Brings up the WebSocket server, constructs the object graph, wires
    // opcodes, and starts the capture pipeline. Returns false on
    // unrecoverable setup failure (bad interface, port in use, missing
    // config files).
    bool start();

    // Write session state to configDir/.handoff so the next binary can
    // restore it without rezoning. Call before QCoreApplication::quit()
    // on SIGHUP. No-op if the capture pipeline was never started.
    void exportHandoffState(const QString& configDir) const;

    // Read and apply configDir/.handoff if it exists. Call after start()
    // but before app.exec() — pcap fires via Qt signals so no packets
    // are processed until the event loop begins. Returns true if state
    // was restored, false if no handoff file was present.
    bool importHandoffState(const QString& configDir);

    // --- IMapPackageHost ---------------------------------------------------
    // Discover packages across all map search roots (always includes the
    // synthetic "default"). zone_count counts base zone files, ignoring
    // numbered _N layer files.
    std::vector<MapPackageInfo> mapPackages() const override;
    QString activeMapPackage() const override { return m_mapPackage; }
    // Set + persist active package (falls back to "default" if unknown),
    // re-resolve the current zone, then broadcast MapPackagesUpdate +
    // ZoneChanged to every connected client. Returns the applied id.
    QString setMapPackage(const QString& id) override;

    // Discovery over an explicit set of roots — exposed for unit tests so
    // they don't depend on the DataLocationMgr cascade.
    std::vector<MapPackageInfo> mapPackagesIn(const QStringList& roots) const;

    // --- ManagerSetProvider ------------------------------------------------
    // Resolve a box's per-box ManagerSet (empty id = active box). Returns
    // nullptr if unknown. Backed by m_boxManagers (populated in
    // onBoxCreated); falls back to the active set.
    const ManagerSet* managersForBox(const QString& boxId) const override;

private slots:
    // Mirrors showeq/src/map.cpp:370 — MapMgr::loadZoneMap. Called on every
    // ZoneMgr::zoneChanged so SessionAdapter has fresh geometry to stream.
    void loadZoneMap(const QString& shortZoneName);

private:
    bool startServer();
    bool startCapture();
    // Construct one per-box set of state managers (ZoneMgr, Player,
    // SpawnShell, SpellShell, GroupMgr, MessageShell, CombatRouter,
    // SpawnMonitor) and run the cross-manager connect()s between them.
    // The daemon-global managers (GuildMgr, FilterMgr, Messages, Spells,
    // ...) must already be constructed — they're shared into every set.
    // Multibox will call this once per box; today it's called once for
    // the single active set. Does NOT wire opcode dispatch (see
    // wireZoneMgr/wireSpawnShell) or assign m_* members — the caller does.
    ManagerSet buildManagerSet();
    // Wire one box's four decode streams to one ManagerSet's managers
    // (plus the daemon-global ItemCache/DateTimeMgr/ZoneServerMgr). This
    // is the single source of opcode→handler wiring: start() calls it for
    // the active set on the global streams; onBoxCreated() calls it per
    // non-primary box on that box's own streams. The connect() order here
    // is golden-sensitive (shared-opcode dispatch order) — preserve it.
    // wireGlobalSinks: also wire the daemon-global sinks (ItemCache,
    // DateTimeMgr, ZoneServerMgr) onto these streams. Only the active box
    // should — feeding them from every box emits redundant ZoneServer /
    // EqTimeSync / ItemLearned envelopes.
    void wireBoxPipeline(EQPacketStream* worldC2S, EQPacketStream* worldS2C,
                         EQPacketStream* zoneC2S, EQPacketStream* zoneS2C,
                         const ManagerSet& ms, bool wireGlobalSinks);
    // BoxRegistry::boxCreated handler. Primary box reuses the active set
    // (already wired to the global streams in start()); every other box
    // gets its own ManagerSet + wireBoxPipeline on its own streams.
    void onBoxCreated(Box* box);
    // BoxRegistry::boxAboutToBeRemoved handler. Drops the evicted box's
    // ManagerSet record and deleteLater's its per-box manager root (the
    // reverse of onBoxCreated). No-op for the primary box.
    void onBoxAboutToBeRemoved(Box* box);
    void applyRustLifecycle(const Box* box, const seq::shadow::Event& event);
    void applyRustEntity(const Box* box, const seq::shadow::Event& event);
    void applyRustPlayer(const Box* box, const seq::shadow::Event& event);
    void connectLifecycleObservers();
    void connectEntityObservers();
    void connectPlayerObservers();

    // --only-session helpers -------------------------------------------------
    // Parsed index form of Config::onlySession: N for "N"/"first"(=1),
    // 0 when the selector is a character name.
    int onlySessionOrdinal() const;
    // Connect a box's four streams onto the global recon signals
    // (idempotent per box). Used by --dump-all-sessions (every non-primary
    // box) and --only-session (the one matching box; resolves the primary
    // box's streams to EQPacket's globals).
    void relayReconTaps(Box* box);
    // Name-selector check: relay `box` if `name` matches Config::onlySession.
    void onlySessionNameCheck(Box* box, const QString& name);

    Config                          m_cfg;

    std::unique_ptr<DataLocationMgr> m_dataLocationMgr;
    DateTimeMgr*                    m_dateTimeMgr   = nullptr;
    ZoneServerMgr*                  m_zoneServerMgr = nullptr;
    EQPacket*                       m_packet        = nullptr;
    Spells*                         m_spells        = nullptr;
    EQStr*                          m_eqStrings     = nullptr;
    DbStrings*                      m_dbStrings     = nullptr;
    SpellMessages*                  m_spellMessages = nullptr;
    ZoneMgr*                        m_zoneMgr       = nullptr;
    GuildMgr*                       m_guildMgr      = nullptr;
    Player*                         m_player        = nullptr;
    FilterMgr*                      m_filterMgr     = nullptr;
    SpawnShell*                     m_spawnShell    = nullptr;
    SpawnMonitor*                   m_spawnMonitor  = nullptr;
    SpellShell*                     m_spellShell    = nullptr;
    GroupMgr*                       m_groupMgr      = nullptr;
    GuildShell*                     m_guildShell    = nullptr;
    CategoryMgr*                    m_categoryMgr   = nullptr;
    ItemCache*                      m_itemCache     = nullptr;
    // Daemon-global loot history; one DB per backend namespace.
    // Left unopened under --replay so fixtures never reach it.
    std::unique_ptr<LootStore>      m_lootStore;
    MessageFilters*                 m_messageFilters = nullptr;
    Messages*                       m_messages       = nullptr;
    MessageShell*                   m_messageShell   = nullptr;
    CombatRouter*                   m_combatRouter   = nullptr;
    PrefsBroker*                    m_prefsBroker    = nullptr;

    // The active box's ManagerSet — what the m_* members above mirror and
    // what wireBoxPipeline binds to the global streams at startup.
    ManagerSet                      m_activeManagers;
    // Per-box ManagerSet bundles, keyed by the stable Box* (box_id mutates
    // placeholder→name-hash on promotion, so it's a poor key). This map
    // records which set belongs to which box so SessionAdapter can resolve
    // one; the managers themselves hang off the per-box root below.
    QHash<const Box*, ManagerSet>   m_boxManagers;
    // Per-box parent QObject owning a non-primary box's ManagerSet managers.
    // Reparented here in onBoxCreated so BoxRegistry::evictStale can reclaim
    // the whole set with one deleteLater (order-safe subtree teardown). The
    // primary box reuses m_activeManagers and has no entry here.
    QHash<const Box*, QObject*>     m_boxManagerRoots;
    // --only-session bookkeeping: sessions seen (1-based discovery order,
    // counted in onBoxCreated) and boxes already relayed onto the recon
    // signals (guards double-connects when a name resolves repeatedly).
    int                             m_boxOrdinal = 0;
    QSet<const Box*>                m_reconRelayed;
    std::unique_ptr<MapData>        m_mapData;
    // Active map package id ("default" = flat maps root). Restored from
    // TomlPreferences in start(), overridden by Config::mapPackage.
    QString                         m_mapPackage = QStringLiteral("default");

    std::unique_ptr<WsServer>       m_ws;

    // Set when --record-golden is passed. The sink is owned here; the
    // adapter is parented to `this` so Qt cleans it up on shutdown.
    std::unique_ptr<FileSink>       m_goldenSink;
    SessionAdapter*                 m_goldenAdapter  = nullptr;

    // Set when --opcode-stats is passed. Parented to `this` so the
    // dtor's writeReport() runs as part of normal Qt teardown.
    OpcodeStatsLogger*              m_opcodeStats    = nullptr;

    // One per --dump-payload OPCODE:PATH pair. Parented to `this`.
    QList<class OpcodePayloadDumper*> m_payloadDumpers;

    // Set when --list-events is passed. Parented to `this` so the dtor
    // flushes the file as part of normal Qt teardown.
    class EventLogger*              m_eventLogger    = nullptr;
};
