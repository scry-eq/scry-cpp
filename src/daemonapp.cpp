#include "daemonapp.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QSet>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QTimer>

#include "main.h"

#include "category.h"
#include "combatrouter.h"
#include "datalocationmgr.h"
#include "datetimemgr.h"
#include "dbstrings.h"
#include "eqstr.h"
#include "everquest.h"
#include "spellmessages.h"
#include "filesink.h"
#include "filtermgr.h"
#include "group.h"
#include "guildshell.h"
#include "guild.h"
#include "itemcache.h"
#include "lootstore.h"
#include "mapcore.h"
#include "messagefilter.h"
#include "messages.h"
#include "messageshell.h"
#include "opcodestats.h"
#include "opcodepayloaddumper.h"
#include "eventlogger.h"
#include "packet.h"
#include "packetstream.h"
#include "boxregistry.h"
#include "packetcommon.h"
#include "packetinfo.h"
#include "player.h"
#include "prefsbroker.h"
#include "sessionadapter.h"
#include "spawnmonitor.h"
#include "spawnshell.h"
#include "spells.h"
#include "spellshell.h"
#include "wsserver.h"
#include "protoencoder.h"
#include "tomlpreferences.h"
#include "zonemgr.h"
#include "zoneservermgr.h"

#include "seq/v1/client.pb.h"

namespace seq { void initGlobals(const QString& def, const QString& user); }

DaemonApp::DaemonApp(Config cfg, QObject* parent)
    : QObject(parent)
    , m_cfg(std::move(cfg))
    , m_mapData(std::make_unique<MapData>())
    , m_ws(std::make_unique<WsServer>(this))
{
}

DaemonApp::~DaemonApp()
{
    // The golden adapter's m_sink points at m_goldenSink (a unique_ptr
    // member). Members are torn down in reverse declaration order, but
    // m_goldenAdapter is a raw pointer cleaned up by ~QObject much
    // later — that would leave m_sink dangling. Tear down explicitly
    // here so the adapter stops before the sink it writes through.
    if (m_goldenAdapter) {
        delete m_goldenAdapter;
        m_goldenAdapter = nullptr;
    }
}

