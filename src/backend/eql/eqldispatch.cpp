/*
 * eqldispatch.cpp — EverQuest Legends packet dispatch adapter.
 *
 * eql owns no wire-struct types: the Legends wire is decoded in Rust
 * (showeq-decoder-rs, `backend-eql` feature → `seq-backend-eql`) via the same
 * uniform `seq::rust::decode_*` surface Live uses. This adapter reads the decoded
 * fields and drives the core managers' target-NEUTRAL primitives (setIdentity /
 * applySelfPosition / setZoneByName / upsertSpawn / moveSpawn), so the frontend
 * (SpawnShell / Player / ZoneMgr) keeps only Live's struct set and nothing named
 * `legends*` exists in core. The per-field offsets/scales live in the Rust
 * parsers (the `seq-backend-eql` crate); see OPCODES_LEGENDS.md for the evidence.
 * Layout shuffles per patch — re-derive the Rust parser from captures.
 */
#include "eqldispatch.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

#include <QString>

#include "seq-bridge-cxx/lib.h"

#include "dbstrings.h"
#include "diagnosticmessages.h"
#include "guild.h"
#include "guildshell.h"
#include "packetcommon.h"   // DIR_Client / DIR_Server
#include "player.h"
#include "spawnshell.h"
#include "zonemgr.h"

namespace {
inline QString latin1(const rust::String& s)
{
    return QString::fromLatin1(s.data(), int(s.size()));
}

// Resolve an EQ Legends STANCE ability id to its display name. The id is a
// stable client enum (eqgame.exe GetAbilityName switch); the OPCODE id is
// patch-volatile and lives in the toml. Unknown ids fall through to the caller.
QString stanceName(uint32_t id)
{
    switch (id) {
    case 117: return QStringLiteral("Offense");
    case 118: return QStringLiteral("Defense");
    case 119: return QStringLiteral("Evasive");
    case 120: return QStringLiteral("Balanced");
    case 121: return QStringLiteral("Mage Hunter");
    case 122: return QStringLiteral("Striker");
    case 123: return QStringLiteral("Berserker");
    case 124: return QStringLiteral("Ranged");
    case 135: return QStringLiteral("Channeler");
    default:  return QString();
    }
}

// Resolve an EQ Legends INVOCATION ability id to its display name (same stable
// eqgame.exe GetAbilityName enum). Unknown ids fall through to the caller.
QString invocationName(uint32_t id)
{
    switch (id) {
    case 125: return QStringLiteral("Recover");
    case 126: return QStringLiteral("Empower");
    case 127: return QStringLiteral("Inversion");
    case 128: return QStringLiteral("Spell Blade");
    case 129: return QStringLiteral("Over Channel");
    case 130: return QStringLiteral("Inviolable");
    case 131: return QStringLiteral("Divine");
    case 132: return QStringLiteral("Chained");
    case 133: return QStringLiteral("Arcane Mastery");
    case 134: return QStringLiteral("Unyielding");
    default:  return QString();
    }
}
}

EqlDispatch::EqlDispatch(ZoneMgr* zoneMgr, SpawnShell* spawnShell, Player* player,
                         DbStrings* dbStrings, GuildShell* guildShell, GuildMgr* guildMgr)
    : m_zoneMgr(zoneMgr)
    , m_spawnShell(spawnShell)
    , m_player(player)
    , m_dbStrings(dbStrings)
    , m_guildShell(guildShell)
    , m_guildMgr(guildMgr)
    , m_selfTracker(seq::rust::eql_self_tracker_new())
{
}

// Shared tail for both guild-in-zone opcodes: turn the decoded rows into neutral
// entries and hand them to GuildMgr. The wire is decoded in seq-backend-eql; no
// guild bytes are read in C++.
static void applyGuildRows(GuildMgr* mgr,
                           const rust::Vec<seq::rust::GuildInZoneRow>& rows)
{
    if (!mgr || rows.empty())
        return;
    QVector<GuildInZoneEntry> out;
    out.reserve(int(rows.size()));
    for (const auto& r : rows)
    {
        GuildInZoneEntry e;
        e.guildId  = r.guild_id;
        e.serverId = r.server_id;
        e.name     = latin1(r.name);
        out.append(e);
    }
    mgr->learnGuilds(out);
}

