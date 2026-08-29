#include "sessionadapter.h"

#include <QByteArray>
#include <QDateTime>
#include <QLoggingCategory>
#include <QSet>
#include <QTimer>
#include <algorithm>
#include <vector>

extern "C" { // pcap headers aren't c++-clean
#include <pcap.h>
}

#include <cerrno>
#include <cstring>
#include <ifaddrs.h>
#include <sys/socket.h>

#include "boxregistry.h"
#include "managerset.h"
#include "category.h"
#include "combatrouter.h"
#include "datetimemgr.h"
#include "envelopesink.h"
#include "filtermgr.h"
#include "group.h"
#include "guildshell.h"
#include "itemcache.h"
#include "itempacket.h"
#include "mapcore.h"
#include "messageshell.h"
#include "player.h"
#include "prefsbroker.h"
#include "protoencoder.h"
#include "mappackagehost.h"
#include "spawn.h"
#include "spawnmonitor.h"
#include "everquest.h"
#include "spawnshell.h"
#include "spellshell.h"
#include "zonemgr.h"
#include "zoneservermgr.h"

#include <QHostAddress>

#include "seq/v1/client.pb.h"

SessionAdapter::SessionAdapter(IEnvelopeSink* sink,
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
                               QObject*       parent)
    : QObject(parent)
    , m_sink(sink)
    , m_spawnShell(spawnShell)
    , m_zoneMgr(zoneMgr)
    , m_player(player)
    , m_mapData(mapData)
    , m_messageShell(messageShell)
    , m_groupMgr(groupMgr)
    , m_guildShell(guildShell)
    , m_spellShell(spellShell)
    , m_combatRouter(combatRouter)
    , m_categoryMgr(categoryMgr)
    , m_filterMgr(filterMgr)
    , m_prefsBroker(prefsBroker)
    , m_spawnMonitor(spawnMonitor)
    , m_itemCache(itemCache)
    , m_dateTimeMgr(dateTimeMgr)
    , m_zoneServerMgr(zoneServerMgr)
    , m_boxes(boxes)
{
    if (m_boxes) {
        connect(m_boxes, &BoxRegistry::changed,
                this, &SessionAdapter::sendBoxList);
        // B2: no character-follow rebind. The single truebox model means every box
        // shares m_activeManagers, so the sink binds once (startStreaming) and the
        // enterWorld reset + onPlayerIdChanged reprime the client across zone-ins.
    }
}

SessionAdapter::~SessionAdapter() = default;

void SessionAdapter::handleClientText(const QString& text)
{
    qInfo("ws text frame ignored: %s", qUtf8Printable(text));
}

