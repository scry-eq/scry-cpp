/*
 * wire_eql.cpp — EverQuest Legends backend wiring TU.
 *
 * Definition of DaemonApp::wireBoxPipeline() for -DSEQ_TARGET=eql. Structurally
 * this is wire_live.cpp with five opcodes re-pointed at the backend-owned
 * EqlDispatch adapter (OP_PlayerProfile / OP_ClientUpdate / OP_NewZone /
 * OP_ZoneEntry / OP_MobUpdate). The S>C OP_ClientUpdate spawn-position wire is
 * kept (Legends' 28B playerSpawnPosStruct → EqlDispatch::playerUpdateOther),
 * just re-pointed at the backend adapter like the C>S self-position wire.
 * Every OTHER wire is carried over from Live unchanged: it only fires if a
 * Legends opcode id collides with a Live-named opcode present in conf/eql, and
 * the size checks keep mismatched payloads from dispatching. Install order is
 * golden-sensitive — do not reorder.
 *
 * Handlers register via EQPacketStream::on() as typed PacketHandlers (seqBind),
 * not Qt string SLOTs. EqlDispatch is a plain object owned by a shared_ptr the
 * wired closures capture — no QObject, no moc, no ODR pull into live/test.
 *
 * Cite opcodes by NAME, never by id — EQL rotates ids nearly every patch and
 * all 20 this file carried had gone stale. conf/eql/opcodes.toml is the map.
 */

#include "daemonapp.h"

#include <memory>

#include "combatrouter.h"
#include "datetimemgr.h"
#include "eqldispatch.h"        // backend/eql (same-dir include)
#include "everquest.h"
#include "group.h"
#include "guild.h"
#include "itemcache.h"
#include "messageshell.h"
#include "packet.h"        // EQStreamPairs, SP_World / SP_Zone
#include "packetcommon.h"
#include "packetinfo.h"
#include "packetstream.h"
#include "player.h"
#include "spawnshell.h"
#include "spellshell.h"
#include "zonemgr.h"
#include "zoneservermgr.h"