void EqlDispatch::guildsInZone(const uint8_t* data, size_t len, uint8_t dir)
{
    if (dir != DIR_Server)
        return;
    applyGuildRows(m_guildMgr,
                   seq::rust::decode_guilds_in_zone_list(
                       rust::Slice<const uint8_t>{data, len}));
}

void EqlDispatch::newGuildInZone(const uint8_t* data, size_t len, uint8_t dir)
{
    if (dir != DIR_Server)
        return;
    applyGuildRows(m_guildMgr,
                   seq::rust::decode_new_guild_in_zone(
                       rust::Slice<const uint8_t>{data, len}));
}

void EqlDispatch::guildMemberList(const uint8_t* data, size_t len, uint8_t dir)
{
    // OP_GuildMemberList S>C: the whole roster, authoritative and replacing.
    // Decoded in Rust because the eql layout diverges from the stock struct on
    // three counts — a header one field wider, the multiclass bitmask sitting in
    // the class slot, and a trailing zone id live never read.
    if (dir != DIR_Server || !m_guildShell)
        return;
    auto rows = seq::rust::decode_guild_roster(rust::Slice<const uint8_t>{data, len});
    // Empty means the parse failed its end-of-payload canary (or the guild is
    // empty). Either way, don't wipe a good roster over it.
    if (rows.empty())
        return;

    QVector<GuildRosterEntry> out;
    out.reserve(int(rows.size()));
    for (const auto& r : rows)
    {
        GuildRosterEntry e;
        e.name       = latin1(r.name);
        e.level      = (uint8_t)r.level;
        e.classVal   = r.primary_class;
        e.classMask  = r.class_mask;
        e.guildRank  = r.rank;
        e.lastOn     = r.last_on;
        e.banker     = r.banker;
        e.alt        = r.alt;
        e.fullMember = r.full_member;
        e.publicNote = latin1(r.public_note);
        e.zoneId     = r.zone_id;
        out.append(e);
    }
    m_guildShell->setRoster(rows[0].guild_id, out);
}

void EqlDispatch::guildMotd(const uint8_t* data, size_t len, uint8_t dir)
{
    // OP_GuildMOTD S>C: message + who set it (both empty when the guild has none).
    // The packet has no guild id; GuildShell associates it with the roster's.
    if (dir != DIR_Server || !m_guildShell)
        return;
    auto m = seq::rust::decode_guild_motd(rust::Slice<const uint8_t>{data, len});
    if (!m.ok)
        return;
    m_guildShell->setMotd(latin1(m.message), latin1(m.sender));
}

void EqlDispatch::applySelfVitals(const seq::rust::SelfStat& v)
{
    if (!m_player || !(v.has_hp || v.has_mana || v.has_end))
        return;
    Player::Vitals out;
    out.haveHP = v.has_hp;
    out.hpCur = (uint32_t)v.hp_cur;
    out.hpMax = (uint32_t)v.hp_max;
    out.haveMana = v.has_mana;
    out.manaCur = (uint32_t)v.mana_cur;
    out.manaMax = (uint32_t)v.mana_max;
    out.haveEnd = v.has_end;
    out.endCur = (uint32_t)v.end_cur;
    out.endMax = (uint32_t)v.end_max;
    // A wide zero-max packet would blank the bars — drop those stats.
    if (out.haveHP && out.hpMax == 0) out.haveHP = false;
    m_player->setVitals(out);
}

