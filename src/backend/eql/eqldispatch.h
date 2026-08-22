/*
 * eqldispatch.h — EverQuest Legends packet dispatch adapter.
 *
 * Compiled ONLY for -DSEQ_TARGET=eql. Holds the Legends-specific handlers so
 * they never touch the core manager classes (ZoneMgr / SpawnShell / Player).
 *
 * This is a plain (non-QObject) class: handlers are registered as typed
 * PacketHandlers via EQPacketStream::on() (see wire_eql.cpp), so they no longer
 * need to be Qt slots. That removes the old moc/ODR constraint that forced this
 * adapter to exist as a QObject at all — a slot on a core manager would have
 * been ODR-used by its always-compiled moc and dragged the Legends code into
 * the live/test link. With typed dispatch the handlers are just callables; this
 * adapter groups them and is owned via a shared_ptr captured by the wired
 * closures, so its lifetime tracks the box's stream dispatchers.
 *
 * Each method decodes the Legends wire bytes via the Rust `seq::rust::decode_*`
 * parsers (there is no `everquest_legends.h`; eql owns no C++ wire structs) and
 * drives the core managers through their target-NEUTRAL public primitives (Player::
 * setIdentity/applySelfPosition, ZoneMgr::setZoneByName, SpawnShell::
 * upsertSpawn/moveSpawn). No Legends type ever enters core.
 */
#ifndef SEQ_BACKEND_EQL_EQLDISPATCH_H
#define SEQ_BACKEND_EQL_EQLDISPATCH_H

#include <cstddef>
#include <cstdint>
#include <functional>

// For rust::Box<EqlSelfTracker> — the one piece of decoder state the dispatch
// owns rather than re-deriving per packet.
#include "seq-bridge-cxx/lib.h"
#include "rustsession.h"

class QString;
class ZoneMgr;
class SpawnShell;
class Player;
class DbStrings;
class GuildShell;
class GuildMgr;

class EqlDispatch
{
public:
    EqlDispatch(ZoneMgr* zoneMgr, SpawnShell* spawnShell, Player* player,
                DbStrings* dbStrings, GuildShell* guildShell, GuildMgr* guildMgr,
                std::function<bool()> rustLifecycleOwned,
                std::function<bool(seq::shadow::LifecycleKind)>
                    rustLifecycleAccepted,
                std::function<void(const seq::shadow::LifecycleProfile&)>
                    profileObserved);

    // OP_GuildsInZoneList / OP_NewGuildInZone S>C: the guilds present in the zone
    // — the sole source of guild names. Decoded in seq-backend-eql (eql owns its
    // own parser; the wire is never read in C++) and fed to GuildMgr::learnGuilds.
    void guildsInZone(const uint8_t* data, size_t len, uint8_t dir);
    void newGuildInZone(const uint8_t* data, size_t len, uint8_t dir);

    // OP_GuildMemberList S>C: the full guild roster. The eql wire diverges from
    // the stock struct (wider header, multiclass bitmask in the class slot, a
    // rank field, a trailing zone id), so it is decoded in seq-backend-eql and
    // applied through GuildShell's neutral setRoster().
    void guildMemberList(const uint8_t* data, size_t len, uint8_t dir);

    // OP_GuildMOTD S>C: the guild message of the day. Decoded in seq-backend-eql
    // (its own parser, per the isolation rule) and applied via GuildShell::setMotd.
    void guildMotd(const uint8_t* data, size_t len, uint8_t dir);