void SessionAdapter::handleClientBinary(const QByteArray& bytes)
{
    // Leaky-bucket rate limit. 30 sustained msg/s with a 60-msg burst is
    // far above what any reasonable client UI emits (Subscribe is once
    // per connection; filter / pref edits are user-initiated). A runaway
    // client gets quietly dropped and warned once per session.
    constexpr double kTokensPerSec = 30.0;
    constexpr double kBucketCap = 60.0;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_bucketLastMs == 0) {
        m_bucketLastMs = now;
        m_bucketTokens = kBucketCap;
    } else {
        m_bucketTokens += (now - m_bucketLastMs) * kTokensPerSec / 1000.0;
        if (m_bucketTokens > kBucketCap) m_bucketTokens = kBucketCap;
        m_bucketLastMs = now;
    }
    if (m_bucketTokens < 1.0) {
        if (!m_rateLimitWarned) {
            qWarning("rate limit: client exceeded %.0f msg/s, dropping",
                     kTokensPerSec);
            m_rateLimitWarned = true;
        }
        return;
    }
    m_bucketTokens -= 1.0;
    m_rateLimitWarned = false;

    seq::v1::ClientEnvelope env;
    if (!env.ParseFromArray(bytes.constData(), bytes.size())) {
        qWarning("malformed ClientEnvelope (%lld bytes)",
                 static_cast<long long>(bytes.size()));
        return;
    }
    if (env.has_subscribe()) {
        if (m_subscribed) {
            qInfo("duplicate Subscribe ignored");
            return;
        }
        m_subscribed = true;
        startStreaming();
        return;
    }
    if (env.has_add_filter_rule() && m_filterMgr) {
        const auto& add = env.add_filter_rule();
        const QString pattern = QString::fromStdString(add.pattern());
        const uint8_t type = static_cast<uint8_t>(add.filter_type());
        if (add.per_zone()) {
            m_filterMgr->addZoneFilter(type, pattern);
        } else {
            m_filterMgr->addFilter(type, pattern);
        }
        // FilterMgr emits filtersChanged after the mutation, which fans
        // out to every connected SessionAdapter via onFilterRulesChanged.
        return;
    }
    if (env.has_remove_filter_rule() && m_filterMgr) {
        const auto& rem = env.remove_filter_rule();
        const QString pattern = QString::fromStdString(rem.pattern());
        const uint8_t type = static_cast<uint8_t>(rem.filter_type());
        if (rem.per_zone()) {
            m_filterMgr->remZoneFilter(type, pattern);
        } else {
            m_filterMgr->remFilter(type, pattern);
        }
        return;
    }
    if (env.has_edit_filter_rule() && m_filterMgr) {
        const auto& edit = env.edit_filter_rule();
        const QString oldPat = QString::fromStdString(edit.old_pattern());
        const QString newPat = QString::fromStdString(edit.new_pattern());
        const uint8_t type = static_cast<uint8_t>(edit.filter_type());
        if (edit.per_zone()) {
            m_filterMgr->editZoneFilter(type, oldPat, newPat);
        } else {
            m_filterMgr->editFilter(type, oldPat, newPat);
        }
        return;
    }
    if (env.has_save_filters() && m_filterMgr) {
        // Mirrors showeq's "Save Filters" / "Save Zone Filters"
        // menu items. Persistence is explicit on purpose so a UI
        // mutation doesn't immediately overwrite an operator's
        // hand-edited XML. No filtersChanged emit — saving doesn't
        // alter rule state, just persists it.
        if (env.save_filters().per_zone()) {
            m_filterMgr->saveZoneFilters();
        } else {
            m_filterMgr->saveFilters();
        }
        return;
    }
    if (env.has_reload_filters() && m_filterMgr) {
        // Re-read XML from disk for both global filters and the
        // current zone overlay (if any). loadFilters/loadZone each
        // emit filtersChanged, which fans out to SpawnShell (re-eval
        // of every spawn's flag bits) and every SessionAdapter
        // (broadcast a fresh FilterRulesUpdate).
        m_filterMgr->loadFilters();
        if (m_zoneMgr) {
            const QString zone = m_zoneMgr->shortZoneName();
            if (!zone.isEmpty()) m_filterMgr->loadZone(zone);
        }
        return;
    }
    if (env.has_set_pref() && m_prefsBroker) {
        // PrefsBroker validates against the allowlist; rejects (unknown
        // key, mismatched value variant) are dropped silently — no error
        // envelope yet. On success the broker emits prefChanged, which
        // every connected SessionAdapter forwards as PrefChanged.
        m_prefsBroker->apply(env.set_pref().pref());
        return;
    }
    if (env.has_rename_spawn_point() && m_spawnMonitor) {
        // Look up by SpawnPoint::key (decimal x|y|z) — same key the
        // daemon ships in SpawnPoint.key. Renames on candidate points
        // (m_spawns, not yet promoted to m_points) are intentionally
        // dropped: the candidate pool churns until two pops at the
        // same coords promote it, and labels assigned mid-promotion
        // would race with the legacy file format that only persists
        // promoted points. Empty `name` clears the label, matching
        // the showeq-c rename dialog where the user could clear the
        // text field before clicking OK.
        const auto& rn = env.rename_spawn_point();
        const QString key = QString::fromStdString(rn.key());
        const QString newName = QString::fromStdString(rn.name());
        const auto& points = m_spawnMonitor->spawnPoints();
        auto it = points.find(key);
        if (it != points.end()) {
            m_spawnMonitor->setName(it.value(), newName);
            // Write-through to <zone>.sp. Legacy waited for the next
            // zoneChanged to flush; we'd rather a single rename
            // survive a daemon crash. saveSpawnPoints() short-circuits
            // when m_modified is false, so a no-op rename is cheap.
            m_spawnMonitor->saveSpawnPoints();
        } else {
            qInfo("rename_spawn_point: unknown key %s",
                  qUtf8Printable(key));
        }
        return;
    }
    if (env.has_list_map_packages()) {
        // Session-scoped reply, not broadcast — mirrors ListDevices. The
        // picker is per-tab UI; selection (SetMapPackage) is what fans out.
        seq::v1::Envelope reply;
        if (m_mapPackageHost) {
            seq::encode::fillMapPackages(reply.mutable_map_packages(),
                                         m_mapPackageHost->mapPackages(),
                                         m_mapPackageHost->activeMapPackage());
        } else {
            reply.mutable_map_packages();
        }
        sendOrBuffer(std::move(reply));
        return;
    }
    if (env.has_set_map_package()) {
        // Apply daemon-globally. The host persists the choice, re-resolves
        // the current zone, and broadcasts a fresh MapPackagesUpdate +
        // ZoneChanged to every connected client (including this one), so no
        // per-socket reply is needed here. Unknown ids fall back to
        // "default" inside the host.
        if (m_mapPackageHost) {
            m_mapPackageHost->setMapPackage(
                QString::fromStdString(env.set_map_package().id()));
        }
        return;
    }
    if (env.has_list_devices()) {
        // Session-scoped reply, not broadcast — the picker is per-tab UI.
        // pcap_findalldevs returns every pcap-capable source on the host:
        // real NICs plus pseudo-sources like `any`, `nflog`, `nfqueue`,
        // `dbus-*`, `bluetooth-monitor`, `usbmon*`. Legacy showeq filtered
        // these out via getifaddrs() (util.cpp:enumerateNetworkDevices),
        // keeping only kernel interfaces with AF_INET/AF_PACKET addresses
        // — match that here so the picker doesn't list traffic sources
        // the user can't sniff EQ on. Failure of getifaddrs leaves the
        // allowlist empty and falls back to the unfiltered pcap list.
        QSet<QString> realIfaces;
        struct ifaddrs* ifaddr = nullptr;
        if (getifaddrs(&ifaddr) == 0) {
            for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
                if (ifa->ifa_addr == nullptr || ifa->ifa_name == nullptr)
                    continue;
                if (ifa->ifa_addr->sa_family == AF_INET ||
                    ifa->ifa_addr->sa_family == AF_PACKET)
                    realIfaces.insert(QString::fromLatin1(ifa->ifa_name));
            }
            freeifaddrs(ifaddr);
        } else {
            qWarning("getifaddrs failed: %s", strerror(errno));
        }

        seq::v1::Envelope reply;
        auto* list = reply.mutable_devices_list();
        char errbuf[PCAP_ERRBUF_SIZE] = {};
        pcap_if_t* alldevs = nullptr;
        if (pcap_findalldevs(&alldevs, errbuf) == 0) {
            for (pcap_if_t* d = alldevs; d != nullptr; d = d->next) {
                const QString name = QString::fromLatin1(d->name ? d->name : "");
                if (!realIfaces.isEmpty() && !realIfaces.contains(name))
                    continue;
                auto* dev = list->add_devices();
                dev->set_name(name.toStdString());
                dev->set_description(d->description ? d->description : "");
                dev->set_is_loopback((d->flags & PCAP_IF_LOOPBACK) != 0);
            }
            pcap_freealldevs(alldevs);
        } else {
            qWarning("pcap_findalldevs failed: %s", errbuf);
        }
        sendOrBuffer(std::move(reply));
        return;
    }
    if (env.has_set_active_box()) {
        if (!m_boxes) return;
        const QString id =
            QString::fromStdString(env.set_active_box().box_id());
        const bool alreadyActive = (m_boxes->activeCharacterId() == id);
        if (!m_boxes->setActiveCharacterId(id)) {
            qInfo("set_active_box: unknown box_id %s",
                  qUtf8Printable(id));
            return;
        }
        // setActiveCharacterId emits changed() on success → sendBoxList() fires.
        // If the box didn't switch (same ID), treat it as a manual refresh:
        // re-send the current snapshot so the client can resync without
        // having to do a box-swap dance.
        if (alreadyActive && m_liveTailing) {
            sendSnapshot();
            sendPlayerStats();
            sendGroupUpdate();
            sendBuffsUpdate();
            if (m_spellShell && !m_spellShell->targetEffects().isEmpty())
                sendEffectsUpdate();
        }
        return;
    }
}