void EqlDispatch::profile(const uint8_t* data, size_t len, uint8_t dir)
{
    // OP_PlayerProfile S>C: identity header (race/class/level) + the character
    // NAME, located by the Rust parser's absolute anchor-scan (the u32 64 + name
    // + u32 32 + surname block signature — robust vs the old fixed deep offset,
    // empty only when the block can't be found). The parser also recovers the
    // surname + current zone/position/guild, but those stay unused here: the
    // current zone comes from OP_NewZone (newZone() below) and position from
    // OP_ClientUpdate, both authoritative.
    if (dir != DIR_Server)
        return;
    auto out = seq::rust::decode_player_profile(
        rust::Slice<const uint8_t>{data, len});
    if (!out.ok)
        return;

    // The per-zone spawn reset. eql sends this once per zone-in and BEFORE the
    // OP_ZoneEntry burst, which is what makes it the usable trigger: OP_NewZone
    // lands after the burst and would wipe it. Previously the only reset was
    // the fresh box BoxRegistry mints per zone session, so a session whose
    // world handshake was never captured (no OP_ZoneServerInfo -> zone sessions
    // bound "by world recency") stopped resetting entirely and piled every zone
    // into one list — 1900+ spawns and stale records under recycled ids.
    m_spawnShell->clear();
    m_player->setID(0);
    m_selfTracker->reset();
    // Name first: setPlayerName only stores it (+ signals the box picker); the
    // setIdentity() below then emits changeItem(tSpawnChangedALL) carrying the
    // new name. An empty name (anchor block not found) leaves box-naming to
    // own-spawn adoption (SpawnShell::playerChangedID), the prior source.
    QString name = latin1(out.name);
    if (!name.isEmpty())
        m_player->setPlayerName(name);
    // Seed the whole skill array (walked out of the profile in seq-backend-eql)
    // BEFORE setIdentity: setIdentity's levelChanged triggers the coalesced
    // PlayerStats snapshot, so the seeded skills ride that first snapshot and the
    // Skills window is populated at zone-in. Empty on a short-read (skills then
    // fall back to incremental OP_SkillUpdate).
    if (!out.skills.empty())
    {
        std::vector<uint32_t> skills(out.skills.begin(), out.skills.end());
        m_player->seedSkills(skills);
    }
    // Seed the purchased-AA list + spent points from the same profile walk
    // (parallel aa_ids/aa_values) so the AA window populates at zone-in.
    if (!out.aa_ids.empty())
    {
        std::vector<uint32_t> aaIds(out.aa_ids.begin(), out.aa_ids.end());
        std::vector<uint32_t> aaVals(out.aa_values.begin(), out.aa_values.end());
        m_player->seedPurchasedAA(aaIds, aaVals, out.aa_spent);
    }
    // Seed the initial EQL stance / invocation from the profile (fixed offset,
    // read in seq-backend-eql). OP_Stance/OP_Invocation only echo on a SWAP, so
    // without this the readout stays blank until the player swaps. Only set when
    // the id resolves to a known ability (0 / out-of-range = none, or drift).
    if (QString sn = stanceName(out.stance); !sn.isEmpty())
        m_player->setStance(sn);
    if (QString invName = invocationName(out.invocation); !invName.isEmpty())
        m_player->setInvocation(invName);
    m_player->setIdentity((uint16_t)out.race, (uint8_t)out.class_, out.level);
    m_player->setClassMask(out.class_mask);   // EQL multiclass (bit N = class N)
    // Carried coin (fixed offset, read in seq-backend-eql). Redundant with
    // OP_MoneyUpdate, which carries the same values in a far more durable 20B
    // struct — kept as a second source so a drifted profile offset or a missing
    // money broadcast can't leave the readout empty. Must come AFTER
    // setIdentity: moneyChanged fires its own PlayerStats snapshot, and emitting
    // it earlier publishes one carrying the still-default identity.
    m_player->setMoneyCoins(out.platinum, out.gold, out.silver, out.copper);
}

void EqlDispatch::zoneChange(const uint8_t*, size_t, uint8_t)
{
    // eql's OP_ZoneChange is a 484B CLIENT request carrying the player's
    // position and no zone id at all, so unlike Live there is nothing to decode
    // — only the zoning flag is knowable. Zone identity arrives later via
    // OP_NewZone -> setZoneByName, which also lowers the flag.
    //
    // The flag gates SpawnShell's stale-zone handlers. On eql only
    // npcMoveUpdate and the ground-item pair are wired behind it, and the bulk
    // spawn stream is NOT (OP_ZoneEntry -> EqlDispatch::spawn -> upsertSpawn is
    // unguarded), so raising it suppresses stale movement without touching the
    // zone's spawn list. Measured over one 26-minute capture: 29 of 3880
    // OP_NpcMoveUpdate and 0 of 23 OP_GroundSpawn fall inside the windows.
    m_zoneMgr->beginZoning();
}

void EqlDispatch::moneyUpdate(const uint8_t* data, size_t len, uint8_t dir)
{
    // OP_MoneyUpdate (0x6414) S>C: the authoritative carried purse. Broadcast at
    // zone-in and occasionally between, but never per coin-earning event — the
    // between-zone accrual rides OP_LootMessage text instead.
    if (dir != DIR_Server)
        return;
    auto out = seq::rust::decode_money_update(
        rust::Slice<const uint8_t>{data, len});
    if (!out.ok)
        return;
    m_player->setMoneyCoins(out.platinum, out.gold, out.silver, out.copper);
}