bool DaemonApp::start()
{
    if (!m_cfg.noListen) {
        if (!startServer()) {
            return false;
        }
    } else {
        qInfo("--no-listen: WebSocket server disabled");
    }
    qInfo("box model: one persistent decode context per character (truebox)");

    // DataLocationMgr resolves file paths against the per-target writable root
    // SEQ_DATA_NAMESPACE (user) and PKGDATADIR (install prefix). The namespace
    // is compiled in: Live uses ".scry" FLAT so filters/, maps/, spawnpoints/
    // interop with the Elixir scry daemon (same namespace scheme); Test/EQL
    // nest under ".scry/<target>" so their data can't collide with Live's.
    // Daemon-only writes (prefs, per-daemon state) go under <root>/daemon/. When
    // --config-dir is passed, it substitutes for the read-only PKGDATADIR slot
    // (build-tree conf/ standing in for the install path) — the user dir stays
    // at the namespace root so writes still land in a writable location.
    const QString dataNamespace = QStringLiteral(SEQ_DATA_NAMESPACE);
    if (!m_cfg.configDir.isEmpty()) {
        m_dataLocationMgr =
            std::make_unique<DataLocationMgr>(dataNamespace, m_cfg.configDir);
        qInfo("config dir: %s (overrides PKGDATADIR)",
              qUtf8Printable(m_cfg.configDir));
    } else {
        m_dataLocationMgr = std::make_unique<DataLocationMgr>(dataNamespace);
    }
    qInfo("data namespace: ~/%s", SEQ_DATA_NAMESPACE);
    m_dataLocationMgr->setupUserDirectory();

    const QFileInfo defPref =
        m_dataLocationMgr->findExistingFile(".", "seqdef.toml", true, false);
    const QFileInfo userPref =
        m_dataLocationMgr->findWriteFile("daemon", "scryd.toml", true, true);
    // One-shot adoption of pre-rename prefs (old root and/or old filename).
    // findExistingFile also searches the legacy root, so this covers both.
    if (!userPref.exists()) {
        const QFileInfo oldPref =
            m_dataLocationMgr->findExistingFile("daemon", "showeq-daemon.toml", true, true);
        if (oldPref.exists() &&
            QFile::copy(oldPref.absoluteFilePath(), userPref.absoluteFilePath()))
            qInfo("preferences: adopted '%s' -> '%s'",
                  qUtf8Printable(oldPref.absoluteFilePath()),
                  qUtf8Printable(userPref.absoluteFilePath()));
    }
    seq::initGlobals(defPref.absoluteFilePath(), userPref.absoluteFilePath());

    // Cross-cutting helpers the extracted managers expect to find on the
    // QObject tree. Spells is optional — if spells_us.txt is missing the
    // daemon still runs, we just can't render spell names or compute
    // calc-from-level durations. Search cascade:
    //   1. DataLocationMgr (user dir / --config-dir / pkg dir)
    //   2. /usr/local/share/showeq/ (parallel showeq install — daemon
    //      doesn't ship its own copy of spells_us.txt)
    m_dateTimeMgr = new DateTimeMgr(this, "datetimemgr");
    m_zoneServerMgr = new ZoneServerMgr(this);
    QFileInfo spellsFile =
        m_dataLocationMgr->findExistingFile(".", "spells_us.txt");
    if (!spellsFile.exists()) {
        QFileInfo fi(QStringLiteral("/usr/local/share/showeq/spells_us.txt"));
        if (fi.exists()) spellsFile = fi;
    }
    if (spellsFile.exists()) {
        qInfo("loaded spells from %s", qUtf8Printable(spellsFile.absoluteFilePath()));
    } else {
        qInfo("no spells_us.txt found — spell names + durations will be empty");
    }
    m_spells    = new Spells(spellsFile.exists()
                              ? spellsFile.absoluteFilePath()
                              : QString());
    m_eqStrings = new EQStr();
    // EQ format-string table for OP_FormattedMessage / OP_SimpleMessage
    // payloads. Without it those handlers emit "Unknown: <id>: <args>"
    // because the format-id → template lookup returns nothing. Same
    // search cascade as spells_us.txt above.
    QFileInfo eqstrFile =
        m_dataLocationMgr->findExistingFile(".", "eqstr_us.txt");
    if (!eqstrFile.exists()) {
        QFileInfo fi(QStringLiteral("/usr/local/share/showeq/eqstr_us.txt"));
        if (fi.exists()) eqstrFile = fi;
    }
    if (eqstrFile.exists()) {
        m_eqStrings->load(eqstrFile.absoluteFilePath());
        qInfo("loaded eqstr from %s", qUtf8Printable(eqstrFile.absoluteFilePath()));
    } else {
        qInfo("no eqstr_us.txt found — formatted system messages will read \"Unknown: <id>\"");
    }

    // dbstr_us.txt — modern EQ's dynamic-content text table (faction names,
    // /time output, splash strings). Some OP_FormattedMessage format IDs have
    // no eqstr template and only resolve here; MessageShell::formattedMessage
    // uses it as a fallback. Same data-location cascade as eqstr above.
    // Optional — inert if the file is absent. (Ported from archive/test-client.)
    m_dbStrings = new DbStrings();
    QFileInfo dbstrFile = m_dataLocationMgr->findExistingFile(".", "dbstr_us.txt");
    if (!dbstrFile.exists()) {
        QFileInfo fi(QStringLiteral("/usr/local/share/showeq/dbstr_us.txt"));
        if (fi.exists()) dbstrFile = fi;
    }
    if (dbstrFile.exists())
        m_dbStrings->load(dbstrFile.absoluteFilePath());

    // spells_us_str.txt — per-spell message text (cast/effect/wear-off lines).
    // Loaded here so the plumbing is in place, but its consumers in
    // MessageShell::simpleMessage are gated behind a TODO pending Live wire
    // verification (the selectors + spell-id field were derived from Test —
    // see archive/test-client commit b403896). Inert until that is wired.
    m_spellMessages = new SpellMessages();
    QFileInfo spellStrFile = m_dataLocationMgr->findExistingFile(".", "spells_us_str.txt");
    if (!spellStrFile.exists()) {
        QFileInfo fi(QStringLiteral("/usr/local/share/showeq/spells_us_str.txt"));
        if (fi.exists()) spellStrFile = fi;
    }
    if (spellStrFile.exists())
        m_spellMessages->load(spellStrFile.absoluteFilePath());

    // EQPacket reads `[VPacket] Filename` from pSEQPrefs to decide where
    // to record/playback. Set it before constructing EQPacket so both
    // the recordPackets and playbackPackets paths can find their file.
    // Recording (write) and replay (read) are mutually exclusive at the
    // VPacket layer, so if both flags were passed we reject early.
    if (!m_cfg.recordVpk.isEmpty() && !m_cfg.replay.isEmpty()) {
        qCritical("--record-vpk and --replay are mutually exclusive");
        return false;
    }
    if (!m_cfg.recordVpk.isEmpty()) {
        pSEQPrefs->setPrefString("Filename", "VPacket", m_cfg.recordVpk);
    } else if (!m_cfg.replay.isEmpty()) {
        pSEQPrefs->setPrefString("Filename", "VPacket", m_cfg.replay);
    }

    // CLI --device wins; otherwise consult the XML pref so the value the
    // user saved through the preferences UI persists across restarts.
    // Replay sessions ignore the device entirely.
    if (m_cfg.device.isEmpty() && m_cfg.replay.isEmpty() && m_cfg.agent.isEmpty()) {
        const QString xmlDev =
            pSEQPrefs->getPrefString("Device", "Network", QString());
        if (!xmlDev.isEmpty()) {
            m_cfg.device = xmlDev;
            qInfo("device from prefs: %s", qUtf8Printable(xmlDev));
        }
    }

    // EQPacket's ctor calls pcap_create/pcap_activate, which exit(1)s when
    // there's no device available. Skip capture setup entirely when the
    // user passed neither --device nor --replay; the daemon then serves
    // clients with an empty state — useful for smoke tests and local dev.
    if (!m_cfg.device.isEmpty() || !m_cfg.replay.isEmpty() || !m_cfg.agent.isEmpty()) {
        if (!startCapture()) {
            return false;
        }
    }

    // Daemon-global managers shared into every per-box ManagerSet. These
    // are server-uniform / config / stateless, so all boxes share one
    // instance. Constructed before buildManagerSet() because the per-box
    // managers depend on them.
    const QFileInfo guildFile =
        m_dataLocationMgr->findWriteFile("tmp", "guilds2.dat");
    m_guildMgr = new GuildMgr(guildFile.absoluteFilePath(), this, "guildmgr");
    m_filterMgr = new FilterMgr(
        m_dataLocationMgr.get(),
        /*filterFile*/ "global.xml",
        /*caseSensitive*/ false);
    m_messageFilters = new MessageFilters(this, "messageFilters");
    m_messages = new Messages(m_dateTimeMgr, m_messageFilters,
                              this, "messages");

    // Per-box state managers. Multibox builds one ManagerSet per box so
    // each box decodes into its own game state; today a single active set
    // drives the decode pipeline. The m_* members track the ACTIVE set —
    // they're what loadZoneMap(), wireZoneMgr()/wireSpawnShell(), and the
    // SessionAdapter wiring read.
    m_activeManagers = buildManagerSet();
    const ManagerSet& active = m_activeManagers;
    m_zoneMgr      = active.zoneMgr;
    m_player       = active.player;
    m_spawnShell   = active.spawnShell;
    m_spawnMonitor = active.spawnMonitor;
    m_groupMgr     = active.groupMgr;
    m_guildShell   = active.guildShell;
    m_messageShell = active.messageShell;
    m_spellShell   = active.spellShell;
    m_combatRouter = active.combatRouter;

    // EQ Legends UCS cross-zone chat: EQPacket intercepts the port-9877 chat
    // session and hands each raw payload to the CURRENTLY-ACTIVE box's
    // MessageShell, which decodes it (Rust) and re-emits chatMessage ->
    // SessionAdapter -> web. UCS is a single global session but SessionAdapter
    // follows the active box across zones (a client that zones spawns a new
    // box), so we must resolve the active MessageShell per payload rather than
    // pin the initial one — otherwise chat is lost after the first zone.
    // No-op on live/test (the Rust decoder is an empty stub there).
    if (m_packet) {
        connect(m_packet, &EQPacket::ucsChatData, this,
                [this](const uint8_t* d, size_t l, uint8_t dir,
                       in_addr_t client) {
            const ManagerSet* ns = managersForBox(QString());
            MessageShell* ms = (ns && ns->messageShell) ? ns->messageShell
                                                         : m_messageShell;
            if (ms)
                ms->ucsChatMessage(d, l, dir, static_cast<uint32_t>(client));
        });
    }

    // Per-zone filter overlay for an already-known zone (e.g. replay mode
    // with the zone fixed). Needs the active ZoneMgr, so it runs after
    // buildManagerSet(). The signal it would emit has no listener yet.
    const QString shortZoneName = m_zoneMgr->shortZoneName();
    if (!shortZoneName.isEmpty()) {
        m_filterMgr->loadZone(shortZoneName);
    }

    // CategoryMgr loads user-defined Category groupings from the
    // pSEQPrefs preferences (section "CategoryMgr"). seqdef.toml ships
    // with a default set so the list is never empty.
    m_categoryMgr = new CategoryMgr(this, "categoryMgr");

    // Daemon-side itemId -> ItemTemplate cache. Persisted as JSON under
    // ~/.scry/daemon/itemcache.json so worn-gear stats survive across
    // daemon restarts (we don't see OP_ItemPacket for items the user
    // hasn't moved this session). Wiring of the OP_ItemPacket signal
    // happens in wireZoneMgr() once m_packet is alive.
    //
    // --replay sessions skip persistence entirely so regression goldens
    // aren't contaminated by the user's real cache and replay-captured
    // items don't pollute the on-disk cache.
    m_itemCache = new ItemCache(this);
    if (m_cfg.replay.isEmpty()) {
        const QFileInfo cacheFile = m_dataLocationMgr->findWriteFile(
            "daemon", "itemcache.json", true, true);
        m_itemCache->setStorePath(cacheFile.absoluteFilePath());
    } else {
        qInfo("ItemCache: replay mode, persistence disabled");
    }

    // Loot history. Same replay rule as the item cache, and for a sharper
    // reason: tests/replay/check.sh runs on every pre-push, so without this a
    // regression run would append fixture loot to the user's real DB. The
    // tracker still runs under replay — it just has nowhere to write.
    m_lootStore = std::make_unique<LootStore>();
    {
        const QFileInfo lootFile = m_dataLocationMgr->findWriteFile(
            ".", "loot.db", true, true);
        const bool replaying = !m_cfg.replay.isEmpty();
        m_lootStore->setStorePath(lootFile.absoluteFilePath(), replaying);
        if (replaying)
            qInfo("LootStore: replay mode — read-only, recording disabled");
    }

    // PrefsBroker is the curated TomlPreferences <-> wire bridge. Constructed
    // after pSEQPrefs is initialized but before any client can connect, so
    // the very first PrefsSnapshot reflects the on-disk state.
    m_prefsBroker = new PrefsBroker(this);
    // The broker triggers EQPacket::monitorDevice / monitorIPClient on
    // Network/* edits so changes apply mid-session (the user has to
    // zone for the new session-key handshake — same as showeq).
    // Null in --no-device + --no-replay smoke-test mode; the broker
    // handles that and just persists to XML.
    m_prefsBroker->setPacket(m_packet);

    // Resolve the active map package: CLI --map-package wins, else the
    // persisted [Maps] Package pref, else "default".
    if (!m_cfg.mapPackage.isEmpty()) {
        m_mapPackage = m_cfg.mapPackage;
    } else {
        m_mapPackage = pSEQPrefs->getPrefString("Package", "Maps",
                                                QStringLiteral("default"));
    }

    // Load the initial zone map if we already know the zone (e.g. replay
    // mode with zone already fixed). Otherwise loadZoneMap fires on the
    // first zone-resolving signal.
    if (!shortZoneName.isEmpty()) {
        loadZoneMap(shortZoneName);
    }
    // Camp+login takes the `zonePlayer -> emit zoneBegin` path; inter-zone
    // transitions take the `zoneChange(DIR_Server) -> emit zoneChanged`
    // path. Listen to both — loadZoneMap is idempotent if the zone hasn't
    // changed because clear()+reload yields the same MapData.
    connect(m_zoneMgr, SIGNAL(zoneBegin(const QString&)),
            this,      SLOT(loadZoneMap(const QString&)));
    connect(m_zoneMgr, SIGNAL(zoneChanged(const QString&)),
            this,      SLOT(loadZoneMap(const QString&)));
    // eql delivers the zone name late (OP_NewZone, after the spawn bulk) via
    // zoneResolved — load the map on it too, but note zoneResolved does NOT
    // clear spawns like zoneBegin/zoneChanged. Never emitted on live/test.
    connect(m_zoneMgr, SIGNAL(zoneResolved(const QString&)),
            this,      SLOT(loadZoneMap(const QString&)));

    // Same dual-signal wiring for the per-zone filter overlay. Without
    // this, FilterMgr::loadZone only fires once at startup and the
    // overlay file for the new zone is never re-read on transitions.
    connect(m_zoneMgr,   SIGNAL(zoneBegin(const QString&)),
            m_filterMgr, SLOT(loadZone(const QString&)));
    connect(m_zoneMgr,   SIGNAL(zoneChanged(const QString&)),
            m_filterMgr, SLOT(loadZone(const QString&)));
    connect(m_zoneMgr,   SIGNAL(zoneResolved(const QString&)),
            m_filterMgr, SLOT(loadZone(const QString&)));

    // Let the WebSocket server hand these to each SessionAdapter it spawns.
    m_ws->setState(m_spawnShell, m_zoneMgr, m_player, m_mapData.get(),
                   m_messageShell, m_groupMgr, m_guildShell, m_spellShell,
                   m_combatRouter, m_categoryMgr, m_filterMgr,
                   m_prefsBroker, m_spawnMonitor, m_itemCache,
                   m_dateTimeMgr, m_zoneServerMgr,
                   m_packet ? &m_packet->boxRegistry() : nullptr);
    m_ws->setMapPackageHost(this);
    m_ws->setManagerProvider(this);
    m_ws->setLootStore(m_lootStore.get());

    // --record-golden: spin up an internal SessionAdapter writing into a
    // FileSink. Subscribe is synthesized immediately so the on-disk
    // stream begins with a Snapshot, matching what a freshly-connected
    // real client would receive.
    if (!m_cfg.recordGolden.isEmpty()) {
        m_goldenSink = std::make_unique<FileSink>(m_cfg.recordGolden);
        if (!m_goldenSink->isOpen()) {
            return false;
        }
        m_goldenAdapter = new SessionAdapter(m_goldenSink.get(),
                                             m_spawnShell, m_zoneMgr, m_player,
                                             m_mapData.get(), m_messageShell,
                                             m_groupMgr, m_guildShell,
                                             m_spellShell,
                                             m_combatRouter, m_categoryMgr,
                                             m_filterMgr, m_prefsBroker,
                                             m_spawnMonitor, m_itemCache,
                                             m_dateTimeMgr, m_zoneServerMgr,
                                             m_packet ? &m_packet->boxRegistry()
                                                      : nullptr,
                                             this);
        // The golden adapter writes the regression-harness .pbstream;
        // strip wall-clock fields so the tier-2 byte-cmp is stable
        // across runs.
        m_goldenAdapter->setDeterministic(true);
        m_goldenAdapter->setMapPackageHost(this);
        m_goldenAdapter->setManagerProvider(this);
        seq::v1::ClientEnvelope subEnv;
        subEnv.mutable_subscribe();
        QByteArray subBytes;
        subBytes.resize(static_cast<int>(subEnv.ByteSizeLong()));
        subEnv.SerializeToArray(subBytes.data(), subBytes.size());
        m_goldenAdapter->handleClientBinary(subBytes);
        qInfo("recording envelope golden to %s",
              qUtf8Printable(m_cfg.recordGolden));
    }

    if (m_packet) {
        // Tap decoded packets BEFORE the regular wiring so the logger
        // sees every dispatch (it doesn't matter for correctness — the
        // signal is broadcast — but keeping it adjacent to where the
        // packet pipeline starts makes the order obvious).
        if (!m_cfg.opcodeStats.isEmpty()) {
            m_opcodeStats = new OpcodeStatsLogger(m_packet, m_cfg.opcodeStats,
                                                  m_cfg.dumpAllSessions, this);
        }

        if (!m_cfg.listEvents.isEmpty()) {
            m_eventLogger = new EventLogger(m_packet, m_cfg.listEvents, this);
        }

        // --only-session: recon follows exactly one box. Index 1 / "first"
        // is the primary box — already the default recon source, so leave
        // its taps alone. Any other selector detaches the primary taps here;
        // onBoxCreated relays the matching session when it's identified.
        if (!m_cfg.onlySession.isEmpty()) {
            if (m_cfg.dumpAllSessions)
                qWarning("--only-session overrides --dump-all-sessions");
            const int ord = onlySessionOrdinal();
            if (ord != 1)
                m_packet->disconnectReconTaps();
            if (ord > 0) {
                qInfo("recon: --only-session tracking session #%d", ord);
            } else {
                qInfo("recon: --only-session tracking character '%s' "
                      "(relays once the name resolves)",
                      qUtf8Printable(m_cfg.onlySession));
                // Sweep on every registry change so a box named by ANY path
                // (NamePromoter at OP_EnterWorld — the earliest — or the
                // profile / own-spawn promotions) starts relaying as soon as
                // its display_name matches. Idempotent per box.
                connect(&m_packet->boxRegistry(), &BoxRegistry::changed,
                        this, [this]() {
                    m_packet->boxRegistry().forEach([this](Box& b) {
                        onlySessionNameCheck(&b, b.display_name);
                    });
                });
            }
        }

        if (m_cfg.listBoxes) {
            // Stage 1 of multibox-sessions: stderr-dump the registry
            // every 5s so the user can verify two-client captures
            // surface both boxes. Final dump on aboutToQuit covers
            // the --replay EOF case (process exits before the next
            // timer tick).
            auto* boxTimer = new QTimer(this);
            connect(boxTimer, &QTimer::timeout, this, [this]() {
                qInfo().noquote() << m_packet->boxRegistry().dumpString();
            });
            boxTimer->start(5000);
            connect(QCoreApplication::instance(),
                    &QCoreApplication::aboutToQuit, this, [this]() {
                qInfo().noquote() << m_packet->boxRegistry().dumpString();
            });
        }

        // Periodically reclaim boxes whose EQ session has gone idle past the
        // TTL (default 10 min; --box-idle-ttl SECONDS, 0 disables). Each zone
        // change opens a fresh world socket, so a long multibox session piles
        // up one Box (with its own ManagerSet + streams) per character per
        // zone; this sweeps the superseded ones. Skipped for --replay: its
        // wall-clock last_seen stays fresh across a short playback and we
        // don't want eviction perturbing deterministic goldens.
        if (m_cfg.replay.isEmpty() && m_cfg.boxIdleTtlMs > 0) {
            // Sweep often enough to act promptly on a short TTL, but no more
            // than once a minute for the common long TTL.
            const qint64 ttl = m_cfg.boxIdleTtlMs;
            const int interval = int(ttl < 5000 ? 5000 : (ttl > 60000 ? 60000
                                                                       : ttl));
            auto* sweep = new QTimer(this);
            connect(sweep, &QTimer::timeout, this, [this]() {
                const int n = m_packet->boxRegistry().evictStale(
                    QDateTime::currentMSecsSinceEpoch(), m_cfg.boxIdleTtlMs);
                if (n > 0)
                    qInfo("BoxRegistry: evicted %d idle box session(s)", n);
            });
            sweep->start(interval);
        }

        for (const QString& spec : m_cfg.dumpPayload) {
            const int colon = spec.indexOf(':');
            if (colon <= 0) {
                qWarning("--dump-payload: malformed %s, expected OPCODE:PATH",
                         qUtf8Printable(spec));
                continue;
            }
            bool ok = false;
            const uint16_t op = static_cast<uint16_t>(
                spec.left(colon).toUInt(&ok, 0));
            if (!ok) {
                qWarning("--dump-payload: bad opcode %s",
                         qUtf8Printable(spec.left(colon)));
                continue;
            }
            m_payloadDumpers.append(
                new OpcodePayloadDumper(m_packet, op, spec.mid(colon + 1), this));
        }

        // Wire the active ManagerSet onto the four global decode streams
        // (the primary box aliases these). This is the single-box decode
        // path; non-primary boxes are wired per-box in onBoxCreated().
        wireBoxPipeline(m_packet->worldClientStream(),
                        m_packet->worldServerStream(),
                        m_packet->zoneClientStream(),
                        m_packet->zoneServerStream(),
                        m_activeManagers, /*wireGlobalSinks=*/true);

        // Build + wire a per-box ManagerSet whenever a new box appears.
        // Fires synchronously from BoxRegistry::observe (after the box's
        // streams are allocated), so a box is fully wired before the
        // packet that created it is routed to its streams.
        connect(&m_packet->boxRegistry(), &BoxRegistry::boxCreated,
                this, [this](Box* box) { onBoxCreated(box); });

        // Reclaim a box's ManagerSet when the registry evicts its idle
        // session. EQPacket tears down the matching streams/observers on
        // the same signal; order between the two slots doesn't matter
        // since each only frees objects it owns (via deleteLater).
        connect(&m_packet->boxRegistry(), &BoxRegistry::boxAboutToBeRemoved,
                this, [this](Box* box) { onBoxAboutToBeRemoved(box); });

        // On an active-box switch the newly-active box is already sitting in
        // its zone, so no zoneChanged fires and the shared MapData still holds
        // the PREVIOUS box's geometry — the re-snapshot would re-ship the old
        // map. Reload MapData for the new box's current zone here, resolving
        // the same managers SessionAdapter will. Connected at startup, this
        // runs before any per-client SessionAdapter Subscribe (those attach
        // when a ws client connects, strictly later), so sendSnapshot reads
        // fresh geometry.
        connect(&m_packet->boxRegistry(), &BoxRegistry::activeCharacterChanged,
                this, [this](Box* old, Box* target) {
            // Only a genuine switch needs this. The first box becoming active
            // (old == nullptr, e.g. adopt-first-character) loads its map via
            // the normal zoneChanged path; reloading here would just re-clear
            // MapData mid-replay and flip goldens.
            if (!old || !target) return;
            const ManagerSet* ns = managersForBox(target->box_id);
            if (ns && ns->zoneMgr)
                loadZoneMap(ns->zoneMgr->shortZoneName());
        });

        // Replay normally quits at EOF (golden generation /
        // opcode-stats / --no-listen one-shots all want this). With
        // --wait-for-client, however, we're driving the web UI from a
        // recorded capture — playback must hold until a real
        // SessionAdapter is wired (otherwise the early-replay
        // envelopes hit a deferred adapter and get dropped) and the
        // daemon must stay running after EOF so the user can poke
        // at the final state.
        const bool isReplay = !m_cfg.replay.isEmpty();
        if (isReplay && !m_cfg.waitForClient) {
            connect(m_packet, &EQPacket::playbackFinished, this, [] {
                QTimer::singleShot(0, &QCoreApplication::quit);
            });
        }

        if (isReplay && m_cfg.waitForClient) {
            // Defer start until the first WsServer client subscribes.
            connect(m_ws.get(), &WsServer::firstClientSubscribed,
                    this, [this] {
                qInfo("--wait-for-client: client connected, starting replay");
                m_packet->start(10);
            });
            qInfo("--wait-for-client: replay paused, waiting for ws client");
        } else {
            m_packet->start(10);
            qInfo("capture pipeline running");
        }
    } else {
        qInfo("no --device, --agent or --replay — capture pipeline idle");
    }
    return true;
}