void SessionAdapter::connectPerBox()
{
    connect(m_spawnShell, &SpawnShell::addItem,
            this,         &SessionAdapter::onAddItem);
    connect(m_spawnShell, &SpawnShell::delItem,
            this,         &SessionAdapter::onDelItem);
    connect(m_spawnShell, &SpawnShell::changeItem,
            this,         &SessionAdapter::onChangeItem);
    connect(m_spawnShell, &SpawnShell::spawnConsidered,
            this,         &SessionAdapter::onSpawnConsidered);
    connect(m_spawnShell, &SpawnShell::targetSpawn,
            this,         &SessionAdapter::onTargetSpawn);
    // NOTE: do NOT also connect Player::changeItem directly here. SpawnShell's
    // ctor forwards Player::changeItem straight into SpawnShell::changeItem
    // (spawnshell.cpp), which the connect above already subscribes to — a second
    // direct connect made every player update arrive twice, emitting two byte-
    // identical SpawnUpdated envelopes back to back (roughly half of all self
    // spawn updates in a replayed zone session were such duplicates).
    // `killSpawn`, `zoneBegin`, `zoneChanged` each have same-name slots on
    // their source classes (SpawnShell::killSpawn(const uint8_t*), etc.),
    // which confuses the compile-time &Class::member form. Fall back to
    // the string-based SIGNAL()/SLOT() connect for these three.
    connect(m_spawnShell,
            SIGNAL(killSpawn(const Item*, const Item*, uint16_t)),
            this,
            SLOT(onKillSpawn(const Item*, const Item*, uint16_t)));
    if (m_zoneMgr) {
        connect(m_zoneMgr, SIGNAL(zoneBegin(const QString&)),
                this,      SLOT(onZoneBegin(const QString&)));
        connect(m_zoneMgr, SIGNAL(zoneChanged(const QString&)),
                this,      SLOT(onZoneChanged(const QString&)));
        // eql: the current zone name arrives via zoneResolved (OP_NewZone), after
        // the spawn bulk — send the web the ZoneChanged envelope + map geometry
        // without the spawn-clear that zoneBegin/zoneChanged carry.
        connect(m_zoneMgr, SIGNAL(zoneResolved(const QString&)),
                this,      SLOT(onZoneChanged(const QString&)));
    }

    // Player stat signals — every one of these gets coalesced into a
    // single PlayerStats envelope via onPlayerStatsChanged. Slot has no
    // arguments; Qt5 quietly drops the signals' extra args.
    if (m_player) {
        connect(m_player, SIGNAL(hpChanged(int16_t, int16_t)),
                this,     SLOT(onPlayerStatsChanged()));
        connect(m_player, SIGNAL(manaChanged(uint32_t, uint32_t)),
                this,     SLOT(onPlayerStatsChanged()));
        connect(m_player, SIGNAL(statChanged(int, int, int)),
                this,     SLOT(onPlayerStatsChanged()));
        connect(m_player, SIGNAL(stamChanged(int, int, int, int)),
                this,     SLOT(onPlayerStatsChanged()));
        connect(m_player, SIGNAL(endChanged(uint32_t, uint32_t)),
                this,     SLOT(onPlayerStatsChanged()));
        // Coalesced multi-stat update (eql stat-sync) — one envelope per packet.
        connect(m_player, SIGNAL(vitalsChanged()),
                this,     SLOT(onPlayerStatsChanged()));
        connect(m_player, SIGNAL(moneyChanged(uint32_t)),
                this,     SLOT(onPlayerStatsChanged()));
        connect(m_player, SIGNAL(levelChanged(uint8_t)),
                this,     SLOT(onPlayerStatsChanged()));
        connect(m_player, SIGNAL(expChangedInt(int, int, int)),
                this,     SLOT(onPlayerStatsChanged()));
        connect(m_player, SIGNAL(expAltChangedInt(int, int, int)),
                this,     SLOT(onPlayerStatsChanged()));
        // NOTE: do NOT subscribe to addSkill — Player::charProfile fires
        // it 100x in a tight loop at zone-in and each emit re-serializes
        // a partially-populated PlayerStats. The client then diffs each
        // incremental snapshot and synthesizes ~100 bogus "0->N" entries
        // in the skill-up log. The final population is captured by other
        // signals (newPlayer / expChangedInt) that fire after the loop.
        connect(m_player, SIGNAL(changeSkill(int, int)),
                this,     SLOT(onPlayerStatsChanged()));
        connect(m_player, SIGNAL(newPlayer()),
                this,     SLOT(onPlayerStatsChanged()));
        // The player's spawn ID changes once at zone-in (from 0 -> real
        // id). The client tracks `playerId` from the Snapshot's
        // player_id field, so we resend a fresh snapshot here — that
        // drops the old player entry and re-establishes player_id
        // pointing at the new one. Connection happens after SpawnShell's
        // playerChangedID slot (registered in SpawnShell ctor), so by
        // the time we run the re-insert into m_players is already done.
        connect(m_player, SIGNAL(changedID(uint16_t, uint16_t)),
                this,     SLOT(onPlayerIdChanged()));
        connect(m_player, SIGNAL(expGained(const QString&, int, long, QString)),
                this,     SLOT(onExpGained(const QString&, int, long, QString)));
    }

    if (m_messageShell) {
        connect(m_messageShell,
                SIGNAL(chatMessage(uint32_t, const QString&,
                                   const QString&, const QString&,
                                   uint32_t, const QString&)),
                this,
                SLOT(onChatMessage(uint32_t, const QString&,
                                   const QString&, const QString&,
                                   uint32_t, const QString&)));
        connect(m_messageShell,
                SIGNAL(inspectReceived(const inspectDataStruct*)),
                this,
                SLOT(onInspectAnswer(const inspectDataStruct*)));
        connect(m_messageShell,
                SIGNAL(lootDropsReceived(uint32_t, const QString&, const QStringList&, const QVector<uint32_t>&, const QVector<uint32_t>&)),
                this,
                SLOT(onLootDrops(uint32_t, const QString&, const QStringList&, const QVector<uint32_t>&, const QVector<uint32_t>&)));
        connect(m_messageShell,
                SIGNAL(lootTransactionReceived(uint32_t, uint32_t, uint32_t, uint32_t, bool)),
                this,
                SLOT(onLootTransaction(uint32_t, uint32_t, uint32_t, uint32_t, bool)));
    }

    if (m_guildShell) {
        // The roster replaces wholesale, so only the terminal loaded() matters —
        // connecting added() too would emit one full roster envelope per member.
        connect(m_guildShell, SIGNAL(loaded()), this, SLOT(sendGuildRoster()));
        connect(m_guildShell, SIGNAL(motdChanged()), this, SLOT(sendGuildMotd()));
        // A member's online-status update re-sends the whole roster (proto has no
        // partial-member message); the signal's GuildMember* arg is dropped.
        connect(m_guildShell, SIGNAL(updated(const GuildMember*)),
                this, SLOT(sendGuildRoster()));
        // Rank-name table grows (arrives per-rank after the roster); each send is
        // the full known table, so the client just takes the latest.
        connect(m_guildShell, SIGNAL(rankNamesChanged()),
                this, SLOT(sendGuildRankNames()));
    }

    if (m_groupMgr) {
        connect(m_groupMgr, SIGNAL(rosterUpdated()),
                this,       SLOT(onGroupChanged()));
    }

    if (m_spellShell) {
        connect(m_spellShell, SIGNAL(addSpell(const SpellItem*)),
                this,         SLOT(onBuffsChanged()));
        connect(m_spellShell, SIGNAL(delSpell(const SpellItem*)),
                this,         SLOT(onBuffsChanged()));
        connect(m_spellShell, SIGNAL(changeSpell(const SpellItem*)),
                this,         SLOT(onBuffsChanged()));
        connect(m_spellShell, SIGNAL(clearSpells()),
                this,         SLOT(onBuffsChanged()));
        // Mob-effect signals (player's DoTs/debuffs on mobs) → SpawnEffectsUpdate.
        connect(m_spellShell, SIGNAL(addEffect(const SpellItem*)),
                this,         SLOT(onEffectsChanged()));
        connect(m_spellShell, SIGNAL(delEffect(const SpellItem*)),
                this,         SLOT(onEffectsChanged()));
        connect(m_spellShell, SIGNAL(changeEffect(const SpellItem*)),
                this,         SLOT(onEffectsChanged()));
        connect(m_spellShell, SIGNAL(clearEffects()),
                this,         SLOT(onEffectsChanged()));
    }

    if (m_combatRouter) {
        connect(m_combatRouter,
                SIGNAL(combatEvent(uint32_t, const QString&,
                                   uint32_t, const QString&,
                                   uint32_t, int32_t,
                                   uint32_t, const QString&)),
                this,
                SLOT(onCombatEvent(uint32_t, const QString&,
                                   uint32_t, const QString&,
                                   uint32_t, int32_t,
                                   uint32_t, const QString&)));
        connect(m_combatRouter,
                SIGNAL(spawnCast(uint32_t, const QString&,
                                 uint32_t, const QString&, uint32_t)),
                this,
                SLOT(onSpawnCast(uint32_t, const QString&,
                                 uint32_t, const QString&, uint32_t)));
    }

    if (m_spawnMonitor) {
        // SpawnMonitor surfaces four signals — newSpawnPoint (promoted
        // from candidate to tracked), spawnPointUpdated (re-pop / kill
        // restart), spawnPointDeleted (explicit user delete), and
        // clearSpawnPoints (zone change / clear-all). The current
        // promoted set is iterated into the Snapshot below.
        connect(m_spawnMonitor, &SpawnMonitor::newSpawnPoint,
                this,           &SessionAdapter::onSpawnPointAdded);
        connect(m_spawnMonitor, &SpawnMonitor::spawnPointUpdated,
                this,           &SessionAdapter::onSpawnPointUpdated);
        connect(m_spawnMonitor, &SpawnMonitor::spawnPointDeleted,
                this,           &SessionAdapter::onSpawnPointDeleted);
        connect(m_spawnMonitor, &SpawnMonitor::clearSpawnPoints,
                this,           &SessionAdapter::onSpawnPointsCleared);
    }
}

