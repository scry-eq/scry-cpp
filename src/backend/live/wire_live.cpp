/*
 * wire_live.cpp — live/test backend wiring TU.
 *
 * Definition of DaemonApp::wireBoxPipeline() for the live/test targets. The
 * declaration stays in daemonapp.h and this is still a DaemonApp member, so
 * member access (m_itemCache / m_dateTimeMgr / m_zoneServerMgr) is unchanged.
 *
 * Handlers are registered with EQPacketStream::on() as typed PacketHandlers
 * (via seqBind on a manager method) instead of Qt string SLOTs — no moc, no
 * requirement that the receiver be a QObject slot. The install-order of the
 * wire() calls below is golden-sensitive (shared opcode+payload on one stream
 * dispatches in install order) — do not reorder.
 *
 * CMake compiles exactly one wiring TU per target: this one for live/test,
 * src/backend/eql/wire_eql.cpp for eql.
 *
 * NOTE: this file lives next to backend/live/everquest.h, so its
 * `#include "everquest.h"` resolves same-directory to Live's copy even in the
 * `test` build (where the core compiles against backend/test/everquest.h). That
 * is safe ONLY because this TU is struct-LAYOUT-independent — it names struct
 * types in payload strings / signal signatures but never takes sizeof() or reads
 * a field. Keep it that way: do not add layout-dependent code here, or the test
 * build would silently use Live's layout for it.
 */

#include "daemonapp.h"