bool DaemonApp::startServer()
{
    if (!m_ws->listen(m_cfg.listenHost, m_cfg.listenPort)) {
        qCritical("failed to listen on %s:%u",
                  qUtf8Printable(m_cfg.listenHost.toString()),
                  m_cfg.listenPort);
        return false;
    }
    qInfo("scryd listening on %s:%u",
          qUtf8Printable(m_cfg.listenHost.toString()),
          m_cfg.listenPort);
    return true;
}

bool DaemonApp::startCapture()
{
    // Opcode tables are per-target: conf/<target>/opcodes.toml, selected by the
    // compiled SEQ_OPCODE_SUBDIR ("live"/"test"/"eql"). The rest of
    // --config-dir (seqdef.toml, maps/, etc.) stays shared at the root. XML is
    // now settings-only; the opcode table is read straight from the TOML that
    // was always its canonical source.
    const QString opcodeSubdir = QStringLiteral(SEQ_OPCODE_SUBDIR);
    // preferUser=false: the opcode table is shipped config (conf/), NOT user
    // data. The user data dir (~/.scry) is SHARED, and its flat table may be
    // swapped per target (e.g. EQL opcodes for legacy headless .vpk
    // playback). Preferring user-data there made the LIVE daemon
    // (SEQ_OPCODE_SUBDIR=".") load ~/.scry's EQL table for a live capture →
    // total mis-decode / SessionDisconnect. conf/ wins; ~/.scry stays a
    // fallback only when conf/ lacks the file.
    const QFileInfo opcodesToml =
        m_dataLocationMgr->findExistingFile(opcodeSubdir, "opcodes.toml", false, false);
    if (!opcodesToml.exists()) {
        qCritical("missing opcode table (%s/opcodes.toml) "
                  "— check that conf/ is installed to PKGDATADIR",
                  qUtf8Printable(opcodeSubdir));
        return false;
    }

    const bool hasReplay = !m_cfg.replay.isEmpty();
    const bool wantRecord = !m_cfg.recordVpk.isEmpty();
    // CLI --ip wins, then XML pref, then sentinel. Empty / sentinel
    // string == auto-detect next session, same semantics as showeq.
    QString clientIp = m_cfg.ip;
    if (clientIp.isEmpty()) {
        clientIp = pSEQPrefs->getPrefString("IP", "Network", AUTOMATIC_CLIENT_IP);
    }
    if (clientIp.isEmpty()) clientIp = AUTOMATIC_CLIENT_IP;
    m_packet = new EQPacket(
        opcodesToml.absoluteFilePath(),
        /*arqSeqGiveUp*/ 512,
        /*device*/ hasReplay ? QString() : m_cfg.device,
        /*agent*/ hasReplay ? QString() : m_cfg.agent,
        /*ip*/ clientIp,
        /*mac*/ QStringLiteral("0"),
        /*realtime*/ false,
        /*snaplen*/ 2,
        /*buffersize*/ 4,
        /*sessionTracking*/ false,
        /*recordPackets*/ wantRecord,
        /*playbackPackets*/ hasReplay
            ? (m_cfg.replayIsPcap ? PLAYBACK_FORMAT_TCPDUMP : PLAYBACK_FORMAT_SEQ)
            : PLAYBACK_OFF,
        /*playbackSpeed*/ 0,
        this, "packet");
    if (m_cfg.strictGateSizes && m_packet->undeclaredGateSizeCount() > 0) {
        qCritical("--strict-gate-sizes: %d mapped SZC_Match opcode(s) gate on an "
                  "inherited Live sizeof — declare them in seq-backend-eql "
                  "size_overrides() (see BACKEND GATE-SIZE warnings above)",
                  m_packet->undeclaredGateSizeCount());
        return false;
    }
    if (wantRecord) {
        qInfo("recording raw packets to %s", qUtf8Printable(m_cfg.recordVpk));
    }
    return true;
}