void SessionAdapter::startStreaming()
{
    if (!m_spawnShell) {
        qWarning("Subscribe received before state managers were wired");
        return;
    }

    // Re-point to the active character's managers before wiring signals. The
    // constructor receives the initial box's managers from WsServer; if the
    // active character was promoted between construction and this Subscribe
    // (e.g. a new browser tab opened to an already-running session), the
    // constructor-set managers are stale.
    if (m_managerProvider && m_boxes) {
        const ManagerSet* ns =
            m_managerProvider->managersForBox(m_boxes->activeCharacterId());
        if (ns && ns->spawnShell && ns->spawnShell != m_spawnShell) {
            m_spawnShell   = ns->spawnShell;
            m_zoneMgr      = ns->zoneMgr;
            m_player       = ns->player;
            m_messageShell = ns->messageShell;
            m_groupMgr     = ns->groupMgr;
            m_guildShell   = ns->guildShell;   // MOTD + roster ride this
            m_spellShell   = ns->spellShell;
            m_combatRouter = ns->combatRouter;
            m_spawnMonitor = ns->spawnMonitor;
        }
    }

    // STEP 1: connect signals — handlers push into m_buffered until the
    //         snapshot is drained. This MUST happen before any iteration
    //         of SpawnShell state so mid-iteration changes are captured.
    //         Per-box manager signals go through connectPerBox() so an
    //         active-box switch can disconnect+reconnect them; the
    //         daemon-global managers below are wired once and never move.
    connectPerBox();

    if (m_categoryMgr) {
        // CategoryMgr's add/del/cleared/loaded signals all fire on
        // mutation; coalesce into one slot that resends the full list.
        connect(m_categoryMgr, SIGNAL(addCategory(const Category*)),
                this,          SLOT(onCategoriesChanged()));
        connect(m_categoryMgr, SIGNAL(delCategory(const Category*)),
                this,          SLOT(onCategoriesChanged()));
        connect(m_categoryMgr, SIGNAL(clearedCategories()),
                this,          SLOT(onCategoriesChanged()));
        connect(m_categoryMgr, SIGNAL(loadedCategories()),
                this,          SLOT(onCategoriesChanged()));
    }

    if (m_filterMgr) {
        connect(m_filterMgr, SIGNAL(filtersChanged()),
                this,        SLOT(onFilterRulesChanged()));
    }

    if (m_prefsBroker) {
        // Direct connection in the same thread; pref payloads are small
        // and this fans out to every connected SessionAdapter so each
        // emits its own PrefChanged envelope.
        connect(m_prefsBroker, &PrefsBroker::prefChanged,
                this,          &SessionAdapter::onPrefChanged);
    }

    if (m_dateTimeMgr) {
        // Sync-point only — see SessionAdapter::onEqTimeSync header note.
        connect(m_dateTimeMgr, &DateTimeMgr::syncDateTime,
                this,          &SessionAdapter::onEqTimeSync);
    }

    if (m_zoneServerMgr) {
        connect(m_zoneServerMgr, &ZoneServerMgr::zoneServerChanged,
                this,            &SessionAdapter::onZoneServerChanged);
    }

    if (m_itemCache) {
        connect(m_itemCache, &ItemCache::itemLearned,
                this,        &SessionAdapter::onItemLearned);
        connect(m_itemCache, &ItemCache::wornSlotsChanged,
                this,        &SessionAdapter::onWornSlotsChanged);
    }

    // STEP 2: iterate current state into a Snapshot and ship it.
    sendSnapshot();

    // Initial PlayerStats so the client has something to render before
    // the next Player signal fires.
    if (m_player) {
        sendPlayerStats();
    }
    // Initial GroupUpdate (all 6 slots, may all be empty).
    if (m_groupMgr) {
        sendGroupUpdate();
    }
    // Initial GuildRoster. Only when one has actually arrived — the roster is
    // sent on request, so an empty envelope here would claim "no guild" for
    // every session that simply hasn't asked yet.
    if (m_guildShell && !m_guildShell->members().isEmpty()) {
        sendGuildRoster();
    }
    // Initial GuildMotd — only once a MOTD packet has actually arrived (an empty
    // MOTD is legitimate state, but "no packet yet" is not something to assert).
    if (m_guildShell && m_guildShell->hasMotd()) {
        sendGuildMotd();
    }
    // Initial GuildRankNames — only when the rank-name table has entries, so a
    // client that connects after the guild data arrived resolves member ranks.
    if (m_guildShell && !m_guildShell->rankNames().isEmpty()) {
        sendGuildRankNames();
    }
    // Initial BuffsUpdate (may be an empty list before login).
    if (m_spellShell) {
        sendBuffsUpdate();
        // No initial EMPTY effects send: effects only exist after the player
        // casts on a mob, and the signal-driven path handles add/clear. An
        // empty envelope here would just add a wallclock-captured_ms envelope
        // to every session (golden-flapping, like buffs). Only re-prime when
        // there's actually something to catch up on.
        if (!m_spellShell->targetEffects().isEmpty())
            sendEffectsUpdate();
    }
    // Initial CategoriesUpdate (loaded from prefs at CategoryMgr ctor).
    if (m_categoryMgr) {
        sendCategoriesUpdate();
    }
    // Initial FilterRulesUpdate (global + per-zone rules currently
    // loaded; either may be empty if files don't exist).
    if (m_filterMgr) {
        sendFilterRulesUpdate();
    }
    // Initial PrefsSnapshot — every allowlisted pref's current value.
    if (m_prefsBroker) {
        sendPrefsSnapshot();
    }
    // Initial BoxListUpdated so the picker UI has data before any
    // registry mutation. Stage 4 of docs/MULTIBOX_PLAN.md.
    if (m_boxes) {
        sendBoxList();
    }
    // Initial MapPackagesUpdate so the client can populate its package
    // picker. Suppressed in deterministic/golden mode (sendMapPackages).
    sendMapPackages();

    // STEP 3: drain anything that arrived during the iteration.
    for (auto& env : m_buffered) {
        emitEnvelope(std::move(env));
    }
    m_buffered.clear();

    // STEP 4: from here on, handlers emit directly to the socket.
    m_liveTailing = true;
}