void EqlDispatch::spawnAppearance(const uint8_t* data, size_t len, uint8_t dir)
{
    // 24B {u32 spawnId, u32 type, u32 value, u8[12]}. Both directions carry the
    // same struct. Type numbering is eql's own; only type 6 is confirmed —
    // wire-verified against scripted pose toggles (110 sit / 100 stand / 111
    // duck) and independently corroborated by legacy showeq's spawnEventEQL.
    //
    // Everything else is left alone deliberately. One 26-minute capture saw
    // types 22 (193x, always 0 — periodic tick), 36 (51x, 0/1/3/4), 43 (47x,
    // mostly 1), 11 (29x, always 0), 41 (22x, carries a unix timestamp), 26,
    // 3, 7, 8, 5, 1, 15, 35 — none with a confirmed meaning or a spawn field to
    // land in. Type 13 (anon, per legacy) did not appear at all. Guessing here
    // would put wrong state on spawns, so they are consumed silently until a
    // targeted capture pins them down.
    //
    // Type 0x2c (mob lock) is handled separately by SpawnShell::updateSpawnLock,
    // which is wired to this same opcode and shared with Live.
    //
    // Decoded by eql's OWN Rust parser rather than a cast through Live's
    // spawnAppearance2Struct: the two happen to share a layout today, but a Live
    // struct edit must not be able to reach eql's wire (and this handler now also
    // serves OP_SpawnAppearance, whose Live struct is a different shape entirely).
    (void)dir;
    auto out = seq::rust::decode_spawn_appearance(
        rust::Slice<const uint8_t>{data, len});
    if (!out.ok)
        return;
    if (out.kind != 6)
        return;
    m_spawnShell->updateSpawnAnimation((uint16_t)out.spawn_id,
                                       (uint8_t)out.parameter);
}

void EqlDispatch::expUpdate(const uint8_t* data, size_t len, uint8_t dir)
{
    // OP_ExpUpdate: the regular exp bar.
    //
    // This used to also DERIVE level-ups here, by treating an exp decrease as a
    // ding — a heuristic adopted only because eql was believed to have no
    // discrete level packet. It does (OP_LevelUpdate; see wire_eql.cpp and
    // OPCODES_LEGENDS.md), and that packet carries an absolute level, so the
    // guess is now both redundant and capable of double-counting a ding against
    // the authoritative value. Level handling belongs to Player::updateLevel.
    if (dir != DIR_Server)
        return;

    m_player->updateExp(data);
}

void EqlDispatch::playerUpdateSelf(const uint8_t* data, size_t len, uint8_t dir)
{
    // OP_ClientUpdate C>S 42B self-position, re-derived 2026-07-29 (the rotation
    // grew it 38B -> 42B and rearranged the body): u16 spawnId@2, IEEE floats
    // gameY@10 / gameX@22 / gameZ@34, 11-bit compass heading in the low bits at
    // @26. See player_self_pos.rs for how each was pinned.
    //
    // A self spawn id is BACK on the wire at offset 2 (the 38B bodies between
    // 07/14 and 07/29 carried none). It is deliberately NOT used to adopt, and
    // that is the whole point of this note: the id it carries is the PHANTOM
    // TWIN's, not the live copy's. Measured on a 3-zone capture — the field held
    // 15707 / 15719 while zoneEntry name-match adopted 15701 / 15715, and dumping
    // OP_ZoneEntry shows each pair sharing one name, i.e. exactly eql's
    // live-plus-phantom double-announce (the twin's id sits a few higher, and is
    // also what self stats are keyed to). Setting m_player to it would pin us to
    // the hidden phantom and leave the live copy loose in the spawn list.
    //
    // So the death-respawn gap stays open: an in-zone respawn issues a new id
    // pair and sends no self OP_ZoneEntry, and this field alone cannot say which
    // half of the pair is live. Closing it needs the twin -> live mapping, which
    // only the ZoneEntry pair establishes (m_selfTracker::alt_id / is_self).
    if (dir != DIR_Client)
        return;
    auto out = seq::rust::decode_player_self_pos(
        rust::Slice<const uint8_t>{data, len});
    if (!out.ok)
        return;
    // Mid-session attach (sniffer started, or restarted, while already in a
    // zone) never witnesses OP_PlayerProfile or the OP_ZoneEntry burst, so the
    // name match above can never fire and the player stays invisible until they
    // zone — position dead, and stat-sync unattributable because the tracker
    // knows no id. This report is the client's OWN, so its id is ours by
    // construction; the tracker decides what that is worth (provisional only,
    // never displacing a name-matched live copy) and remembers it so the eql
    // stat channel resolves as self. With a self already adopted the same call
    // just learns which id is the twin.
    if (m_selfTracker->observe_self_pos(out.spawn_id) == 1 && m_player->id() == 0)
        m_player->setPlayerID(out.spawn_id);

    if (m_player->id() == 0)
        return;   // still unresolved (no id on the wire this patch)

    // Position and heading are authoritative on every packet. The velocities are
    // NOT located for this patch and the parser surfaces 0 for them rather than
    // reading a stale offset (which would smear the marker between updates) —
    // see player_self_pos.rs. delta_heading has no wire field.
    m_player->applySelfPosition(int16_t(out.x), int16_t(out.y), int16_t(out.z),
                                int16_t(out.delta_x), int16_t(out.delta_y),
                                int16_t(out.delta_z), out.heading, 0.0f);
}

