#ifndef RUSTSESSION_H
#define RUSTSESSION_H

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "seq-bridge-cxx/lib.h"

namespace seq::shadow {

enum class Stream { World, Zone };
enum class Direction { ServerToClient, ClientToServer };
enum class FlushReason { Shutdown, ZoneTransition, ReplayEnd, Reset };
enum class Disposition { Decoded, Ignored, Unhandled, Malformed, Unmapped };

struct SpawnAdded { rust::EventSpawnInfo payload; };
struct SpawnMoved { rust::EventSpawnMoved payload; };
struct SpawnRemoved { rust::EventSpawnId payload; };
struct SpawnKilled { rust::EventSpawnKilled payload; };
struct SpawnHp { rust::EventSpawnHp payload; };
struct StatSync { rust::EventStatSync payload; };
struct SelfPos { rust::EventSelfPos payload; };
struct SpawnAnimation { rust::EventSpawnAnimation payload; };
struct SpawnIllusion { rust::EventSpawnIllusion payload; };
struct GuildsInZone { rust::EventGuildsInZone payload; };
struct TimeOfDay { rust::EventTimeOfDay payload; };
struct ZoneChanged { rust::EventZoneInfo payload; };
struct PlayerProfile { rust::EventProfileInfo payload; };
struct Stance { rust::EventNamed payload; };
struct Invocation { rust::EventNamed payload; };
struct InspectAnswer { rust::EventInspectAnswer payload; };
struct GuildRoster { rust::EventGuildRoster payload; };
struct ZoneServerInfo { rust::EventZoneServerInfo payload; };
struct ItemSet { rust::EventItemSet payload; };
struct ItemLearned { rust::EventItemLearned payload; };
struct GuildMotd { rust::EventGuildMotdPayload payload; };
struct GuildRankName { rust::EventGuildRankName payload; };
struct LoadoutSwap { rust::EventLoadoutSwap payload; };
struct Doors { rust::EventDoors payload; };
struct GroundItemRemoved { rust::EventGroundItemRemoved payload; };
struct GroundItem { rust::EventGroundItem payload; };
struct Combat { rust::EventCombat payload; };
struct SpawnCast { rust::EventSpawnCast payload; };
struct Targeted { rust::EventSpawnId payload; };
struct Considered { rust::EventSpawnId payload; };
struct AaTable { rust::EventAaTable payload; };
struct Exp { rust::EventExp payload; };
struct AaExp { rust::EventAaExp payload; };
struct Stamina { rust::EventStaminaPayload payload; };
struct ManaUpdate { rust::EventManaUpdate payload; };
struct SkillUpdate { rust::EventSkillUpdatePayload payload; };
struct LootTransaction { rust::EventLootTransactionPayload payload; };
struct LootDrops { rust::EventLootDropsPayload payload; };
struct Money { rust::EventMoney payload; };
struct SimpleMessage { rust::EventSimpleMessagePayload payload; };
struct FormattedMessage { rust::EventFormattedMessagePayload payload; };
struct SpecialMessage { rust::EventSpecialMessagePayload payload; };
struct LootMessage { rust::EventLootMessagePayload payload; };
struct Chat { rust::EventChat payload; };
struct BuffList { rust::EventBuffList payload; };
struct GroupFollow { rust::EventGroupFollowPayload payload; };
struct GroupDisband { rust::EventGroupDisbandPayload payload; };
struct LevelUpdate { rust::EventLevelUpdatePayload payload; };
struct EnterWorld {};

using Event = std::variant<
    SpawnAdded, SpawnMoved, SpawnRemoved, SpawnKilled, SpawnHp, StatSync,
    SelfPos, SpawnAnimation, SpawnIllusion, GuildsInZone, TimeOfDay,
    ZoneChanged, PlayerProfile, Stance, Invocation, InspectAnswer, GuildRoster,
    ZoneServerInfo, ItemSet, ItemLearned, GuildMotd, GuildRankName, LoadoutSwap,
    Doors, GroundItemRemoved, GroundItem, Combat, SpawnCast, Targeted,
    Considered, AaTable, Exp, AaExp, Stamina, ManaUpdate, SkillUpdate,
    LootTransaction, LootDrops, Money, SimpleMessage, FormattedMessage,
    SpecialMessage, LootMessage, Chat, BuffList, GroupFollow, GroupDisband,
    LevelUpdate, EnterWorld>;

static_assert(std::variant_size_v<Event> == 49,
              "the C++ shadow event model must cover every Rust event kind");

struct Batch {
    uint64_t protocolGeneration = 0;
    Disposition disposition = Disposition::Ignored;
    std::vector<Event> events;
    std::vector<rust::SelfStat> selfStats;
    std::vector<rust::LootRow> lootRows;
};

struct PacketRecord {
    Stream stream = Stream::Zone;
    uint16_t opcode = 0;
    Direction direction = Direction::ServerToClient;
    size_t payloadSize = 0;
    int64_t timestamp = 0;
};

struct Record {
    uint64_t sequence = 0;
    std::optional<PacketRecord> packet;
    std::optional<FlushReason> flushReason;
    Batch batch;
    size_t retainedBytes = 0;
    bool detailsOmitted = false;
};

// Convert one cxx-compatible tagged batch into the public C++ variant. This
// function only checks the tag and payload index. It does not look up opcodes,
// select a backend, correlate packets, or change daemon state.
Batch translate(rust::SessionDecodeBatch batch);

class ProtocolRegistry {
public:
    // An empty directory uses the catalogs embedded in the pinned decoder.
    explicit ProtocolRegistry(const std::string& protocolDir = {});

    const rust::SessionProtocolRegistry& rustRegistry() const { return *m_registry; }
    std::string contentHash(rust::SessionBackend backend) const;

private:
    ::rust::Box<rust::SessionProtocolRegistry> m_registry;
};

class Session {
public:
    // journalLimit bounds diagnostic memory. Every record has a monotonic
    // sequence so consumers can detect records dropped from the front.
    Session(const ProtocolRegistry& registry, rust::SessionBackend backend,
            size_t journalLimit = 256,
            size_t journalByteLimit = 4 * 1024 * 1024);

    const Record& decode(Stream stream, uint16_t opcode, Direction direction,
                         const uint8_t* payload, size_t payloadSize,
                         int64_t timestamp);
    const Record& flush(FlushReason reason);
    // Returns true only when this call opened and flushed a new boundary.
    // Repeated start signals within one transition are ignored until complete.
    bool beginZoneTransition();
    void completeZoneTransition() { m_zoneTransitionOpen = false; }

    const std::deque<Record>& journal() const { return m_journal; }
    uint64_t recordCount() const { return m_recordCount; }
    uint64_t droppedRecordCount() const { return m_droppedRecordCount; }
    size_t journalBytes() const { return m_journalBytes; }

private:
    const Record& append(Record record);

    ::rust::Box<rust::SessionResource> m_session;
    size_t m_journalLimit;
    size_t m_journalByteLimit;
    size_t m_journalBytes = 0;
    bool m_zoneTransitionOpen = false;
    uint64_t m_recordCount = 0;
    uint64_t m_droppedRecordCount = 0;
    std::deque<Record> m_journal;
};

} // namespace seq::shadow

#endif