void SessionAdapter::sendSnapshot()
{
    seq::v1::Envelope env;
    auto* snap = env.mutable_snapshot();
    snap->set_session_id(m_sessionId.toStdString());
    // Backend data namespace (compile-time per SEQ_TARGET) so clients can keep
    // per-backend data (loot DB, etc.) apart: ".scry" (Live) / ".scry/eql".
    snap->set_data_namespace(SEQ_DATA_NAMESPACE);
    if (m_zoneMgr) {
        snap->set_zone_short(m_zoneMgr->shortZoneName().toStdString());
        snap->set_zone_long(m_zoneMgr->longZoneName().toStdString());
        m_lastSnapshotZone = m_zoneMgr->shortZoneName();
    }
    if (m_player) {
        snap->set_player_id(m_player->getPlayerID());
    }
    if (m_mapData && m_mapData->numLayers() > 0) {
        seq::encode::fillMapGeometry(snap->mutable_geometry(), *m_mapData);
    }

    // SpawnShell maintains separate ItemMaps (QHash<int, Item*>) for
    // spawns, drops, doors, and players; the Phase 1 client renders all
    // of them the same way (as labeled dots), so we collapse them into
    // a single repeated Spawn list. ItemMap is a QHash; Qt randomizes
    // hash seeds per-process so iteration order is not stable. Collect
    // pointers into a vector and sort by id before encoding so the
    // Snapshot bytes are deterministic across runs (regression-harness
    // requirement; clients key by id and don't depend on order anyway).
    std::vector<const Item*> all;
    QSet<uint16_t> seenIds;
    auto collect = [&](const ItemMap& map) {
        ItemConstIterator it(map);
        while (it.hasNext()) {
            it.next();
            if (const Item* item = it.value()) {
                all.push_back(item);
                seenIds.insert(item->id());
            }
        }
    };
    collect(m_spawnShell->spawns());
    collect(m_spawnShell->drops());
    collect(m_spawnShell->doors());
    collect(m_spawnShell->getConstMap(tPlayer));

    // Belt + suspenders: if the player object never made it into
    // SpawnShell's m_players map (which can happen during early init
    // before zonePlayer fires), include it directly so the client gets
    // a marker for `player_id`.
    if (m_player && m_player->getPlayerID() != 0 &&
        !seenIds.contains(m_player->getPlayerID())) {
        all.push_back(m_player);
    }

    // EQ reuses ids across spawn types (a NPC and a door can both have id
    // N), so sort by (id, name) — id alone leaves duplicate-id pairs in
    // QHash-iteration order, which is randomized per process.
    std::sort(all.begin(), all.end(),
              [](const Item* a, const Item* b) {
                  if (a->id() != b->id()) return a->id() < b->id();
                  return a->name() < b->name();
              });

    for (const Item* item : all) {
        seq::encode::fillSpawn(snap->add_spawns(), *item,
                               m_categoryMgr, m_filterMgr);
    }

    // Seed any already-promoted SpawnPoints. QHash iteration order is
    // randomized per-process, so sort by key for deterministic Snapshot
    // bytes (regression-harness requirement).
    if (m_spawnMonitor) {
        std::vector<const SpawnPoint*> points;
        const auto& map = m_spawnMonitor->spawnPoints();
        points.reserve(map.size());
        for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
            if (it.value()) points.push_back(it.value());
        }
        std::sort(points.begin(), points.end(),
                  [](const SpawnPoint* a, const SpawnPoint* b) {
                      return a->key() < b->key();
                  });
        for (const SpawnPoint* sp : points) {
            seq::encode::fillSpawnPoint(snap->add_spawn_points(), *sp,
                                        m_deterministic);
        }
    }

    // Only emit items when the cache is non-empty. Worn-set fields
    // (item_totals + worn_set) ride a separate gate: emit only when
    // we've actually observed an equip event this session, so resumes
    // without an OP_ItemPacket fire don't ship a zero-valued message
    // (which would still serialize, changing the byte stream).
    if (m_itemCache && m_itemCache->size() > 0) {
        for (uint32_t id : m_itemCache->sortedIds()) {
            ItemTemplate t;
            if (!m_itemCache->lookup(id, &t)) continue;
            seq::encode::fillItem(snap->add_items(), t);
        }
    }
    if (m_itemCache && !m_itemCache->wornSlots().isEmpty()) {
        seq::encode::fillItemTotals(snap->mutable_item_totals(),
                                     *m_itemCache);
        seq::encode::fillWornSet(snap->mutable_worn_set(), *m_itemCache);
    }

    // Norrath time + zone server endpoint, both populated only after
    // their first wire arrival. Empty fields stay absent on the wire so
    // deterministic goldens don't shift before the relevant opcodes are
    // observed in the fixture.
    if (m_dateTimeMgr) {
        const QDateTime& dt = m_dateTimeMgr->eqDateTime();
        if (dt.isValid()) {
            auto* sync = snap->mutable_eq_time_sync();
            sync->set_year(dt.date().year());
            sync->set_month(dt.date().month());
            sync->set_day(dt.date().day());
            sync->set_hour(dt.time().hour());
            sync->set_minute(dt.time().minute());
        }
    }
    if (m_zoneServerMgr && m_zoneServerMgr->hasInfo()) {
        auto* zs = snap->mutable_zone_server();
        zs->set_host(m_zoneServerMgr->host().toStdString());
        zs->set_port(m_zoneServerMgr->port());
    }

    emitEnvelope(std::move(env));
}