void EqlDispatch::death(const uint8_t* data, size_t len, uint8_t dir)
{
    // OP_Death (0x66cb) S>C, 40B newCorpseStruct (victim = spawn_id, killer_id).
    if (dir != DIR_Server)
        return;
    auto out = seq::rust::decode_death(rust::Slice<const uint8_t>{data, len});
    if (!out.ok)
        return;

    if (m_player && out.spawn_id == (uint32_t)m_player->id())
    {
        // The LOCAL player died. On EQL a death respawns you IN-ZONE with a
        // BRAND-NEW self-spawn id: there is no SessionDisconnect (unlike a zone
        // change, which resets the self-id to 0 and self-heals) and no
        // player-reinit OP_ZoneEntry (the classic charProfile re-entry). The
        // respawned PC is simply re-broadcast as an ordinary OP_ZoneEntry NewSpawn
        // and the C>S self-pos stream switches to the new id (confirmed on
        // eqlegends-corpsepin: self-id 12636 -> 12913, no id-0 dip). The Live
        // death chain (killSpawn -> corpse-in-place, then respawnFromHover +
        // OP_ZoneEntry re-init) never completes here, so routing the player's own
        // death to killSpawn pins the PC record to the death spot as a renamed
        // corpse forever — the marker never follows the respawn (the reported
        // bug). Instead sever the player from its id so the next self-pos
        // re-adopts the respawn id (playerUpdateSelf's id()==0 path) and
        // playerChangedID re-adopts the respawn spawn's name; stash the dead id so
        // a trailing self-pos for the old body can't re-pin us. The server's own
        // OP_RemoveSpawn/OP_DeleteSpawn for the old id clears the stale body, and
        // EQL broadcasts no lingering player-corpse spawn, so we fabricate none.
        m_awaitingRespawnFromId = out.spawn_id;
        m_player->setID(0);
        m_selfTracker->reset();   // new id pair follows; drop the old one
        return;
    }

    // A mob (or any non-player spawn) died: unchanged shared Live corpse path
    // (marks the spawn a corpse, sets last-kill on our killing blow, emits
    // SpawnKilled). It re-decodes the same bytes — cheap, and keeps one owner of
    // the corpse logic.
    m_spawnShell->killSpawn(data);
}