ManagerSet DaemonApp::buildManagerSet()
{
    // Constructs one set of per-box state managers in the SAME order (and
    // with the same cross-manager connect()s) the daemon has always used,
    // so single-box decode output stays byte-identical. The daemon-global
    // managers (m_guildMgr, m_filterMgr, m_messages, m_messageFilters,
    // m_spells, m_eqStrings, m_dateTimeMgr, m_dataLocationMgr) must already
    // exist — they're shared into every set.
    ManagerSet ms;

    ms.zoneMgr = new ZoneMgr(this, "zonemgr");
    ms.player  = new Player(this, ms.zoneMgr, m_guildMgr);
    ms.spawnShell =
        new SpawnShell(*m_filterMgr, ms.zoneMgr, ms.player, m_guildMgr);

    // SpawnMonitor learns recurring NPC pop locations + respawn timers
    // from observed spawn/kill cycles (showeq interface.cpp:326). It
    // connects its own slots to the SpawnShell + ZoneMgr in its ctor.
    ms.spawnMonitor = new SpawnMonitor(m_dataLocationMgr.get(),
                                       ms.zoneMgr, ms.spawnShell,
                                       this, "spawnMonitor");
    // --replay: don't load/save ~/.scry/spawnpoints/*.sp. Those files are
    // written in QHash order and reloaded on zone, so persisting during a
    // replay makes spawn-point emission order (and tier-2 goldens)
    // non-deterministic run-to-run. Mirrors the ItemCache handling above.
    if (!m_cfg.replay.isEmpty()) {
        ms.spawnMonitor->setPersist(false);
        qInfo("SpawnMonitor: replay mode, spawn-point persistence disabled");
    }
    // Persist on shutdown. saveSpawnPoints is a no-op unless modified, so
    // this is cheap; aboutToQuit fires from the event loop after quit().
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
            ms.spawnMonitor, &SpawnMonitor::saveSpawnPoints);

    // GroupMgr tracks group members. Wiring matches showeq/src/
    // interface.cpp:593-615 — player profile signal, group opcode
    // handlers, and the spawn lifecycle slots.
    ms.groupMgr = new GroupMgr(ms.spawnShell, ms.player, this, "groupMgr");
    connect(ms.zoneMgr,    SIGNAL(playerProfile(const charProfileStruct*)),
            ms.groupMgr,   SLOT(player(const charProfileStruct*)));
    connect(ms.spawnShell, SIGNAL(addItem(const Item*)),
            ms.groupMgr,   SLOT(addItem(const Item*)));
    connect(ms.spawnShell, SIGNAL(delItem(const Item*)),
            ms.groupMgr,   SLOT(delItem(const Item*)));
    connect(ms.spawnShell, SIGNAL(killSpawn(const Item*, const Item*, uint16_t)),
            ms.groupMgr,   SLOT(killSpawn(const Item*)));
    // SpawnShell::clear() (zone change) bulk-frees spawns and emits only
    // clearItems() — without this the GroupMgr m_spawn pointers dangle past
    // the zone and fillGroupUpdate dereferences freed Spawns (UAF crash).
    connect(ms.spawnShell, SIGNAL(clearItems()),
            ms.groupMgr,   SLOT(clear()));

    // GuildShell holds THIS character's guild roster (the daemon-global
    // GuildMgr only maps guild ids to names). Populated by the backend that
    // decodes a roster opcode; inert where none is wired.
    ms.guildShell = new GuildShell(ms.zoneMgr, this, "guildShell");

    // MessageShell parses chat / system / NPC text into structured
    // signals. Needs the global MessageFilters + Messages.
    ms.messageShell = new MessageShell(m_messages, m_eqStrings, m_spells,
                                       m_spellMessages, m_dbStrings,
                                       ms.zoneMgr, ms.spawnShell, ms.player,
                                       this, "messageShell");
    ms.messageShell->setLootStore(m_lootStore.get());

    // SpellShell tracks active buffs / outgoing casts. Wires player
    // signals + clear-on-zone, mirroring showeq interface.cpp:967-988.
    ms.spellShell = new SpellShell(ms.player, ms.spawnShell, m_spells);
    ms.spellShell->setParent(this);
    connect(ms.player, SIGNAL(newPlayer()),
            ms.spellShell, SLOT(clear()));
    connect(ms.player, SIGNAL(buffLoad(const spellBuff*)),
            ms.spellShell, SLOT(buffLoad(const spellBuff*)));
    connect(ms.zoneMgr, SIGNAL(zoneChanged(const QString&)),
            ms.spellShell, SLOT(zoneChanged()));
    connect(ms.spawnShell, SIGNAL(killSpawn(const Item*, const Item*, uint16_t)),
            ms.spellShell, SLOT(killSpawn(const Item*)));
    // Prune the player's mob effects when a mob despawns (out-of-range /
    // OP_DeleteSpawn) — killSpawn only covers deaths, which leave a corpse.
    connect(ms.spawnShell, SIGNAL(delItem(const Item*)),
            ms.spellShell, SLOT(delSpawn(const Item*)));

    // CombatRouter parses OP_Action2 into structured combat events.
    ms.combatRouter = new CombatRouter(ms.spawnShell, m_spells, this);

    return ms;
}