void SessionAdapter::sendMapPackages()
{
    // Suppress in deterministic/golden mode so tier-2 replay goldens stay
    // byte-identical (mirrors session_id handling in sendSnapshot).
    if (m_deterministic) return;
    if (!m_mapPackageHost) return;
    seq::v1::Envelope env;
    seq::encode::fillMapPackages(env.mutable_map_packages(),
                                 m_mapPackageHost->mapPackages(),
                                 m_mapPackageHost->activeMapPackage());
    sendOrBuffer(std::move(env));
}

void SessionAdapter::deliverBroadcast(const seq::v1::Envelope& env)
{
    // Copy: the caller owns the source Envelope and fans it to many
    // adapters. Each adapter stamps its own seq/server_ts_ms in
    // emitEnvelope. Suppressed on the golden adapter to keep goldens stable.
    if (m_deterministic) return;
    seq::v1::Envelope copy = env;
    sendOrBuffer(std::move(copy));
}

void SessionAdapter::emitEnvelope(seq::v1::Envelope&& env)
{
    env.set_seq(++m_seq);
    env.set_server_ts_ms(static_cast<uint64_t>(
        QDateTime::currentMSecsSinceEpoch()));
    m_sink->send(env);

    // Resume buffer. Cap by time window (30s) and by absolute size
    // (10k entries) so a chatty zone can't OOM the daemon between
    // disconnect + reconnect.
    constexpr int kMaxEntries = 10000;
    constexpr qint64 kWindowMs = 30 * 1000;
    m_ringBuffer.push_back(std::move(env));
    const qint64 cutoff = m_ringBuffer.back().server_ts_ms() - kWindowMs;
    while (!m_ringBuffer.empty() &&
           static_cast<qint64>(m_ringBuffer.front().server_ts_ms()) < cutoff) {
        m_ringBuffer.pop_front();
    }
    while (static_cast<int>(m_ringBuffer.size()) > kMaxEntries) {
        m_ringBuffer.pop_front();
    }
}

void SessionAdapter::replaySince(uint64_t lastSeq)
{
    // Linear scan is fine — the buffer is bounded at 10k. Skip envelopes
    // the client already has, replay the rest in order through the
    // current sink. We do NOT bump m_seq or rewrite server_ts_ms here:
    // the envelope was already finalized when it was first emitted, and
    // a faithful replay preserves the original ordering + timestamps.
    for (const auto& env : m_ringBuffer) {
        if (env.seq() > lastSeq) {
            m_sink->send(env);
        }
    }
}

void SessionAdapter::sendOrBuffer(seq::v1::Envelope&& env)
{
    if (m_liveTailing) {
        emitEnvelope(std::move(env));
    } else {
        m_buffered.append(std::move(env));
    }
}

void SessionAdapter::onAddItem(const Item* item)
{
    if (!item) return;
    seq::v1::Envelope env;
    seq::encode::fillSpawn(env.mutable_spawn_added()->mutable_spawn(), *item,
                           m_categoryMgr, m_filterMgr);
    sendOrBuffer(std::move(env));
}

void SessionAdapter::onDelItem(const Item* item)
{
    if (!item) return;
    seq::v1::Envelope env;
    env.mutable_spawn_removed()->set_id(item->id());
    sendOrBuffer(std::move(env));
}

void SessionAdapter::onChangeItem(const Item* item, uint32_t changeType)
{
    if (!item) return;

    // tSpawnChangedALL is the legacy "full re-render" signal — used by
    // SpawnShell::changePlayerID, killSpawn-corpse-replace,
    // updateSpawnInfo, etc. The spawn may not currently exist in the
    // client (e.g. changePlayerID emits delItem(oldId) then
    // changeItem(player, ALL) at the new id). Send SpawnAdded so the
    // client overwrites/creates the entry; SpawnUpdated would be dropped
    // because there's no existing entry at the new id.
    //
    // Filter-flag changes get the same treatment: filter flags feed into
    // category membership (see fillSpawn), so when a user adds a rule
    // the spawn's category_ids set changes too. SpawnUpdated only carries
    // filter_flags, so the client's category_ids would otherwise stay
    // stale until re-zone.
    const bool filterChanged =
        (changeType & (tSpawnChangedFilter | tSpawnChangedRuntimeFilter)) != 0;
    if ((changeType & tSpawnChangedALL) == tSpawnChangedALL || filterChanged) {
        seq::v1::Envelope env;
        seq::encode::fillSpawn(env.mutable_spawn_added()->mutable_spawn(),
                               *item, m_categoryMgr, m_filterMgr);
        sendOrBuffer(std::move(env));
        return;
    }

    seq::v1::Envelope env;
    auto* upd = env.mutable_spawn_updated();
    upd->set_id(item->id());

    if (const auto* sp = dynamic_cast<const Spawn*>(item)) {
        if (changeType & tSpawnChangedPosition) {
            seq::encode::fillPos(upd->mutable_pos(), *sp);
        }
        if (changeType & tSpawnChangedHP) {
            upd->set_hp_cur(sp->HP());
        }
        if (changeType & tSpawnChangedName) {
            upd->set_name(sp->name().toStdString());
        }
    }
    sendOrBuffer(std::move(env));
}

void SessionAdapter::onKillSpawn(const Item* deceased, const Item* killer,
                                 uint16_t killerId)
{
    seq::v1::Envelope env;
    auto* k = env.mutable_spawn_killed();
    if (deceased) k->set_deceased_id(deceased->id());
    k->set_killer_id(killer ? killer->id() : killerId);
    sendOrBuffer(std::move(env));
}

void SessionAdapter::onZoneBegin(const QString& shortName)
{
    // DaemonApp::loadZoneMap is wired to the same zone signal before this
    // adapter, so the shared production projector sees the new geometry.
    seq::v1::Envelope env = seq::encode::zoneChanged(
        shortName, m_zoneMgr ? m_zoneMgr->longZoneName() : QString(),
        m_mapData);
    sendOrBuffer(std::move(env));
}

void SessionAdapter::onZoneChanged(const QString& shortName)
{
    onZoneBegin(shortName);
    // A zone-in Snapshot may already have gone out (from onPlayerIdChanged, on
    // self-id adoption) carrying the NEW player_id but the OLD zone name — the
    // new name only resolves here (OP_NewZone). Re-send now that the zone is
    // known so the client's zone_short matches and its zoneChanged handling
    // doesn't discard the just-established spawns (incl. the player's own).
    // Deduped on m_lastSnapshotZone so the eql double-signal (zoneChanged +
    // zoneResolved) and same-zone map-package re-emits don't double-send.
    if (m_subscribed && m_zoneMgr &&
        m_zoneMgr->shortZoneName() != m_lastSnapshotZone) {
        sendSnapshot();
    }
}