void EqlDispatch::enterWorld(const uint8_t*, size_t, uint8_t dir)
{
    // OP_EnterWorld (0x26bf) C>S, 72B: the client entering the world with its
    // character. It fires at login and AGAIN on every in-place session re-entry
    // — a private instance, or any zone that reuses the world socket. Because the
    // world socket is reused, BoxRegistry keeps the SAME box, so no active-box
    // roll re-snapshots the client, and zoneResolved is deliberately non-clearing
    // (the first zone-in's name arrives after its bulk). So on a re-entry the box
    // still holds the OLD zone's spawns and our old self-id, and the instance can
    // share the old zone's short name (making its zone_changed a client no-op).
    //
    // Reset here — BEFORE the instance's OP_ZoneEntry bulk repopulates — and drop
    // the self-id so playerUpdateSelf / own-spawn adoption re-adopts the instance's
    // NEW one; stash the old id so a trailing old-session self-pos can't re-pin us
    // (mirrors death()). That re-adoption calls Player::setPlayerID, whose
    // changedID signal already drives SessionAdapter::onPlayerIdChanged -> a fresh
    // Snapshot, so the web re-primes with the instance's spawns + new self-id with
    // no extra plumbing here. Gate on an established session so the initial login
    // (whose OP_EnterWorld precedes any zone) does not reset.
    if (dir != DIR_Client)
        return;
    if (!m_sessionEstablished || !m_player)
        return;
    seqInfo("EQL: session re-entry (OP_EnterWorld) — resetting box, self-id %d -> 0",
            m_player->id());
    const uint32_t oldId = (uint32_t)m_player->id();
    m_spawnShell->clear();
    m_player->setID(0);
    m_selfTracker->reset();   // ditto: the re-entry issues a fresh id pair
    m_awaitingRespawnFromId = oldId;
}

void EqlDispatch::playerUpdateOther(const uint8_t* data, size_t len, uint8_t dir)
{
    // OP_ClientUpdate S>C, 28B: the position broadcast for spawns OTHER than the
    // local player (players + some NPCs). eql packs each coord in the LOW 19 bits
    // of its own u32 (×8 fixed-point) — z@4, x@8, y@12 as re-derived 2026-07-29
    // against the untouched OP_MobUpdate / OP_NpcMoveUpdate streams — decoded by
    // this backend's own parse_player_spawn_pos.
    // The parser surfaces raw 19-bit values; apply >> 3 here for the 1/8-unit →
    // integer conversion (as Live's SpawnShell::playerUpdate does). Position
    // only → the neutral SpawnShell::moveSpawn, the same path OP_MobUpdate uses;
    // moveSpawn takes no heading, so the decoded heading is unused for now.
    if (dir != DIR_Server)
        return;
    auto out = seq::rust::decode_player_spawn_pos(
        rust::Slice<const uint8_t>{data, len});
    if (!out.ok)
        return;
    // The player's own position is authoritative from OP_ClientUpdate C>S
    // (playerUpdateSelf); never let the broadcast move the PC record.
    if (m_player && out.spawn_id == (uint16_t)m_player->id())
        return;
    m_spawnShell->moveSpawn(out.spawn_id,
                            int16_t(out.x >> 3), int16_t(out.y >> 3),
                            int16_t(out.z >> 3));
}

void EqlDispatch::newZone(const uint8_t* data, size_t len, uint8_t dir)
{
    // OP_NewZone (0x1dbf) S>C, once per zone-in: the authoritative current zone,
    // carried as packed null-terminated short + long name text. Drives the map /
    // filter / web via ZoneMgr::setZoneByName -> zoneResolved (which does NOT
    // clear spawns or reset the player — see zonemgr.cpp). This fires AFTER the
    // bulk spawn list + the profile, so it must not reset the box.
    if (dir != DIR_Server)
        return;
    auto out = seq::rust::decode_new_zone(rust::Slice<const uint8_t>{data, len});
    if (!out.ok)
        return;
    m_zoneMgr->setZoneByName(latin1(out.short_name), latin1(out.long_name));

    // A zone has now resolved: any LATER OP_EnterWorld is a genuine re-entry
    // (see enterWorld()).
    m_sessionEstablished = true;
}