#include "combatrouter.h"
#include "datetimemgr.h"
#include "everquest.h"
#include "group.h"
#include "guild.h"
#include "guildshell.h"
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
// DaemonApp::wireBoxPipeline
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
    auto progression = [this](PacketHandler handler) {
        return [this, handler = std::move(handler)](
                   const uint8_t* data, size_t len, uint8_t dir) {
            if (!m_packet ||
                m_packet->legacyProgressionEnabledForCurrentPacket())
                handler(data, len, dir);
        };
    };
    ms.player->setProgressionMutationGuard([this] {
        return !m_packet ||
               m_packet->legacyProgressionEnabledForCurrentPacket();
    });

    // --- ZoneMgr: zone transitions + player profile.
    wire("OP_ZoneEntry", SP_Zone, DIR_Client,
         "ClientZoneEntryStruct", SZC_Match,
         seqBind(ms.zoneMgr, &ZoneMgr::zoneEntryClient));
    ms.zoneMgr->setRustLifecycleProbe([this] {
        return m_packet && !m_packet->legacyLifecycleEnabledForCurrentPacket();
    });
    ms.zoneMgr->setRustProfileAcceptedProbe([this] {
        return m_packet && m_packet->rustLifecycleAcceptedForCurrentPacket(
            seq::shadow::LifecycleKind::PlayerProfile);
    });
    wire("OP_PlayerProfile", SP_Zone, DIR_Server,
         "uint8_t", SZC_None,
         seqBind(ms.zoneMgr, &ZoneMgr::zonePlayer));
    wire("OP_ZoneChange", SP_Zone, DIR_Client | DIR_Server,
         "zoneChangeStruct", SZC_Match,
         lifecycle(seqBind(ms.zoneMgr, &ZoneMgr::zoneChange)));
    wire("OP_NewZone", SP_Zone, DIR_Server,
         "uint8_t", SZC_None,
         lifecycle(seqBind(ms.zoneMgr, &ZoneMgr::zoneNew)));
    wire("OP_SendZonePoints", SP_Zone, DIR_Server,
         "zonePointsStruct", SZC_None,
         entity(seqBind(ms.zoneMgr, &ZoneMgr::zonePoints)));
    wire("OP_DzSwitchInfo", SP_Zone, DIR_Server,
         "dzSwitchInfo", SZC_None,
         seqBind(ms.zoneMgr, &ZoneMgr::dynamicZonePoints));
    wire("OP_DzInfo", SP_Zone, DIR_Server,
         "dzInfo", SZC_Match,
         seqBind(ms.zoneMgr, &ZoneMgr::dynamicZoneInfo));

    // OP_GuildMOTD — the player's guild message of the day. Per-character
    // (GuildShell lives in the ManagerSet), so per-box, not a global sink.
    // Decoded in seq-decode (Live's own guild_motd parser) inside the handler;
    // no C++ struct cast. GuildShell stamps the guild id from its roster.
    wire("OP_GuildMOTD", SP_Zone, DIR_Server,
         "guildMOTDStruct", SZC_None,
         seqBind(ms.guildShell, &GuildShell::guildMOTD));
    // OP_GuildMemberList — the guild roster. Variable-length (a member list with
    // per-member LPText names/notes), so no fixed struct: uint8_t/none, decoded
    // in seq-decode's stock-layout parser inside the handler.
    wire("OP_GuildMemberList", SP_Zone, DIR_Server,
         "uint8_t", SZC_None,
         seqBind(ms.guildShell, &GuildShell::guildRoster));
    // OP_GuildMemberUpdate — a member's zone/last-on (online status), a fixed
    // 88B GuildMemberUpdate struct. Decoded in seq-decode via the eqstructs
    // binding; the handler updates the tracked member + re-emits the roster.
    wire("OP_GuildMemberUpdate", SP_Zone, DIR_Server,
         "GuildMemberUpdate", SZC_Match,
         seqBind(ms.guildShell, &GuildShell::memberZoneUpdate));
    // OP_ExpandedGuildInfo — tagged union; its rank-name action builds the
    // guild's rank -> label table (arrives per-rank right after the roster).
    // Variable-size, so uint8_t/none + a seq-decode parser in the handler.
    wire("OP_ExpandedGuildInfo", SP_Zone, DIR_Server,
         "uint8_t", SZC_None,
         seqBind(ms.guildShell, &GuildShell::expandedGuildInfo));
    // OP_GuildsInZoneList / OP_NewGuildInZone — the guild id->name source that
    // puts guild tags on spawns (fed to the daemon-wide GuildMgr). Decoded in
    // seq-decode. Per-box (each zone-in re-sends the in-zone set); GuildMgr
    // ignores keys it already holds, so it duplicates no envelope. NewGuildInZone
    // is ffff on Live (legacy doesn't map it) so it's inert until its id is found.
    wire("OP_GuildsInZoneList", SP_Zone, DIR_Server,
         "uint8_t", SZC_None,
         seqBind(m_guildMgr, &GuildMgr::guildsInZone));
    wire("OP_NewGuildInZone", SP_Zone, DIR_Server,
         "uint8_t", SZC_None,
         seqBind(m_guildMgr, &GuildMgr::newGuildInZone));

    // Cross-manager: profile feeds Player too (after GroupMgr, which is
    // connected in buildManagerSet() — preserves slot fire order). This is an
    // inter-manager Qt signal (ZoneMgr and Player stay QObjects), not a packet
    // dispatch.
    connect(ms.zoneMgr, &ZoneMgr::playerProfile, this,
            [this, player = ms.player](const charProfileStruct* profile) {
        if (!m_packet || m_packet->legacyPlayersEnabledForCurrentPacket())
            player->player(profile);
    });
    connect(ms.zoneMgr, SIGNAL(playerProfileSupplement(const charProfileStruct*)),
            ms.player,  SLOT(applyProfileSupplement(const charProfileStruct*)));

    // OP_ClientUpdate DIR_Client = this player's movement (playerSelfPos);
    // the DIR_Server playerSpawnPos variant goes to SpawnShell below.
    wire("OP_ClientUpdate", SP_Zone, DIR_Server | DIR_Client,
         "playerSelfPosStruct", SZC_Match,
         player(seqBind(ms.player, &Player::playerUpdateSelf)));

    // OP_ItemPacket / OP_TimeOfDay / OP_ZoneServerInfo feed daemon-GLOBAL
    // sinks. Only the active box wires them — otherwise every box's
    // (now unmuted) world/zone stream would re-fire them, duplicating
    // ZoneServer / EqTimeSync / ItemLearned envelopes to the client.
    if (wireGlobalSinks) {
        wire("OP_ItemPacket", SP_Zone, DIR_Server,
             "itemPacketStruct", SZC_None,
             progression(seqBind(m_itemCache, &ItemCache::onItemPacket)));
        wire("OP_TimeOfDay", SP_Zone, DIR_Server,
             "timeOfDayStruct", SZC_Match,
             lifecycle(seqBind(m_dateTimeMgr, &DateTimeMgr::timeOfDay)));
        wire("OP_ZoneServerInfo", SP_World, DIR_Server,
             "zoneServerInfoStruct", SZC_Match,
             lifecycle(seqBind(m_zoneServerMgr, &ZoneServerMgr::zoneServerInfo)));
    }

    // --- SpawnShell: spawn lifecycle + positions.
    wire("OP_GroundSpawn", SP_Zone, DIR_Server,
         "makeDropStruct", SZC_None,
         entity(seqBind(ms.spawnShell, &SpawnShell::newGroundItem)));
    wire("OP_ClickObject", SP_Zone, DIR_Server,
         "remDropStruct", SZC_Match,
         entity(seqBind(ms.spawnShell, &SpawnShell::removeGroundItem)));
    wire("OP_SpawnDoor", SP_Zone, DIR_Server,
         "doorStruct", SZC_Modulus,
         entity(seqBind(ms.spawnShell, &SpawnShell::newDoorSpawns)));
    wire("OP_ZoneEntry", SP_Zone, DIR_Server,
         "uint8_t", SZC_None,
         entity(seqBind(ms.spawnShell, &SpawnShell::zoneEntry)));
    wire("OP_MobUpdate", SP_Zone, DIR_Server | DIR_Client,
         "spawnPositionUpdate", SZC_Match,
         entity(seqBind(ms.spawnShell, &SpawnShell::updateSpawns)));
    wire("OP_WearChange", SP_Zone, DIR_Server,
         "wearChangeStruct", SZC_None,
         seqBind(ms.spawnShell, &SpawnShell::updateSpawnInfo));
    // OP_SpawnAppearance2 type=0x2c = TLP mob-lock / FTE flag.
    wire("OP_SpawnAppearance2", SP_Zone, DIR_Server,
         "spawnAppearance2Struct", SZC_Match,
         seqBind(ms.spawnShell, &SpawnShell::updateSpawnLock));
    wire("OP_HPUpdate", SP_Zone, DIR_Server | DIR_Client,
         "hpNpcUpdateStruct", SZC_Match,
         player(seqBind(ms.spawnShell, &SpawnShell::updateNpcHP)));
    wire("OP_MobHealth", SP_Zone, DIR_Server,
         "mobHealthStruct", SZC_Match,
         player(seqBind(ms.spawnShell, &SpawnShell::updateMobHealth)));

    // Player vitals — same opcodes also feed Player (filtered by self).
    // SpawnShell's OP_HPUpdate/OP_WearChange wires above MUST precede
    // these (shared opcode+payload on the same stream dispatches in order).
    wire("OP_HPUpdate", SP_Zone, DIR_Server | DIR_Client,
         "hpNpcUpdateStruct", SZC_Match,
         player(seqBind(ms.player, &Player::updateNpcHP)));
    wire("OP_ManaChange", SP_Zone, DIR_Server,
         "manaDecrementStruct", SZC_Match,
         player(seqBind(ms.player, &Player::manaChange)));
    wire("OP_Stamina", SP_Zone, DIR_Server,
         "staminaStruct", SZC_Match,
         seqBind(ms.player, &Player::updateStamina));
    wire("OP_EndUpdate", SP_Zone, DIR_Server,
         "endUpdateStruct", SZC_Match,
         seqBind(ms.player, &Player::updateEndurance));
    wire("OP_ExpUpdate", SP_Zone, DIR_Server,
         "expUpdateStruct", SZC_Match,
         progression(seqBind(ms.player, &Player::updateExp)));
    // Player::updateAltExp survived the showeq-c extraction but its wiring
    // didn't — the AA bar only refreshed at zone-in (charProfile) until this
    // was re-wired 2026-07-13.
    wire("OP_AAExpUpdate", SP_Zone, DIR_Server,
         "altExpUpdateStruct", SZC_Match,
         progression(seqBind(ms.player, &Player::updateAltExp)));
    wire("OP_LevelUpdate", SP_Zone, DIR_Server,
         "levelUpUpdateStruct", SZC_Match,
         progression(seqBind(ms.player, &Player::updateLevel)));
    wire("OP_SkillUpdate", SP_Zone, DIR_Server,
         "skillIncStruct", SZC_Match,
         progression(seqBind(ms.player, &Player::increaseSkill)));
    wire("OP_WearChange", SP_Zone, DIR_Server,
         "wearChangeStruct", SZC_None,
         seqBind(ms.player, &Player::updateSpawnInfo));
    wire("OP_DeleteSpawn", SP_Zone, DIR_Server | DIR_Client,
         "deleteSpawnStruct", SZC_Match,
         entity(seqBind(ms.spawnShell, &SpawnShell::deleteSpawn)));
    wire("OP_SpawnRename", SP_Zone, DIR_Server,
         "spawnRenameStruct", SZC_Match,
         entity(seqBind(ms.spawnShell, &SpawnShell::renameSpawn)));
    wire("OP_Illusion", SP_Zone, DIR_Server | DIR_Client,
         "spawnIllusionStruct", SZC_Match,
         playerAppearance(seqBind(ms.spawnShell, &SpawnShell::illusionSpawn)));
    wire("OP_SpawnAppearance", SP_Zone, DIR_Server | DIR_Client,
         "spawnAppearanceStruct", SZC_Match,
         playerAppearance(
             seqBind(ms.spawnShell, &SpawnShell::updateSpawnAppearance)));
    wire("OP_Death", SP_Zone, DIR_Server,
         "newCorpseStruct", SZC_Match,
         player(seqBind(ms.spawnShell, &SpawnShell::killSpawn)));
    wire("OP_Shroud", SP_Zone, DIR_Server,
         "spawnShroudSelf", SZC_None,
         seqBind(ms.spawnShell, &SpawnShell::shroudSpawn));
    wire("OP_RemoveSpawn", SP_Zone, DIR_Server | DIR_Client,
         "removeSpawnStruct", SZC_None,
         entity(seqBind(ms.spawnShell, &SpawnShell::removeSpawn)));
    wire("OP_Consider", SP_Zone, DIR_Server | DIR_Client,
         "considerStruct", SZC_Match,
         seqBind(ms.spawnShell, &SpawnShell::consMessage));
    wire("OP_TargetMouse", SP_Zone, DIR_Server | DIR_Client,
         "clientTargetStruct", SZC_Match,
         seqBind(ms.spawnShell, &SpawnShell::clientTarget));
    wire("OP_NpcMoveUpdate", SP_Zone, DIR_Server,
         "uint8_t", SZC_None,
         entity(seqBind(ms.spawnShell, &SpawnShell::npcMoveUpdate)));
    wire("OP_ClientUpdate", SP_Zone, DIR_Server,
         "playerSpawnPosStruct", SZC_Match,
         entity(seqBind(ms.spawnShell, &SpawnShell::playerUpdate)));
    wire("OP_CorpseLocResponse", SP_Zone, DIR_Server,
         "corpseLocStruct", SZC_Match,
         entity(seqBind(ms.spawnShell, &SpawnShell::corpseLoc)));

    // --- MessageShell: chat / system / NPC text.
    wire("OP_CommonMessage", SP_Zone, DIR_Client | DIR_Server,
         "channelMessageStruct", SZC_None,
         seqBind(ms.messageShell, &MessageShell::channelMessage));
    wire("OP_FormattedMessage", SP_Zone, DIR_Server,
         "formattedMessageStruct", SZC_None,
         seqBind(ms.messageShell, &MessageShell::formattedMessage));
    wire("OP_SimpleMessage", SP_Zone, DIR_Server,
         "simpleMessageStruct", SZC_Match,
         seqBind(ms.messageShell, &MessageShell::simpleMessage));
    wire("OP_SpecialMesg", SP_Zone, DIR_Server,
         "specialMessageStruct", SZC_None,
         seqBind(ms.messageShell, &MessageShell::specialMessage));
    wire("OP_InspectAnswer", SP_Zone, DIR_Server,
         "inspectDataStruct", SZC_Match,
         seqBind(ms.messageShell, &MessageShell::inspectData));

    // --- GroupMgr.
    wire("OP_GroupUpdate", SP_Zone, DIR_Server,
         "uint8_t", SZC_None,
         seqBind(ms.groupMgr, &GroupMgr::groupUpdate));
    wire("OP_GroupMemberList", SP_Zone, DIR_Server,
         "uint8_t", SZC_None,
         seqBind(ms.groupMgr, &GroupMgr::groupMemberList));
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
    // the player's own buff list. Buffs are surfaced only when they actually
    // apply: OP_Buff (form 1/2) / OP_Action (target == player) here on Live,
    // OP_BuffList on EQL — all player-scoped.
    wire("OP_Buff", SP_Zone, DIR_Server,
         "uint8_t", SZC_None,
         seqBind(ms.spellShell, &SpellShell::buff));
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
}