void DaemonApp::onBoxCreated(Box* box)
{
    if (!box) return;
    const int ordinal = ++m_boxOrdinal;   // 1-based discovery order
    if (box->is_primary) {
        // The primary box's four streams ARE the global streams, already
        // wired to the active ManagerSet in start(). Just record the
        // mapping so SessionAdapter can resolve it.
        m_boxManagers.insert(box, m_activeManagers);
    } else {
        // The ONLY non-primary path (B2: model-A per-box sets removed). Truebox /
        // single character: every zone opens a fresh world socket → a new Box,
        // but feed them ALL into the ONE persistent m_activeManagers instead of
        // building a per-box set. Because every Box resolves to the same
        // ManagerSet, a zone-in needs no manager re-bind at all — the web client
        // stays wired to the shared managers, so a zoned-in map never blanks
        // until refresh. Clear the persistent state for the new zone but
        // keep the Player identity; the self-id re-adopts from the new zone's own
        // OP_ZoneEntry (EqlDispatch::consumeSelfSpawn). Return early: the primary
        // box already wired the shared promote/map hooks below onto this same
        // ManagerSet, so per-box duplicates would only re-fire them.
        m_boxManagers.insert(box, m_activeManagers);
        wireBoxPipeline(box->world_c2s, box->world_s2c,
                        box->zone_c2s, box->zone_s2c, m_activeManagers,
                        /*wireGlobalSinks=*/false);
        if (m_activeManagers.spawnShell)
            m_activeManagers.spawnShell->clear();
        if (m_activeManagers.player)
            m_activeManagers.player->setID(0);
        if (m_cfg.dumpAllSessions && m_cfg.onlySession.isEmpty())
            relayReconTaps(box);
        return;
    }

    // --only-session, index selector: relay the Nth session in discovery
    // order (index 1 = the primary box, whose default taps were left intact
    // in start(), so relaying it again isn't needed).
    if (!m_cfg.onlySession.isEmpty()) {
        const int ord = onlySessionOrdinal();
        if (ord > 1 && ord == ordinal)
            relayReconTaps(box);
    }

    // Promote + merge the box by its CHARACTER name on every OP_PlayerProfile
    // (i.e. each zone-in). Read the AUTHORITATIVE name straight off
    // charProfileStruct.name — Player::name() returns the "You" default at
    // this point (its auto-detect flags haven't settled), which would
    // collapse every box into one bogus character. Re-handshakes of the same
    // character merge into one picker entry; promoteByName rolls the
    // character's current decode box to this newest zone session.
    if (ZoneMgr* zm = m_boxManagers[box].zoneMgr) {
        connect(zm, &ZoneMgr::playerProfile, this,
                [this, box](const charProfileStruct* p) {
            if (!p) return;
            const QString name =
                QString::fromLatin1(p->name,
                                    int(qstrnlen(p->name, sizeof(p->name))));
            m_packet->boxRegistry().promoteByName(box, name);
            onlySessionNameCheck(box, name);
        });
    }

    // eql equivalent of the Live block above: the authoritative character name
    // straight off OP_PlayerProfile (Player::setPlayerName -> identityName-
    // Resolved). eql doesn't emit ZoneMgr::playerProfile — its profile is
    // decoded in EqlDispatch, not fillProfileStruct — so the box is named here
    // instead, and promoted unconditionally just like Live.
    if (Player* pl = m_boxManagers[box].player) {
        connect(pl, &Player::identityNameResolved, this,
                [this, box](const QString& name) {
            m_packet->boxRegistry().promoteByName(box, name);
            onlySessionNameCheck(box, name);
        });
    }

    // Fallback name source on eql: the player's own-spawn adoption (SpawnShell::
    // playerChangedID). Used when the profile name is unavailable (offset
    // drifted, or the own-spawn resolves before OP_PlayerProfile decodes);
    // OP_EnterWorld/NamePromoter is Live-shaped, so without this a box lacking a
    // profile name would stay "Unknown". Deferred to only when nothing
    // authoritative named the box first (display_name still empty), since a
    // reused spawn id could in theory adopt a wrong name on live. Also feeds the
    // --only-session name match.
    if (SpawnShell* ss = m_boxManagers[box].spawnShell) {
        connect(ss, &SpawnShell::playerNameResolved, this,
                [this, box](const QString& name) {
            if (box->display_name.isEmpty())
                m_packet->boxRegistry().promoteByName(box, name);
            onlySessionNameCheck(box, name);
        });
    }

    // Keep the shared MapData in sync when the ACTIVE non-primary box zones.
    // The primary's zoneMgr is wired straight to loadZoneMap in start(); a
    // non-primary box that's promoted at login shows its name before its
    // OP_NewZone decodes, so a switch to it loads an empty map and the later
    // zoneChanged (which makes SessionAdapter re-send geometry) would still
    // read a stale MapData. Reload here so that late zone refreshes the map
    // with no second manual swap. Guarded on active so a background box zoning
    // doesn't clobber the active box's map.
    if (!box->is_primary) {
        if (ZoneMgr* zm = m_boxManagers[box].zoneMgr) {
            auto reloadIfActive = [this, box](const QString& zone) {
                BoxRegistry& reg = m_packet->boxRegistry();
                if (reg.currentBoxFor(reg.activeCharacterId()) == box)
                    loadZoneMap(zone);
            };
            connect(zm, qOverload<const QString&>(&ZoneMgr::zoneChanged), this,
                    reloadIfActive);
            // eql resolves the zone late via zoneResolved (OP_NewZone), not
            // zoneChanged — reload the active box's map on it too.
            connect(zm, &ZoneMgr::zoneResolved, this, reloadIfActive);
        }
    }
}