//----------------------------------------------------------------------
// DaemonApp::wireBoxPipeline (eql variant of wire_live.cpp)
void DaemonApp::wireBoxPipeline(EQPacketStream* worldC2S, EQPacketStream* worldS2C,
                                EQPacketStream* zoneC2S, EQPacketStream* zoneS2C,
                                const ManagerSet& ms, bool wireGlobalSinks)
{
    // Register one opcode→handler onto the stream(s) selected by (streamPair,
    // dir). The call sequence below is the exact concatenation of the old
    // wireZoneMgr() then wireSpawnShell(), and is golden-sensitive (shared
    // opcode+payload on one stream dispatches in install order) — do not
    // reorder. The handler is copied into each selected stream's dispatcher.
    auto wire = [&](const QString& op, EQStreamPairs sp, uint8_t dir,
                    const char* payload, EQSizeCheckType szt,
                    const PacketHandler& handler) {
        if (sp & SP_World) {
            if ((dir & DIR_Client) && worldC2S)
                worldC2S->on(op, payload, szt, handler);
            if ((dir & DIR_Server) && worldS2C)
                worldS2C->on(op, payload, szt, handler);
        }
        if (sp & SP_Zone) {
            if ((dir & DIR_Client) && zoneC2S)
                zoneC2S->on(op, payload, szt, handler);
            if ((dir & DIR_Server) && zoneS2C)
                zoneS2C->on(op, payload, szt, handler);
        }
    };
    auto lifecycle = [this](PacketHandler handler) {
        return [this, handler = std::move(handler)](
                   const uint8_t* data, size_t len, uint8_t dir) {
            if (!m_packet || m_packet->legacyLifecycleEnabledForCurrentPacket())
                handler(data, len, dir);
        };
    };
    auto entity = [this](PacketHandler handler) {
        return [this, handler = std::move(handler)](
                   const uint8_t* data, size_t len, uint8_t dir) {
            if (!m_packet || m_packet->legacyEntitiesEnabledForCurrentPacket())
                handler(data, len, dir);
        };
    };
    auto player = [this](PacketHandler handler) {
        return [this, handler = std::move(handler)](
                   const uint8_t* data, size_t len, uint8_t dir) {
            if (!m_packet || m_packet->legacyPlayersEnabledForCurrentPacket())
                handler(data, len, dir);
        };
    };
    auto playerAppearance = [this](PacketHandler handler) {
        return [this, handler = std::move(handler)](
                   const uint8_t* data, size_t len, uint8_t dir) {
            if (!m_packet ||
                m_packet->legacyPlayerAppearanceEnabledForCurrentPacket())
                handler(data, len, dir);
        };
    };

    // Backend-owned adapter holding the Legends handlers (never on the core
    // managers — see eqldispatch.h). Owned by a shared_ptr the wired closures
    // capture, so it lives as long as this box's stream dispatchers reference
    // it — no QObject parent needed.
    auto eql = std::make_shared<EqlDispatch>(
        ms.zoneMgr, ms.spawnShell, ms.player, m_dbStrings, ms.guildShell,
        m_guildMgr,
        [this] {
            return m_packet &&
                   !m_packet->legacyLifecycleEnabledForCurrentPacket();
        },
        [this](seq::shadow::LifecycleKind kind) {
            return m_packet &&
                   m_packet->rustLifecycleAcceptedForCurrentPacket(kind);
        },
        [this] {
            return m_packet &&
                   !m_packet->legacyPlayersEnabledForCurrentPacket();
        },
        [this](const seq::shadow::LifecycleProfile& profile) {
            if (!m_packet) return;
            m_packet->observeLegacyLifecycle(seq::shadow::observeSessionReset(
                seq::rust::EventSessionResetReason::PlayerProfile));
            m_packet->observeLegacyLifecycle(
                seq::shadow::observeProfile(profile));
        });

    // --- ZoneMgr: zone transitions + player profile.
    // (EQ Legends has no separate c2s OP_ZoneEntry: the 4606 c2s 92B is a
    // spawn-list request, acknowledged under OP_ZoneEntry below and NOT wired to
    // ZoneMgr::zoneEntryClient — on eql that would emit zoneBegin() and clear the
    // spawn list. Zone identity comes from OP_PlayerProfile + OP_NewZone.)
    // EQ Legends OP_PlayerProfile: header-only identity decode.
    wire("OP_PlayerProfile", SP_Zone, DIR_Server,
         "uint8_t", SZC_None,
         seqBind(eql, &EqlDispatch::profile));
    // eql's OP_ZoneChange is 484B and carries no zone id, so it cannot use
    // Live's 100B zoneChangeStruct (SZC_Match would drop it) nor ZoneMgr::
    // zoneChange, which decodes that struct. It only raises the zoning flag.
    wire("OP_ZoneChange", SP_Zone, DIR_Client | DIR_Server,
         "uint8_t", SZC_None,
         lifecycle(seqBind(eql, &EqlDispatch::zoneChange)));
    // EQ Legends OP_NewZone S>C: the authoritative current zone, carried
    // as packed short + long name text (parsed in EqlDispatch). Fires once per
    // zone-in, AFTER the profile + bulk spawn list — EqlDispatch::newZone drives
    // ZoneMgr::setZoneByName -> zoneResolved (map/filter/web, no spawn-clear /
    // player-reset). The old 0x4bc8 "OP_NewZone" carried only the BIND zone id
    // (identical across zones) and is not OP_NewZone. See OPCODES_LEGENDS.md.
    wire("OP_NewZone", SP_Zone, DIR_Server,
         "uint8_t", SZC_None,
         lifecycle(seqBind(eql, &EqlDispatch::newZone)));
    // EQ Legends OP_EnterWorld C>S, 72B: the client entering the world
    // with its character — fires at login AND on every in-place session re-entry
    // (private instance, or any zone that reuses the world socket, so BoxRegistry
    // keeps the SAME box and no active-box roll re-primes the web). EqlDispatch::
    // enterWorld resets the box on a re-entry so the new self-id re-adopts and
    // re-snapshots the client via changedID (a no-op at the initial login). Wired
    // per-box, NOT gated on wireGlobalSinks: it drives only
    // THIS box's SpawnShell/Player, so each box resets its own state on its own
    // re-handshake. NamePromoter also hooks this op (lower layer, to name the box)
    // — independent fan-out, both fire.
    wire("OP_EnterWorld", SP_World, DIR_Client,
         "uint8_t", SZC_None,
         seqBind(eql, &EqlDispatch::enterWorld));
    wire("OP_SendZonePoints", SP_Zone, DIR_Server,
         "zonePointsStruct", SZC_None,
         entity(seqBind(ms.zoneMgr, &ZoneMgr::zonePoints)));
    wire("OP_DzSwitchInfo", SP_Zone, DIR_Server,
         "dzSwitchInfo", SZC_None,
         seqBind(ms.zoneMgr, &ZoneMgr::dynamicZonePoints));
    wire("OP_DzInfo", SP_Zone, DIR_Server,
         "dzInfo", SZC_Match,
         seqBind(ms.zoneMgr, &ZoneMgr::dynamicZoneInfo));

    // Cross-manager: profile feeds Player too (after GroupMgr, which is
    // connected in buildManagerSet() — preserves slot fire order).
    connect(ms.zoneMgr, &ZoneMgr::playerProfile, this,
            [this, player = ms.player](const charProfileStruct* profile) {
        if (!m_packet || m_packet->legacyPlayersEnabledForCurrentPacket())
            player->player(profile);
    });

    // EQ Legends OP_ClientUpdate: C>S 38B self-position (was 42B pre-07/14).
    // Re-cracked 2026-07-14 against a /loc ground-truth capture: IEEE floats
    // gameX@14 / gameY@26 / gameZ@10 + heading@18 (13-bit @ bit 8), no spawnId. Size-gated
    // on playerSelfPosStruct (override = 38 = PAYLOAD_LEN); decode is Rust
    // (parse_player_self_pos, eql's own 38B copy). The 24B S>C other-spawn variant
    // is wired separately below (playerUpdateOther), at the Live playerUpdate slot
    // for fire order.
    wire("OP_ClientUpdate", SP_Zone, DIR_Client,
         "playerSelfPosStruct", SZC_Match,
         player(seqBind(eql, &EqlDispatch::playerUpdateSelf)));

    // OP_TimeOfDay / OP_ZoneServerInfo feed daemon-GLOBAL sinks. Only the
    // active box wires them — otherwise every box's (now unmuted) world/zone
    // stream would re-fire them, duplicating ZoneServer / EqTimeSync envelopes.
    //
    // (EQ Legends) OP_ItemPacket is NOT wired: the Legends bulk-item
    // format differs from Live's itemPacketStruct, so feeding it to the Live
    // itemCache would mis-parse. It's identified in conf/eql/opcodes.toml (for
    // recon labeling) but its decoder is future work — see OPCODES_LEGENDS.md.
    if (wireGlobalSinks) {
        wire("OP_TimeOfDay", SP_Zone, DIR_Server,
             "timeOfDayStruct", SZC_Match,
             lifecycle(seqBind(m_dateTimeMgr, &DateTimeMgr::timeOfDay)));
        wire("OP_ZoneServerInfo", SP_World, DIR_Server,
             "zoneServerInfoStruct", SZC_Match,
             lifecycle(seqBind(m_zoneServerMgr, &ZoneServerMgr::zoneServerInfo)));
    }

    // Guild id→name, feeding spawn guild tags via GuildMgr::guildTagUpdated.
    // Decoded in eql's OWN Rust parser (seq_backend_eql::guild_in_zone) through
    // EqlDispatch, then fed to the neutral GuildMgr::learnGuilds — the daemon
    // never reads guild wire bytes in C++, and eql never rides a shared parser,
    // even where the layout happens to match Live today.
    //
    // Per-box, NOT gated on wireGlobalSinks, even though GuildMgr itself is
    // daemon-wide: the roster is re-sent on every zone-in, and each zone-in
    // opens a fresh box, so gating this would silently drop every guild after
    // the first zone. Unlike the sinks above it publishes no envelope of its
    // own — it fills a map that ignores keys it already holds — so wiring it on
    // every box duplicates no output.
    wire("OP_GuildsInZoneList", SP_Zone, DIR_Server,
         "uint8_t", SZC_None,
         seqBind(eql, &EqlDispatch::guildsInZone));
    wire("OP_NewGuildInZone", SP_Zone, DIR_Server,
         "uint8_t", SZC_None,
         seqBind(eql, &EqlDispatch::newGuildInZone));
    // The roster is per-character (GuildShell lives in the ManagerSet), so it
    // wires per-box like the rest of the box pipeline.
    wire("OP_GuildMemberList", SP_Zone, DIR_Server,
         "uint8_t", SZC_None,
         seqBind(eql, &EqlDispatch::guildMemberList));
    wire("OP_GuildMOTD", SP_Zone, DIR_Server,
         "uint8_t", SZC_None,
         seqBind(eql, &EqlDispatch::guildMotd));

    // --- SpawnShell: spawn lifecycle + positions.
    // eql ground-item defs are makeDropStruct/none (variable name field);
    // szt matches conf/eql/opcodes.toml so the handler binds directly.
    wire("OP_GroundSpawn", SP_Zone, DIR_Server,
         "makeDropStruct", SZC_None,
         entity(seqBind(ms.spawnShell, &SpawnShell::newGroundItem)));
    // OP_ClickObject is dual-direction and BOTH sides are 12B remDropStruct
    // (confirmed 2026-08-06 on a drop/pickup capture): the S>C removal is what
    // we decode; the C>S side is the client's pickup REQUEST, carrying
    // {u32 dropId, u32 clickerSpawnId, u32 0} — the same drop id the preceding
    // OP_GroundSpawn announced. Registered as a no-op rather than decoded (the
    // removal already drives state). Left unregistered it would trip the
    // size-diagnostic on every click
    // ("dataLen: 16 doesn't match sizeof(remDropStruct):12"), so absorb it with a
    // no-op — the explicit form of "ignore C>S", matching legends' S>C-only handling.
    wire("OP_ClickObject", SP_Zone, DIR_Server,
         "remDropStruct", SZC_Match,
         entity(seqBind(ms.spawnShell, &SpawnShell::removeGroundItem)));
    wire("OP_ClickObject", SP_Zone, DIR_Client,
         "uint8_t", SZC_None,
         [](const uint8_t*, size_t, uint8_t) {});
    // OP_SpawnDoor: eql door rows are 132B (Live doorStruct is 136B);
    // seq-backend-eql's parse_door owns the 132B layout, the doorStruct size
    // override makes the modulus gate 132, and newDoorSpawns strides via
    // door_stride() — so the shared handler wires directly.
    wire("OP_SpawnDoor", SP_Zone, DIR_Server,
         "doorStruct", SZC_Modulus,
         entity(seqBind(ms.spawnShell, &SpawnShell::newDoorSpawns)));
    // EQ Legends OP_ZoneEntry: one spawn per payload (name + block);
    // EqlDispatch parses the variable-length name and the fixed spawn block.
    // Stock-SEQ name: the s2c OP_ZoneEntry has been the per-spawn payload since
    // 2008 (OP_ZoneSpawns is the dead bulk-array op). Replaces the leftover Live
    // SpawnShell::zoneEntry wire — that path calls m_player->update() on the PC
    // record, which eql routes through EqlDispatch::spawn/upsertSpawn instead.
    wire("OP_ZoneEntry", SP_Zone, DIR_Server,
         "uint8_t", SZC_None,
         entity(seqBind(eql, &EqlDispatch::spawn)));
    // EQ Legends OP_LoadoutSwap: a player's multiclass loadout change.
    // Variable-size (self ~118KB w/ inventory tail, broadcast ~490B) — hand-
    // decoded via decode_loadout_swap (reuses the ZoneEntry record parser), so
    // size-gate none. Self refreshes Player identity; broadcast updates a nearby
    // tracked spawn's class/level. See eqldispatch.cpp / OPCODES_LEGENDS.md.
    wire("OP_LoadoutSwap", SP_Zone, DIR_Server,
         "uint8_t", SZC_None,
         player(seqBind(eql, &EqlDispatch::loadoutSwap)));
    // EQ Legends OP_MobUpdate: per-mob position update (14B),
    // byte-identical to Live spawnPositionUpdate — size-gate on it directly;
    // decode via the shared Rust decode_mob_update.
    wire("OP_MobUpdate", SP_Zone, DIR_Server,
         "spawnPositionUpdate", SZC_Match,
         entity(seqBind(eql, &EqlDispatch::mobUpdate)));
    // EQ Legends OP_TargetMouse: C>S target select. The Legends payload
    // is byte-identical to Live's clientTargetStruct ({u32 spawn_id}, 0 = clear),
    // so it needs NO Legends glue — wire straight to the neutral core handler,
    // exactly as wire_live.cpp does. clientTarget emits targetSpawn -> Targeted
    // envelope -> web (untarget = spawn_id 0). Confirmed 2026-07-07; see
    // OPCODES_LEGENDS.md.
    wire("OP_TargetMouse", SP_Zone, DIR_Client,
         "clientTargetStruct", SZC_Match,
         seqBind(ms.spawnShell, &SpawnShell::clientTarget));
    // OP_WearChange is NAMED in conf/eql/opcodes.toml but deliberately
    // UNWIRED (l-patch addendum 3: stock's WearChange handler shows nothing for
    // equip changes). The two Live SpawnUpdateStruct/updateSpawnInfo bindings (kept
    // in wire_live.cpp) were removed here: eql's WearChange is a 32B {spawnId,
    // wearSlot, material} packet and sizeof(SpawnUpdateStruct)==32, so binding it
    // would SZC_Match and mis-decode wear bytes as a spawn update (spawn-state
    // corruption). Re-derive an eql-specific decoder if live equip tracking is wanted.
    // OP_SpawnAppearance2 type=0x2c = TLP mob-lock / FTE flag.
    wire("OP_SpawnAppearance2", SP_Zone, DIR_Server,
         "spawnAppearance2Struct", SZC_Match,
         seqBind(ms.spawnShell, &SpawnShell::updateSpawnLock));
    // Same opcode, eql-specific type numbering: type 6 = stand/sit/duck pose.
    // Dispatch is a fan-out, so this runs alongside the lock handler above and
    // each ignores the other's type.
    wire("OP_SpawnAppearance2", SP_Zone, DIR_Server,
         "spawnAppearance2Struct", SZC_Match,
         player(seqBind(eql, &EqlDispatch::spawnAppearance)));
    // OP_HPUpdate is eql's multiplexed stat-sync channel (u32 id + u8 flags +
    // per-stat payload), not Live's fixed hpNpcUpdateStruct — decode via
    // EqlDispatch, which passes the real packet length. Feeds spawn HP cur/max
    // (wide numeric or narrow percent) and the player's mana (wide form).
    wire("OP_HPUpdate", SP_Zone, DIR_Server,
         "uint8_t", SZC_None,
         player(seqBind(eql, &EqlDispatch::statSync)));
    wire("OP_MobHealth", SP_Zone, DIR_Server,
         "mobHealthStruct", SZC_Match,
         player(seqBind(ms.spawnShell, &SpawnShell::updateMobHealth)));

    // Player vitals — same opcodes also feed Player (filtered by self).
    wire("OP_ManaChange", SP_Zone, DIR_Server,
         "manaDecrementStruct", SZC_Match,
         player(seqBind(ms.player, &Player::manaChange)));
    wire("OP_Stamina", SP_Zone, DIR_Server,
         "staminaStruct", SZC_Match,
         seqBind(ms.player, &Player::updateStamina));
    // OP_Stance / OP_Invocation S>C echo: the player's active
    // swappable stance / invocation. 4B {u32 abilityId}, size-gated on the
    // shared activateAbilityStruct (size eql-owned via size_overrides). Resolved
    // to a display name in EqlDispatch and surfaced through PlayerStats.
    wire("OP_Stance", SP_Zone, DIR_Server,
         "activateAbilityStruct", SZC_Match,
         seqBind(eql, &EqlDispatch::stance));
    wire("OP_Invocation", SP_Zone, DIR_Server,
         "activateAbilityStruct", SZC_Match,
         seqBind(eql, &EqlDispatch::invocation));
    wire("OP_EndUpdate", SP_Zone, DIR_Server,
         "endUpdateStruct", SZC_Match,
         seqBind(ms.player, &Player::updateEndurance));
    // OP_Unknown4 is NOT money — once misnamed OP_MoneyUpdate, stays unwired.
    // OP_LootTransaction: subcode 7 = item sale coin, subcode 5 = corpse pile.
    wire("OP_LootTransaction", SP_Zone, DIR_Server,
         "uint8_t", SZC_None,
         seqBind(ms.messageShell, &MessageShell::lootTransaction));
    wire("OP_MoneyUpdate", SP_Zone, DIR_Server,
         "uint8_t", SZC_None,
         seqBind(eql, &EqlDispatch::moneyUpdate));
    // OP_ExpUpdate (16B expUpdateStruct): the regular exp bar. The ids
    // were cross-wired with OP_AAExpUpdate; corrected per the community
    // l-patch. exp@0 is 0-100000 permille — the same scale the daemon already
    // uses (reset()/loadProfile set m_minExp=0/m_maxExp=100000/m_tickExp=1), so
    // updateExp consumes it directly with no conversion. Routed through
    // EqlDispatch (not Player::updateExp direct) because eql has no discrete
    // level-up packet — expUpdate derives the ding from the exp wrap, then
    // forwards to Player::updateExp. See OP_LevelUpdate below + OPCODES_LEGENDS.md.
    wire("OP_ExpUpdate", SP_Zone, DIR_Server,
         "expUpdateStruct", SZC_Match,
         seqBind(eql, &EqlDispatch::expUpdate));
    // OP_AAExpUpdate (12B): u32 altexp (0-100000 per-AA-point
    // progress), u32 aapoints (unspent), u32 tail (unread; Live carries u8
    // percent + pad there, which updateAltExp ignores). First 8 bytes match
    // Live's altExpUpdateStruct and the scale is the same 0-100000 the
    // handler consumes natively (the old "needs a ->330 conversion" note
    // predated ac918ec's rescale), so it wires straight to the Live handler.
    wire("OP_AAExpUpdate", SP_Zone, DIR_Server,
         "altExpUpdateStruct", SZC_Match,
         seqBind(ms.player, &Player::updateAltExp));
    // OP_SendAATable (S>C): static AA ability-definition burst at zone-in
    // (one variable-length record per packet). EqlDispatch::sendAATable resolves
    // each record's descID -> titleSID -> dbstr type-1 name and records descID ->
    // name on Player, so protoencoder fills AAEntry.name (web AA window shows real
    // titles instead of "#<id>"). Variable size -> uint8_t/none. See eqldispatch.cpp.
    wire("OP_SendAATable", SP_Zone, DIR_Server,
         "uint8_t", SZC_None,
         seqBind(eql, &EqlDispatch::sendAATable));
    // OP_LevelUpdate: eql DOES have a discrete level packet — the older "no such
    // packet exists" finding is superseded (see OPCODES_LEGENDS.md). It is an 80B
    // widened container whose head is the stock levelUpUpdateStruct
    // (level@0, levelOld@4, exp@8), so SZC_Match would drop every one of them on
    // the size gate; SZC_None lets it through and updateLevel already clamps its
    // read to sizeof(levelUpUpdateStruct).
    wire("OP_LevelUpdate", SP_Zone, DIR_Server,
         "levelUpUpdateStruct", SZC_None,
         seqBind(ms.player, &Player::updateLevel));
    wire("OP_SkillUpdate", SP_Zone, DIR_Server,
         "skillIncStruct", SZC_Match,
         seqBind(ms.player, &Player::increaseSkill));
    // (OP_WearChange intentionally unwired on eql — see the note above OP_SpawnAppearance2.)
    wire("OP_DeleteSpawn", SP_Zone, DIR_Server | DIR_Client,
         "deleteSpawnStruct", SZC_Match,
         entity(seqBind(ms.spawnShell, &SpawnShell::deleteSpawn)));
    wire("OP_SpawnRename", SP_Zone, DIR_Server,
         "spawnRenameStruct", SZC_Match,
         entity(seqBind(ms.spawnShell, &SpawnShell::renameSpawn)));
    wire("OP_Illusion", SP_Zone, DIR_Server | DIR_Client,
         "spawnIllusionStruct", SZC_Match,
         playerAppearance(seqBind(ms.spawnShell, &SpawnShell::illusionSpawn)));
    // OP_SpawnAppearance carries eql's WIDE 24B record ({u32 spawnId, u32 type,
    // u32 value}), not Live's 8B struct, and its type numbering is its own — so
    // it must NOT go to SpawnShell::updateSpawnAppearance, which applies Live's
    // type semantics. It went there until 2026-07-30 and decoded nothing at all,
    // because the gate was inheriting Live's sizeof(spawnAppearanceStruct)=8
    // against a 24-byte wire. The gate size is now declared by
    // seq-backend-eql::size_overrides and the bytes go to eql's own handler.
    wire("OP_SpawnAppearance", SP_Zone, DIR_Server | DIR_Client,
         "spawnAppearanceStruct", SZC_Match,
         playerAppearance(seqBind(eql, &EqlDispatch::spawnAppearance)));
    // OP_Death: mob deaths take the shared Live corpse path, but the player's OWN
    // death needs eql-specific respawn handling (EQL respawns in-zone with a new
    // self-id and sends no player-reinit OP_ZoneEntry), so route through
    // EqlDispatch::death, which forwards non-player deaths to SpawnShell::killSpawn
    // and severs the self-id on the player's own death. See eqldispatch.cpp.
    wire("OP_Death", SP_Zone, DIR_Server,
         "newCorpseStruct", SZC_Match,
         player(seqBind(eql, &EqlDispatch::death)));
    wire("OP_Shroud", SP_Zone, DIR_Server,
         "spawnShroudSelf", SZC_None,
         seqBind(ms.spawnShell, &SpawnShell::shroudSpawn));
    wire("OP_RemoveSpawn", SP_Zone, DIR_Server | DIR_Client,
         "removeSpawnStruct", SZC_None,
         entity(seqBind(ms.spawnShell, &SpawnShell::removeSpawn)));
    // OP_Consider (24B both dirs): eql's considerStruct is its OWN 24B
    // struct (seq-backend-eql owns it; Live's is 32B), size-gated via the backend
    // size table (struct_size_overrides) — so SZC_Match validates the real 24B
    // with the real name, no uint8_t placeholder. parse_consider -> shared
    // Consider -> neutral SpawnShell::consMessage (spawnConsidered -> Considered
    // envelope -> web select-on-consider), the same path Live uses.
    wire("OP_Consider", SP_Zone, DIR_Server | DIR_Client,
         "considerStruct", SZC_Match,
         seqBind(ms.spawnShell, &SpawnShell::consMessage));
    // (OP_TargetMouse is wired once, DIR_Client, further up — the Live-copy
    // DIR_Server|DIR_Client duplicate double-registered the C>S handler.)
    wire("OP_NpcMoveUpdate", SP_Zone, DIR_Server,
         "uint8_t", SZC_None,
         entity(seqBind(ms.spawnShell, &SpawnShell::npcMoveUpdate)));
    // EQ Legends OP_ClientUpdate S>C, 24B: the position broadcast for
    // OTHER spawns. eql's playerSpawnPosStruct is 24B as of the 08/04 rotation
    // (was 28B on 07/29; 19-bit ×8 packed, x/y in the LOW bits of @4/@16 and z
    // in the HIGH bits of @12), so it's size-gated via the backend size table
    // (struct_size_overrides declares 24 = PAYLOAD_LEN)— SZC_Match with the real struct name,
    // no uint8_t placeholder. Decoded by eql's own parse_player_spawn_pos (24B)
    // → EqlDispatch → the neutral SpawnShell::moveSpawn, the same primitive
    // OP_MobUpdate uses (this is the exact slot Live wires SpawnShell::playerUpdate
    // — keep here for fire order). Position re-cracked 2026-07-14; see OPCODES_LEGENDS.md.
    wire("OP_ClientUpdate", SP_Zone, DIR_Server,
         "playerSpawnPosStruct", SZC_Match,
         entity(seqBind(eql, &EqlDispatch::playerUpdateOther)));
    wire("OP_CorpseLocResponse", SP_Zone, DIR_Server,
         "corpseLocStruct", SZC_Match,
         entity(seqBind(ms.spawnShell, &SpawnShell::corpseLoc)));

    // --- MessageShell: chat / system / NPC text.
    wire("OP_CommonMessage", SP_Zone, DIR_Client | DIR_Server,
         "channelMessageStruct", SZC_None,
         seqBind(ms.messageShell, &MessageShell::channelMessage));
    // EQL's OP_FormattedMessage diverges from the Live formattedMessageStruct
    // layout (format id @9, spell id @0, pre-split caret args) — decoded by the
    // eql parse_formatted_message and routed via formattedMessageEQL.
    wire("OP_FormattedMessage", SP_Zone, DIR_Server,
         "formattedMessageStruct", SZC_None,
         seqBind(ms.messageShell, &MessageShell::formattedMessageEQL));
    // OP_LootMessage: eql personal auto-loot/sell text.
    wire("OP_LootMessage", SP_Zone, DIR_Server,
         "uint8_t", SZC_None,
         seqBind(ms.messageShell, &MessageShell::lootMessage));
    // OP_LootDrops: corpse loot window.
    wire("OP_LootDrops", SP_Zone, DIR_Server,
         "uint8_t", SZC_None,
         seqBind(ms.messageShell, &MessageShell::lootDrops));
    wire("OP_SimpleMessage", SP_Zone, DIR_Server,
         "simpleMessageStruct", SZC_Match,
         seqBind(ms.messageShell, &MessageShell::simpleMessage));
    wire("OP_SpecialMesg", SP_Zone, DIR_Server,
         "specialMessageStruct", SZC_None,
         seqBind(ms.messageShell, &MessageShell::specialMessage));
    wire("OP_InspectAnswer", SP_Zone, DIR_Server,
         "inspectDataStruct", SZC_Match,
         seqBind(ms.messageShell, &MessageShell::inspectData));

    // --- GroupMgr. On Legends the roster identity rides OP_GroupUpdate (the
    // full authoritative roster), OP_GroupFollow (incremental join) and
    // OP_GroupDisband/2 (leave/disband). OP_GroupMemberList is dead here.
    wire("OP_GroupUpdate", SP_Zone, DIR_Server,
         "uint8_t", SZC_None,
         seqBind(ms.groupMgr, &GroupMgr::groupUpdate));
    wire("OP_GroupFollow", SP_Zone, DIR_Server,
         "groupFollowStruct", SZC_None,
         seqBind(ms.groupMgr, &GroupMgr::addGroupMember));
    wire("OP_GroupDisband", SP_Zone, DIR_Server,
         "groupDisbandStruct", SZC_None,
         seqBind(ms.groupMgr, &GroupMgr::removeGroupMember));
    wire("OP_GroupDisband2", SP_Zone, DIR_Server,
         "groupDisbandStruct", SZC_None,
         seqBind(ms.groupMgr, &GroupMgr::removeGroupMember));

    // --- SpellShell. (OP_SimpleMessage here is a SECOND receiver after
    // MessageShell above — order preserved.)
    // NOTE: OP_CastSpell is deliberately NOT wired to SpellShell. Inserting a
    // spell at cast start put it in the buff list ~cast-time before it landed
    // (premature "fading" on short self-buffs) AND added spells cast ON MOBS to
    // the player's own buff list. On EQL the authoritative player buff bar
    // arrives via OP_BuffList (below), so cast-start insertion is pure noise.
    wire("OP_Buff", SP_Zone, DIR_Server,
         "uint8_t", SZC_None,
         seqBind(ms.spellShell, &SpellShell::buff));
    // OP_BuffList/2/3 (73e2/713a/5c47, Xerxes 07/14 map): authoritative per-spawn
    // active-buff lists with real remaining durations. Same format + handler; the
    // handler resolves the owner by findability (self → buff panel, findable mob
    // → target effects).
    for (const char* op : {"OP_BuffList", "OP_BuffList2", "OP_BuffList3"})
      wire(op, SP_Zone, DIR_Server,
           "uint8_t", SZC_None,
           seqBind(ms.spellShell, &SpellShell::buffList));
    wire("OP_Action", SP_Zone, DIR_Server | DIR_Client,
         "actionStruct", SZC_Match,
         seqBind(ms.spellShell, &SpellShell::action));
    wire("OP_Action", SP_Zone, DIR_Server | DIR_Client,
         "actionAltStruct", SZC_Match,
         seqBind(ms.spellShell, &SpellShell::action));
    wire("OP_SimpleMessage", SP_Zone, DIR_Server,
         "simpleMessageStruct", SZC_Match,
         seqBind(ms.spellShell, &SpellShell::simpleMessage));

    // --- CombatRouter.
    wire("OP_Action2", SP_Zone, DIR_Client | DIR_Server,
         "action2Struct", SZC_Match,
         seqBind(ms.combatRouter, &CombatRouter::action2));
    // OP_BeginCast (S>C, 19B): a spawn started casting. Surfaced as a
    // transient cast indicator on the target (NOT a buff — cast-start buff
    // insertion was deliberately dropped, see OP_CastSpell note above). Gate
    // size 19 is eql-owned via seq-backend-eql size_overrides(beginCastStruct).
    wire("OP_BeginCast", SP_Zone, DIR_Server,
         "beginCastStruct", SZC_Match,
         seqBind(ms.combatRouter, &CombatRouter::beginCast));
}