    // OP_PlayerProfile (0x62f0) S>C: identity header (race/class/level).
    void profile(const uint8_t* data, size_t len, uint8_t dir);
    // OP_MoneyUpdate (0x6414) S>C, 20B: authoritative carried purse.
    void moneyUpdate(const uint8_t* data, size_t len, uint8_t dir);
    // OP_SpawnAppearance2 (0x4170), 24B: pose/state broadcast. Type numbering
    // is eql-specific, which is why it is decoded here and not in core.
    void spawnAppearance(const uint8_t* data, size_t len, uint8_t dir);
    // OP_ClientUpdate (0x7171) C>S: this player's float position + heading.
    void playerUpdateSelf(const uint8_t* data, size_t len, uint8_t dir);
    // OP_ClientUpdate (0x7171) S>C, 28B: OTHER spawns' position broadcast
    // (19-bit ×8 packed) — drives SpawnShell::moveSpawn, like OP_MobUpdate.
    void playerUpdateOther(const uint8_t* data, size_t len, uint8_t dir);
    // OP_ZoneChange S>C/C>S: eql's is client-only and carries position but no
    // zone id, so it can only raise the zoning flag.
    void zoneChange(const uint8_t* data, size_t len, uint8_t dir);
    // OP_NewZone (0x1dbf) S>C: packed null-terminated short/long zone names.
    void newZone(const uint8_t* data, size_t len, uint8_t dir);
    // OP_ZoneEntry (0x4606) S>C: null-terminated name + fixed spawn block
    // (stock-SEQ name for the per-spawn zone stream; not the dead OP_ZoneSpawns).
    void spawn(const uint8_t* data, size_t len, uint8_t dir);
    // OP_MobUpdate (0x061b) S>C: per-mob position update.
    void mobUpdate(const uint8_t* data, size_t len, uint8_t dir);
    // OP_HPUpdate (0x2735) S>C: multiplexed stat-sync channel — spawn HP cur/max
    // (wide numeric or narrow percent) plus the player's mana/endurance.
    void statSync(const uint8_t* data, size_t len, uint8_t dir);
    // OP_ExpUpdate (0x6801) S>C: the regular exp bar. eql has NO discrete
    // level-up packet, so this ALSO drives the level — a wrap (exp resets to a
    // low value) is a ding. See the .cpp + OPCODES_LEGENDS.md.
    void expUpdate(const uint8_t* data, size_t len, uint8_t dir);
    // OP_Death (0x66cb) S>C: mob deaths take the shared Live corpse path; the
    // player's OWN death severs the self-id so the in-zone respawn's new self-id
    // is re-adopted by playerUpdateSelf (EQL sends no player-reinit OP_ZoneEntry).
    void death(const uint8_t* data, size_t len, uint8_t dir);
    // OP_LoadoutSwap (0x631c; was 0x7477 pre-07/14) S>C: a player's multiclass loadout change (new
    // class + level, no profile resend). Self refreshes the PC identity; the
    // broadcast variant updates a nearby tracked spawn's class/level.
    void loadoutSwap(const uint8_t* data, size_t len, uint8_t dir);
    // OP_Stance (0x0fab) / OP_Invocation (0x3b12) S>C echo (authoritative): the
    // player's active stance / invocation. 4B {u32 abilityId}; the id resolves
    // to a display name (backend-only table) stored on Player -> PlayerStats.
    void stance(const uint8_t* data, size_t len, uint8_t dir);
    void invocation(const uint8_t* data, size_t len, uint8_t dir);
    // OP_SendAATable (0x31ae) S>C: one AA ability-rank definition per packet,
    // burst at zone-in. Resolves descID -> titleSID -> dbstr type-1 name and
    // records descID -> name on Player so protoencoder can fill AAEntry.name
    // for purchased AAs (web AA window shows the title instead of "#<id>").
    void sendAATable(const uint8_t* data, size_t len, uint8_t dir);
    // OP_EnterWorld (0x26bf) C>S: the world handshake, fired at login AND on
    // every in-place session re-entry (private instance, or any zone that reuses
    // the world socket — same BoxRegistry box, so no active-box roll re-primes
    // the client). On a re-entry it resets the box (clears the old zone's spawns,
    // drops the stale self-id); the instance's new self-id then re-adopts via
    // setPlayerID, whose changedID signal already re-snapshots the client. A
    // no-op at the initial login (no zone resolved yet).
    void enterWorld(const uint8_t* data, size_t len, uint8_t dir);

private:
    // True if (name,id) is the local player's own ZoneEntry — adopt/re-home the
    // self-id from it and keep it (and its per-zone phantom twin) out of the spawn
    // list. The decision itself lives in the Rust backend (m_selfTracker); this
    // only applies the verdict to Player.
    bool consumeSelfSpawn(const QString& name, uint16_t id);
    // Push a resolved set of self vitals onto Player (no-op when empty).
    void applySelfVitals(const seq::rust::SelfStat& v);

    ZoneMgr*    m_zoneMgr;
    SpawnShell* m_spawnShell;
    Player*     m_player;
    DbStrings*  m_dbStrings;   // AA titleSID -> name (dbstr type-1); may be empty
    GuildShell* m_guildShell;
    GuildMgr*   m_guildMgr;    // daemon-global guild id->name map
    std::function<bool()> m_rustLifecycleOwned;
    std::function<bool(seq::shadow::LifecycleKind)> m_rustLifecycleAccepted;
    std::function<void(const seq::shadow::LifecycleProfile&)>
        m_profileObserved;
    // While awaiting the in-zone respawn after the local player's own death,
    // holds the dead self-spawn id (0 = not awaiting). Guards playerUpdateSelf
    // from re-adopting the dead id onto a trailing corpse-side update.
    uint32_t    m_awaitingRespawnFromId = 0;
    // Retained across a Rust-owned reset, whose typed reducer runs before this
    // adapter's reconnect compatibility tail.
    uint32_t    m_lastKnownSelfId = 0;
    // True once the first OP_NewZone has resolved, i.e. a session is established.
    // Gates enterWorld(): the initial login's OP_EnterWorld precedes any zone, so
    // it must not reset; every LATER OP_EnterWorld is a genuine re-entry.
    bool        m_sessionEstablished = false;
    // eql session identity, owned by the Rust backend: which spawn id is us, its
    // per-zone phantom twin, and any vitals that arrived before the id carrying
    // them could be attributed. Stateful by necessity — see
    // seq_backend_eql::self_track. Keeping it there rather than here means scry
    // and any other host inherit the quirk instead of re-deriving it.
    rust::Box<seq::rust::EqlSelfTracker> m_selfTracker;
};

#endif // SEQ_BACKEND_EQL_EQLDISPATCH_H