void SessionAdapter::onPlayerStatsChanged()
{
    if (!m_player) return;
    sendPlayerStats();
}

void SessionAdapter::onPlayerIdChanged()
{
    if (!m_subscribed) return;
    sendSnapshot();
    sendPlayerStats();
}

void SessionAdapter::onChatMessage(uint32_t channel, const QString& from,
                                   const QString& target, const QString& text,
                                   uint32_t chatColor,
                                   const QString& channelName)
{
    seq::v1::Envelope env;
    auto* chat = env.mutable_chat();
    chat->set_channel(channel);
    chat->set_from(from.toStdString());
    chat->set_target(target.toStdString());
    chat->set_text(text.toStdString());
    chat->set_chat_color(chatColor);
    if (!channelName.isEmpty())
        chat->set_channel_name(channelName.toStdString());
    sendOrBuffer(std::move(env));
}

void SessionAdapter::onLootTransaction(uint32_t corpseId, uint32_t itemId,
                                       uint32_t quantity, uint32_t coinCopper,
                                       bool fromCorpse)
{
    seq::v1::Envelope env;
    auto* lt = env.mutable_loot_transaction();
    lt->set_corpse_id(corpseId);
    lt->set_item_id(itemId);
    lt->set_quantity(quantity);
    lt->set_coin_copper(coinCopper);
    lt->set_coin_from_corpse(fromCorpse);
    sendOrBuffer(std::move(env));
}

void SessionAdapter::onExpGained(const QString& mobName, int mobLevel,
                                  long xpGained, const QString& zoneName)
{
    seq::v1::Envelope env;
    seq::encode::fillExperienceTick(env.mutable_exp(),
        mobName, mobLevel,
        static_cast<uint32_t>(xpGained > 0 ? xpGained : 0),
        zoneName);
    sendOrBuffer(std::move(env));
}

void SessionAdapter::onGroupChanged()
{
    if (!m_groupMgr) return;
    sendGroupUpdate();
}

void SessionAdapter::sendGroupUpdate()
{
    if (!m_groupMgr) return;
    seq::v1::Envelope env;
    seq::encode::fillGroupUpdate(env.mutable_group(), *m_groupMgr);
    sendOrBuffer(std::move(env));
}

void SessionAdapter::sendGuildRoster()
{
    if (!m_guildShell) return;
    seq::v1::Envelope env;
    seq::encode::fillGuildRoster(env.mutable_guild_roster(), *m_guildShell);
    sendOrBuffer(std::move(env));
}

void SessionAdapter::sendGuildMotd()
{
    if (!m_guildShell) return;
    seq::v1::Envelope env;
    seq::encode::fillGuildMotd(env.mutable_guild_motd(), *m_guildShell);
    sendOrBuffer(std::move(env));
}

void SessionAdapter::sendGuildRankNames()
{
    if (!m_guildShell) return;
    seq::v1::Envelope env;
    seq::encode::fillGuildRankNames(env.mutable_guild_rank_names(), *m_guildShell);
    sendOrBuffer(std::move(env));
}

void SessionAdapter::onBuffsChanged()
{
    if (!m_spellShell) return;
    sendBuffsUpdate();
}

void SessionAdapter::onEffectsChanged()
{
    if (!m_spellShell) return;
    // A mob's OP_BuffList emits one add/changeEffect per buff; sending the full
    // list on each would flood the client. Coalesce a burst into one emit per
    // event-loop turn. Golden mode stays synchronous so output is reproducible.
    if (m_deterministic) { sendEffectsUpdate(); return; }
    if (m_effectsDirty) return;
    m_effectsDirty = true;
    QTimer::singleShot(0, this, [this] {
        m_effectsDirty = false;
        if (m_spellShell) sendEffectsUpdate();
    });
}

void SessionAdapter::sendEffectsUpdate()
{
    if (!m_spellShell) return;
    seq::v1::Envelope env;
    auto* update = env.mutable_spawn_effects();
    update->set_captured_ms(static_cast<uint64_t>(
        QDateTime::currentMSecsSinceEpoch()));
    for (const SpellItem* s : m_spellShell->targetEffects()) {
        if (!s) continue;
        seq::encode::fillBuff(update->add_effects(), *s);
    }
    sendOrBuffer(std::move(env));
}

void SessionAdapter::sendBuffsUpdate()
{
    if (!m_spellShell) return;
    seq::v1::Envelope env;
    auto* update = env.mutable_buffs();
    update->set_captured_ms(static_cast<uint64_t>(
        QDateTime::currentMSecsSinceEpoch()));
    for (const SpellItem* s : m_spellShell->spellList()) {
        if (!s) continue;
        seq::encode::fillBuff(update->add_buffs(), *s);
    }
    sendOrBuffer(std::move(env));
}

void SessionAdapter::onCombatEvent(uint32_t sourceId, const QString& sourceName,
                                   uint32_t targetId, const QString& targetName,
                                   uint32_t type, int32_t damage,
                                   uint32_t spellId, const QString& spellName)
{
    seq::v1::Envelope env;
    auto* ev = env.mutable_combat();
    ev->set_source_id(sourceId);
    ev->set_source_name(sourceName.toStdString());
    ev->set_target_id(targetId);
    ev->set_target_name(targetName.toStdString());
    ev->set_type(type);
    ev->set_damage(damage);
    ev->set_spell_id(spellId);
    ev->set_spell_name(spellName.toStdString());
    sendOrBuffer(std::move(env));
}

void SessionAdapter::onSpawnCast(uint32_t casterId, const QString& casterName,
                                 uint32_t spellId, const QString& spellName,
                                 uint32_t castTimeMs)
{
    seq::v1::Envelope env;
    auto* ev = env.mutable_spawn_cast();
    ev->set_caster_id(casterId);
    ev->set_caster_name(casterName.toStdString());
    ev->set_spell_id(spellId);
    ev->set_spell_name(spellName.toStdString());
    ev->set_cast_time_ms(castTimeMs);
    sendOrBuffer(std::move(env));
}

void SessionAdapter::onCategoriesChanged()
{
    if (!m_categoryMgr) return;
    sendCategoriesUpdate();
}

void SessionAdapter::sendCategoriesUpdate()
{
    if (!m_categoryMgr) return;
    seq::v1::Envelope env;
    seq::encode::fillCategoriesUpdate(env.mutable_categories(), *m_categoryMgr);
    sendOrBuffer(std::move(env));
}

void SessionAdapter::onFilterRulesChanged()
{
    if (!m_filterMgr) return;
    sendFilterRulesUpdate();
}

void SessionAdapter::sendFilterRulesUpdate()
{
    if (!m_filterMgr) return;
    seq::v1::Envelope env;
    seq::encode::fillFilterRulesUpdate(env.mutable_filter_rules(), *m_filterMgr);
    sendOrBuffer(std::move(env));
}