void DaemonApp::onBoxAboutToBeRemoved(Box* box)
{
    if (!box || box->is_primary) return;   // primary reuses m_activeManagers
    // Drop the resolver record first so any sendBoxList re-emit triggered
    // by this eviction can't hand SessionAdapter a set that's being torn
    // down, then deleteLater the whole per-box manager subtree.
    m_boxManagers.remove(box);
    if (QObject* root = m_boxManagerRoots.take(box))
        root->deleteLater();
}

int DaemonApp::onlySessionOrdinal() const
{
    if (m_cfg.onlySession.compare(QLatin1String("first"),
                                  Qt::CaseInsensitive) == 0)
        return 1;
    bool ok = false;
    const int n = m_cfg.onlySession.toInt(&ok);
    return (ok && n > 0) ? n : 0;   // 0 = name selector
}

void DaemonApp::relayReconTaps(Box* box)
{
    if (!box || m_reconRelayed.contains(box)) return;
    m_reconRelayed.insert(box);

    // The primary box aliases EQPacket's global streams (its Box::* stream
    // fields stay null); every other box owns its streams.
    EQPacketStream* zone[2]  = { box->zone_s2c,  box->zone_c2s };
    EQPacketStream* world[2] = { box->world_s2c, box->world_c2s };
    if (box->is_primary) {
        zone[0]  = m_packet->zoneServerStream();
        zone[1]  = m_packet->zoneClientStream();
        world[0] = m_packet->worldServerStream();
        world[1] = m_packet->worldClientStream();
    }
    for (EQPacketStream* s : zone)
        connect(s, SIGNAL(decodedPacket(const uint8_t*, size_t, uint8_t,
                                        uint16_t, const EQPacketOPCode*)),
                m_packet, SIGNAL(decodedZonePacket(const uint8_t*, size_t,
                                        uint8_t, uint16_t, const EQPacketOPCode*)));
    for (EQPacketStream* s : world)
        connect(s, SIGNAL(decodedPacket(const uint8_t*, size_t, uint8_t,
                                        uint16_t, const EQPacketOPCode*)),
                m_packet, SIGNAL(decodedWorldPacket(const uint8_t*, size_t,
                                        uint8_t, uint16_t, const EQPacketOPCode*)));
    qInfo("recon: relaying session %s%s%s",
          qUtf8Printable(box->box_id),
          box->display_name.isEmpty() ? "" : " / ",
          qUtf8Printable(box->display_name));
}