bool EqlDispatch::consumeSelfSpawn(const QString& name, uint16_t id)
{
    // eql sends the local player's OWN ZoneEntry TWICE per zone — a live copy that
    // moves and a static phantom the client hides — each under a fresh per-zone id
    // (the phantom's is a few higher). Neither should become a spawns[] entry: the
    // self is owned by Player, whose identity comes from OP_PlayerProfile and whose
    // position/heading come from the C>S self-report.
    //
    // Which record is which (and the batch window that separates a twin from a
    // zone change) is decided by the Rust backend, so every host shares one
    // implementation. We only apply the verdict.
    const uint8_t routing = m_selfTracker->observe_spawn(
        m_player->realName().toStdString(), name.toStdString(), id);
    if (routing == 0)
        return false;   // not us — let it through to the spawn list

    // Re-homing runs playerChangedID, which drops any stray copy from the list
    // and re-snapshots the client. The twin is simply swallowed.
    if (routing == 1)
    {
        // A name match is authoritative: it supersedes any id adopted
        // provisionally from the C>S self-report while we had nothing better.
        m_selfTracker->take_retired_provisional();
        m_player->setPlayerID(id);
    }

    // eql keys the self's stats to the twin id, and the stat packet carrying the
    // real maxima can arrive BEFORE the record that identifies that id — in some
    // zone-ins it is the only one that ever carries them. The tracker holds such
    // vitals until the id resolves; this is where they land.
    applySelfVitals(m_selfTracker->take_pending_vitals());
    return true;
}

void EqlDispatch::spawn(const uint8_t* data, size_t len, uint8_t dir)
{
    // OP_ZoneEntry S>C: null-terminated name + variable NPC block, decoded by
    // the full front walk in parse_zone_spawn (name, lastName, race/class/deity/
    // guild, real npc flag, decoded position, HP, h2048 heading). title/suffix
    // are decoded but have no daemon/proto home yet; the heading is the starting
    // facing — OP_MobUpdate / OP_NpcMoveUpdate refine it on movement.
    if (dir != DIR_Server)
        return;
    auto out = seq::rust::decode_spawn(rust::Slice<const uint8_t>{data, len});
    if (!out.ok)
        return;
    const QString name = latin1(out.name);
    // Keep the local player's own ZoneEntry (and its phantom twin) out of the
    // spawn list; adopt/re-home the self-id from it instead.
    if (consumeSelfSpawn(name, (uint16_t)out.spawn_id))
        return;
    m_spawnShell->upsertSpawn((uint16_t)out.spawn_id, name, latin1(out.last_name),
                              out.x, out.y, out.z, out.heading,
                              out.level, out.cur_hp, out.max_hp,
                              (uint16_t)out.race, (uint8_t)out.class_, (uint16_t)out.deity,
                              (uint16_t)out.guild_id, (uint16_t)out.guild_server_id,
                              out.npc, out.class_mask);
}

void EqlDispatch::loadoutSwap(const uint8_t* data, size_t len, uint8_t dir)
{
    // OP_LoadoutSwap (0x7477) S>C: a player switched loadouts (the Legends
    // multiclass class/level change). EQL sends no OP_PlayerProfile on a swap,
    // so this is the only source for the new identity. The self variant carries
    // a serialized inventory tail; the server also broadcasts a short ~490B
    // variant for ANY nearby player's swap. Both share the header + ZoneEntry-
    // format record the Rust decoder reads (see loadout_swap.rs).
    if (dir != DIR_Server)
        return;
    auto out = seq::rust::decode_loadout_swap(rust::Slice<const uint8_t>{data, len});
    if (!out.ok)
        return;
    if (m_player && out.spawn_id == (uint32_t)m_player->id())
    {
        // Our own swap: refresh identity. Race is unchanged on a swap but the
        // record carries it correctly, so the existing setIdentity primitive is
        // safe (it drives class + level to the frontend via tSpawnChangedALL).
        m_player->setIdentity((uint16_t)out.race, (uint8_t)out.class_, out.level);
        return;
    }
    // A nearby player's swap: update the tracked spawn's level + class in place
    // (no position/HP — the spawn list / con display is the consumer), so it
    // doesn't read stale until that spawn's next regular OP_ZoneEntry.
    m_spawnShell->updateSpawnIdentity((uint16_t)out.spawn_id, out.level, (uint8_t)out.class_);
}

void EqlDispatch::stance(const uint8_t* data, size_t len, uint8_t dir)
{
    // OP_Stance (0x0fab) S>C echo (authoritative): the player's active stance
    // changed. 4B {u32 abilityId}; resolve the id to a display name and store it
    // on Player -> PlayerStats.stance. Unknown id → "#<id>" so it stays visible.
    if (dir != DIR_Server || !m_player)
        return;
    auto out = seq::rust::decode_activate_ability(
        rust::Slice<const uint8_t>{data, len});
    if (!out.ok)
        return;
    QString name = stanceName(out.ability_id);
    if (name.isEmpty())
        name = QStringLiteral("#%1").arg(out.ability_id);
    m_player->setStance(name);
}