void SessionAdapter::onPrefChanged(const seq::v1::Pref& pref)
{
    if (m_deterministic) return;
    seq::v1::Envelope env;
    *env.mutable_pref_changed()->mutable_pref() = pref;
    sendOrBuffer(std::move(env));
}

void SessionAdapter::onSpawnConsidered(const Item* item)
{
    if (!item) return;
    seq::v1::Envelope env;
    env.mutable_considered()->set_spawn_id(item->id());
    sendOrBuffer(std::move(env));
}

void SessionAdapter::onTargetSpawn(uint32_t spawnId)
{
    seq::v1::Envelope env;
    env.mutable_targeted()->set_spawn_id(spawnId);
    sendOrBuffer(std::move(env));
}

void SessionAdapter::sendPrefsSnapshot()
{
    if (!m_prefsBroker) return;
    // Prefs are entirely user-driven (Network/Device, etc.) — locking
    // them into the tier-2 regression golden would mean any local
    // config edit forces a regen. setDeterministic() is only set by
    // --record-golden, so live clients still see prefs.
    if (m_deterministic) return;
    seq::v1::Envelope env;
    m_prefsBroker->fillSnapshot(env.mutable_prefs());
    sendOrBuffer(std::move(env));
}

void SessionAdapter::sendPlayerStats()
{
    seq::v1::Envelope env;
    seq::encode::fillPlayerStats(env.mutable_player_stats(), *m_player);
    sendOrBuffer(std::move(env));
}

void SessionAdapter::onSpawnPointAdded(const SpawnPoint* sp)
{
    if (!sp) return;
    seq::v1::Envelope env;
    seq::encode::fillSpawnPoint(
        env.mutable_spawn_point_added()->mutable_point(), *sp,
        m_deterministic);
    sendOrBuffer(std::move(env));
}

void SessionAdapter::onSpawnPointUpdated(const SpawnPoint* sp)
{
    if (!sp) return;
    seq::v1::Envelope env;
    seq::encode::fillSpawnPoint(
        env.mutable_spawn_point_updated()->mutable_point(), *sp,
        m_deterministic);
    sendOrBuffer(std::move(env));
}

void SessionAdapter::onSpawnPointDeleted(const SpawnPoint* sp)
{
    if (!sp) return;
    seq::v1::Envelope env;
    env.mutable_spawn_point_removed()->set_key(sp->key().toStdString());
    sendOrBuffer(std::move(env));
}

void SessionAdapter::onSpawnPointsCleared()
{
    seq::v1::Envelope env;
    env.mutable_spawn_points_cleared();
    sendOrBuffer(std::move(env));
}

void SessionAdapter::onItemLearned(uint32_t itemId)
{
    if (!m_itemCache) return;
    ItemTemplate t;
    if (!m_itemCache->lookup(itemId, &t)) return;

    seq::v1::Envelope envItem;
    seq::encode::fillItem(envItem.mutable_item_learned()->mutable_item(), t);
    sendOrBuffer(std::move(envItem));
}

void SessionAdapter::onWornSlotsChanged()
{
    if (!m_itemCache) return;

    seq::v1::Envelope envWorn;
    seq::encode::fillWornSet(envWorn.mutable_worn_set(), *m_itemCache);
    sendOrBuffer(std::move(envWorn));

    seq::v1::Envelope envTotals;
    seq::encode::fillItemTotals(envTotals.mutable_item_totals(), *m_itemCache);
    sendOrBuffer(std::move(envTotals));
}

void SessionAdapter::onEqTimeSync(const QDateTime& dt)
{
    if (!dt.isValid()) return;
    sendOrBuffer(seq::encode::eqTimeSync(dt));
}

void SessionAdapter::onZoneServerChanged(const QString& host, quint16 port)
{
    sendOrBuffer(seq::encode::zoneServer(host, port));
}

void SessionAdapter::sendBoxList()
{
    if (!m_boxes) return;
    // Golden recorder (deterministic mode): BoxListUpdated content
    // depends on the packet stream's exact 5-tuples and the timing of
    // OP_EnterWorld arrivals, which makes byte-cmp against pre-Stage-4
    // goldens flap. Production WebSocket clients still get the
    // envelope; only the golden flow suppresses it.
    if (m_deterministic) return;
    seq::v1::Envelope env;
    auto* upd = env.mutable_box_list_updated();
    // Build the picker from the name-keyed view (BoxRegistry::characters): one
    // entry per distinct identity. Its live session (c.session == currentBoxFor)
    // supplies ip / packet count, so a character that has re-zoned shows its
    // CURRENT session's activity rather than the dead first-seen anchor's.
    for (const Character& c : m_boxes->characters()) {
        auto* meta = upd->add_boxes();
        meta->set_box_id(c.id.toStdString());
        meta->set_display_name(c.name.toStdString());
        if (c.session) {
            meta->set_client_ip(
                QHostAddress(ntohl(c.session->client_ip)).toString().toStdString());
            meta->set_packet_count(uint32_t(c.session->packet_count));
        }
        // Zone + level come from the character's CURRENT decode managers
        // (managersForBox resolves the live session). Empty zone / zero level
        // serialize as proto defaults, so a not-yet-decoded character omits them.
        if (m_managerProvider) {
            if (const ManagerSet* ms = m_managerProvider->managersForBox(c.id)) {
                if (ms->zoneMgr)
                    meta->set_zone(ms->zoneMgr->shortZoneName().toStdString());
                // Only a promoted character (name set = OP_PlayerProfile decoded)
                // surfaces level; Player::level() returns the DefaultLevel pref (1)
                // for an undecoded box, which would mislabel placeholders as "L1".
                if (!c.name.isEmpty() && ms->player && ms->player->level() > 0)
                    meta->set_level(uint32_t(ms->player->level()));
            }
        }
    }
    upd->set_active_box_id(m_boxes->activeCharacterId().toStdString());
    sendOrBuffer(std::move(env));
}

void SessionAdapter::onInspectAnswer(const inspectDataStruct* data)
{
    if (!data) return;
    seq::v1::Envelope env;
    seq::encode::fillInspectAnswer(env.mutable_inspect_answer(), *data);
    sendOrBuffer(std::move(env));
}

void SessionAdapter::onLootDrops(uint32_t corpseId, const QString& corpseName,
                                 const QStringList& names,
                                 const QVector<uint32_t>& icons,
                                 const QVector<uint32_t>& itemIds)
{
    seq::v1::Envelope env;
    auto* ld = env.mutable_loot_drops();
    ld->set_corpse_id(corpseId);
    ld->set_corpse_name(corpseName.toStdString());
    for (int i = 0; i < names.size(); ++i)
    {
        auto* it = ld->add_items();
        it->set_name(names[i].toStdString());
        it->set_icon(i < icons.size() ? icons[i] : 0u);
        it->set_item_id(i < itemIds.size() ? itemIds[i] : 0u);
    }
    sendOrBuffer(std::move(env));
}