void DaemonApp::onlySessionNameCheck(Box* box, const QString& name)
{
    if (m_cfg.onlySession.isEmpty() || onlySessionOrdinal() != 0) return;
    if (QString::compare(name, m_cfg.onlySession, Qt::CaseInsensitive) == 0)
        relayReconTaps(box);
}

const ManagerSet* DaemonApp::managersForBox(const QString& boxId) const
{
    if (!m_packet) {
        return m_activeManagers.spawnShell ? &m_activeManagers : nullptr;
    }
    BoxRegistry& reg = m_packet->boxRegistry();
    // Resolve to the character's CURRENT (latest) decode box, so switching
    // to a character shows the zone it's in now, not a stale earlier one.
    const QString id = boxId.isEmpty() ? reg.activeCharacterId() : boxId;
    const Box* b = id.isEmpty() ? reg.primary() : reg.currentBoxFor(id);
    if (!b) return nullptr;
    const auto it = m_boxManagers.find(b);
    return it != m_boxManagers.end() ? &it.value() : nullptr;
}

static QStringList mapSearchPaths(const QString& override,
                                  const DataLocationMgr* dlm)
{
    // Override wins; otherwise the DataLocationMgr cascade (user → pkg).
    // The user dir is ~/.scry/maps; DataLocationMgr already falls back to the
    // pre-rename root, so no separate handling here.
    QStringList paths;
    if (!override.isEmpty()) {
        paths.append(override);
        return paths;
    }
    if (dlm) {
        paths.append(dlm->userDataDir("maps").absolutePath());
        paths.append(dlm->pkgDataDir("maps").absolutePath());
    }
    return paths;
}

static QFileInfo locateMap(const QStringList& dirs, const QString& filename)
{
    for (const QString& d : dirs) {
        QFileInfo fi(d + "/" + filename);
        if (fi.exists()) return fi;
    }
    return QFileInfo();
}

void DaemonApp::loadZoneMap(const QString& shortZoneName)
{
    m_mapData->clear();
    if (shortZoneName.isEmpty()) {
        return;
    }

    const QStringList roots = mapSearchPaths(m_cfg.mapsDir,
                                             m_dataLocationMgr.get());

    // Build the effective search path. When an active package is set
    // (!= "default"), look in <root>/<package>/ FIRST across all roots,
    // then fall back to the flat roots (the synthetic "default" package)
    // so a package that lacks the current zone still renders from the
    // shared maps root. Numbered-layer loading then operates within
    // whichever directory provided the base file (locateMap order).
    QStringList dirs;
    const bool usePackage =
        !m_mapPackage.isEmpty() && m_mapPackage != QStringLiteral("default");
    if (usePackage) {
        for (const QString& root : roots)
            dirs.append(root + QLatin1Char('/') + m_mapPackage);
    }
    dirs.append(roots);

    // Mirrors showeq/src/map.cpp:370-423 — locate the base .map/.txt then
    // any numbered layer files (_1, _2, ...). import=true for layer files so
    // they accumulate into the same MapData rather than replacing it.
    //
    // EQL raid instances arrive named "<base>_solo" / "_multi" /
    // "_eqlraidgroup" etc. and ship no per-instance map, so fall back to the
    // base zone's map when the full instance name has none. The exact name is
    // tried first, so any zone that ships its own map still wins; the base is
    // whatever the server sent before the first '_' (so hateplane vs
    // hateplaneb is decided by the wire, not a hardcoded table).
    QString mapZoneName = shortZoneName;
    QFileInfo baseMap = locateMap(dirs, mapZoneName + ".map");
    QFileInfo baseTxt = locateMap(dirs, mapZoneName + ".txt");
    const int us = shortZoneName.indexOf(QLatin1Char('_'));
    if (!baseMap.exists() && !baseTxt.exists() && us > 0) {
        mapZoneName = shortZoneName.left(us);
        baseMap = locateMap(dirs, mapZoneName + ".map");
        baseTxt = locateMap(dirs, mapZoneName + ".txt");
        if (baseMap.exists() || baseTxt.exists())
            qInfo("zone '%s' has no map of its own; using base map '%s'",
                  qUtf8Printable(shortZoneName), qUtf8Printable(mapZoneName));
    }

    QString extension;
    QStringList files;
    if (baseMap.exists()) {
        extension = ".map";
        files.append(baseMap.absoluteFilePath());
    } else if (baseTxt.exists()) {
        extension = ".txt";
        files.append(baseTxt.absoluteFilePath());
    } else {
        qInfo("no map file found for zone '%s' (searched: %s)",
              qUtf8Printable(shortZoneName),
              qUtf8Printable(dirs.join(", ")));
        return;
    }

    for (int i = 1; i < 10; ++i) {
        const QFileInfo layerFile =
            locateMap(dirs, mapZoneName + "_" + QString::number(i) + extension);
        if (layerFile.exists()) {
            files.append(layerFile.absoluteFilePath());
        }
    }

    bool import = false;
    for (const QString& f : files) {
        if (extension == ".map") {
            m_mapData->loadMap(f, import);
        } else {
            m_mapData->loadSOEMap(f, import);
        }
        import = true;
    }
    qInfo("loaded map for zone '%s' (%lld layer(s) from %s)",
          qUtf8Printable(shortZoneName), (long long)files.size(),
          qUtf8Printable(QFileInfo(files.first()).absolutePath()));
}