void EqlDispatch::invocation(const uint8_t* data, size_t len, uint8_t dir)
{
    // OP_Invocation (0x3b12) S>C echo (authoritative): the player's active
    // invocation changed. Same 4B {u32 abilityId} as OP_Stance; resolve via the
    // invocation table and store on Player -> PlayerStats.invocation.
    if (dir != DIR_Server || !m_player)
        return;
    auto out = seq::rust::decode_activate_ability(
        rust::Slice<const uint8_t>{data, len});
    if (!out.ok)
        return;
    QString name = invocationName(out.ability_id);
    if (name.isEmpty())
        name = QStringLiteral("#%1").arg(out.ability_id);
    m_player->setInvocation(name);
}

void EqlDispatch::sendAATable(const uint8_t* data, size_t len, uint8_t dir)
{
    // OP_SendAATable (0x31ae) S>C: one AA ability-rank definition per packet,
    // burst at zone-in. Layout matches Live's aaInfoStruct: descID@0 (== the
    // profile's aa_array id) and titleSID@13 (a dbstr type-1 id shared by every
    // rank of an AA). Resolve titleSID -> name and record descID -> name so
    // protoencoder fills AAEntry.name for the purchased AAs. A missing name
    // (no dbstr, or an unknown titleSID) leaves the map untouched, so the web
    // keeps its "#<id>" fallback. Silent (no signal): names surface on the next
    // coalesced PlayerStats snapshot, same as the stance/invocation echoes.
    if (dir != DIR_Server || !m_player || !m_dbStrings)
        return;
    auto out = seq::rust::decode_aa_table_entry(
        rust::Slice<const uint8_t>{data, len});
    if (!out.ok)
        return;
    const QString name = m_dbStrings->nameById(out.title_sid);
    if (!name.isEmpty())
        m_player->setAAName(out.desc_id, name);
}

void EqlDispatch::mobUpdate(const uint8_t* data, size_t len, uint8_t dir)
{
    // OP_MobUpdate S>C, 14B (parser validates len).
    if (dir != DIR_Server)
        return;
    auto out = seq::rust::decode_mob_update(rust::Slice<const uint8_t>{data, len});
    if (!out.ok)
        return;
    // The Legends wire is byte-identical to Live's spawnPositionUpdate (packed
    // 19-bit fixed-point coords), so the shared Live parser decodes the full
    // coordinate range directly — no 16-bit wrap, no phase-unwrap needed.
    m_spawnShell->moveSpawn(out.spawn_id,
                            int16_t(out.x), int16_t(out.y), int16_t(out.z));
}

void EqlDispatch::statSync(const uint8_t* data, size_t len, uint8_t dir)
{
    // OP_HPUpdate (0x2735) S>C multiplexed stat-sync channel. The Rust parser
    // (parse_stat_sync) decodes u32 spawnId + u8 flags + per-stat payload —
    // wide {i64 cur,max} pairs or narrow u8 percents — for HP/mana/endurance,
    // with a structural size canary. Passing the REAL length matters: the size
    // varies per packet (6/7/21/37/53B, optional +4 tail).
    if (dir != DIR_Server)
        return;
    auto out = seq::rust::decode_stat_sync(rust::Slice<const uint8_t>{data, len});
    if (!out.ok)
        return;

    // Attribution is the backend's job: eql keys the self's stats to the phantom
    // twin id rather than the adopted movement id, and the packet carrying the
    // real maxima can precede the record that identifies that id. The tracker
    // resolves both cases (holding early vitals until consumeSelfSpawn drains
    // them); here we only route the verdict.
    //
    // Non-self HP goes to the spawn list as before. The player is never an
    // m_spawns entry (unlike legacy SEQ, where the f-patch routes to
    // m_spawns.value(spawnId)) — their vitals reach the client via player_stats.
    // Guard max>0 so a wide zero-max packet can't blank an HP bar.
    const auto verdict = m_selfTracker->observe_stat_sync(out);
    if (verdict.is_self)
        applySelfVitals(verdict);
    else if (out.has_hp && out.hp_max > 0)
        m_spawnShell->updateSpawnHP((uint16_t)out.spawn_id,
                                    (int32_t)out.hp_cur, (int32_t)out.hp_max);
}