// Count base zone files (.map/.txt) in `dir`, ignoring numbered layer
// files <zone>_N.{map,txt} — those are layers of an existing base map,
// not distinct zones. A zone with both a .map and a .txt counts once.
static uint32_t countZoneFiles(const QString& dir)
{
    QDir d(dir);
    const QStringList filters{QStringLiteral("*.map"), QStringLiteral("*.txt")};
    QSet<QString> bases;
    for (const QString& name : d.entryList(filters, QDir::Files)) {
        QString base = QFileInfo(name).completeBaseName();
        const int us = base.lastIndexOf(QLatin1Char('_'));
        if (us > 0 && us + 1 < base.size()) {
            bool numeric = false;
            base.mid(us + 1).toInt(&numeric);
            if (numeric)
                continue; // <zone>_N layer file
        }
        bases.insert(base);
    }
    return static_cast<uint32_t>(bases.size());
}

std::vector<MapPackageInfo>
DaemonApp::mapPackagesIn(const QStringList& roots) const
{
    std::vector<MapPackageInfo> out;

    // Synthetic "default" package == the flat root(s). Sum base zone files
    // sitting directly in the search roots.
    uint32_t defaultZones = 0;
    for (const QString& root : roots)
        defaultZones += countZoneFiles(root);
    out.push_back({QStringLiteral("default"), QStringLiteral("default"),
                   defaultZones});

    // Each immediate subdir holding at least one .map/.txt is a package.
    // First root wins on duplicate package names across roots.
    QSet<QString> seen;
    for (const QString& root : roots) {
        QDir d(root);
        for (const QString& sub :
             d.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (sub == QStringLiteral("default"))
                continue; // reserved id
            if (seen.contains(sub))
                continue;
            const uint32_t zones =
                countZoneFiles(root + QLatin1Char('/') + sub);
            if (zones == 0)
                continue;
            seen.insert(sub);
            out.push_back({sub, sub, zones});
        }
    }
    return out;
}

std::vector<MapPackageInfo> DaemonApp::mapPackages() const
{
    return mapPackagesIn(mapSearchPaths(m_cfg.mapsDir,
                                        m_dataLocationMgr.get()));
}

QString DaemonApp::setMapPackage(const QString& id)
{
    // Fall back to "default" when the requested package is unknown.
    QString resolved = QStringLiteral("default");
    if (!id.isEmpty() && id != QStringLiteral("default")) {
        for (const auto& p : mapPackages()) {
            if (p.id == id) { resolved = id; break; }
        }
    }
    m_mapPackage = resolved;

    // Persist (TomlPreferences, [Maps] Package). Mirrors how Network/Device
    // is read/written elsewhere via pSEQPrefs. TomlPreferences batches
    // modifications in memory, so flush with save() — same pattern as
    // PrefsBroker::apply — otherwise the choice is lost on restart (the
    // daemon hot-reloads via _exit(75), bypassing any aboutToQuit flush).
    if (pSEQPrefs) {
        pSEQPrefs->setPrefString("Package", "Maps", resolved);
        pSEQPrefs->save();
    }

    // Re-resolve the current zone's map within the (new) active package and
    // broadcast a fresh MapPackagesUpdate + ZoneChanged so every client
    // re-renders. No-op gracefully if no zone is known yet.
    const QString zone = m_zoneMgr ? m_zoneMgr->shortZoneName() : QString();
    if (!zone.isEmpty())
        loadZoneMap(zone);

    if (m_ws) {
        seq::v1::Envelope upd;
        seq::encode::fillMapPackages(upd.mutable_map_packages(), mapPackages(),
                                     m_mapPackage);
        m_ws->broadcast(upd);

        if (!zone.isEmpty()) {
            seq::v1::Envelope zc;
            auto* z = zc.mutable_zone_changed();
            z->set_zone_short(zone.toStdString());
            if (m_zoneMgr)
                z->set_zone_long(m_zoneMgr->longZoneName().toStdString());
            if (m_mapData)
                seq::encode::fillMapGeometry(z->mutable_geometry(), *m_mapData);
            m_ws->broadcast(zc);
        }
    }
    return resolved;
}

void DaemonApp::exportHandoffState(const QString& configDir) const
{
    if (m_packet)
        m_packet->exportHandoffState(configDir);

    // Save zone/spawn/player state so the new binary can restore them
    // before the web client reconnects and requests a snapshot. The
    // ".hstate_" prefix keeps these files distinct from any normal
    // save/restore files the user might have configured.
    if (m_zoneMgr && m_spawnShell && m_player) {
        showeq_params->saveRestoreBaseFilename = configDir + "/.hstate_";
        m_zoneMgr->saveZoneState();
        m_spawnShell->saveSpawns();
        m_player->savePlayerState();
    }

    // SpawnMonitor normally flushes via aboutToQuit, which _exit(75)
    // bypasses. Flush it explicitly so the new daemon loads current data.
    if (m_spawnMonitor)
        m_spawnMonitor->saveSpawnPoints();
}

bool DaemonApp::importHandoffState(const QString& configDir)
{
    if (!m_packet || !m_packet->importHandoffState(configDir))
        return false;

    if (m_zoneMgr && m_spawnShell && m_player) {
        showeq_params->saveRestoreBaseFilename = configDir + "/.hstate_";
        // Zone must be restored first — restoreSpawns checks the zone name.
        m_zoneMgr->restoreZoneState();
        m_spawnShell->restoreSpawns();
        m_player->restorePlayerState();
        // Clean up; these files are only valid for one handoff.
        QFile::remove(configDir + "/.hstate_Zone.dat");
        QFile::remove(configDir + "/.hstate_Spawns.dat");
        QFile::remove(configDir + "/.hstate_Player.dat");
    }

    // Reload map geometry and spawn-point list for the restored zone.
    // These normally fire via zoneBegin/zoneChanged signals which are
    // not emitted during a handoff restore.
    const QString zone = m_zoneMgr ? m_zoneMgr->shortZoneName() : QString();
    if (!zone.isEmpty() && zone != "unknown") {
        loadZoneMap(zone);
        // SpawnMonitor self-wired to zoneChanged in its ctor but never
        // received one. Call the slot directly: it sets m_zoneName and
        // calls loadSpawnPoints() without going through the signal (which
        // would clear SpawnShell state we just restored).
        if (m_spawnMonitor)
            m_spawnMonitor->zoneChanged(zone);
    }

    return true;
}
