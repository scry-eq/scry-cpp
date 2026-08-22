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
#include "seq/v1/events.pb.h"

namespace seq::shadow {

enum class Stream { World, Zone };
enum class Direction { ServerToClient, ClientToServer };
enum class FlushReason { Shutdown, ZoneTransition, ReplayEnd, Reset };
enum class Disposition { Decoded, Ignored, Unhandled, Malformed, Unmapped };
enum class LifecycleSelector { Legacy, Shadow, Rust };
enum class LifecycleKind {
    SessionReset,
    EnterWorld,
    ZoneServerInfo,
    PlayerProfile,
    ZoneTransition,
    ZoneChanged,
    ZoneEnvironmentChanged,
    TimeOfDay,
};

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
struct SessionReset { rust::EventSessionReset payload; };
struct ZoneTransition { rust::EventZoneTransition payload; };
struct ZoneEnvironmentChanged { rust::EventZoneEnvironment payload; };
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
struct EnterWorld { rust::EventEnterWorld payload; };

using Event = std::variant<
    SpawnAdded, SpawnMoved, SpawnRemoved, SpawnKilled, SpawnHp, StatSync,
    SelfPos, SpawnAnimation, SpawnIllusion, GuildsInZone, TimeOfDay,
    ZoneChanged, PlayerProfile, Stance, Invocation, InspectAnswer, GuildRoster,
    ZoneServerInfo, ItemSet, ItemLearned, GuildMotd, GuildRankName, LoadoutSwap,
    Doors, GroundItemRemoved, GroundItem, Combat, SpawnCast, Targeted,
    Considered, AaTable, Exp, AaExp, Stamina, ManaUpdate, SkillUpdate,
    LootTransaction, LootDrops, Money, SimpleMessage, FormattedMessage,
    SpecialMessage, LootMessage, Chat, BuffList, GroupFollow, GroupDisband,
    LevelUpdate, EnterWorld, SessionReset, ZoneTransition,
    ZoneEnvironmentChanged>;

static_assert(std::variant_size_v<Event> == 52,
              "the C++ shadow event model must cover every Rust event kind");

// A copyable, deterministic representation used by shadow comparison. The
// payload is a length-prefixed binary encoding of every field in the selected
// lifecycle event. It avoids retaining references into the bounded journal.
struct LifecycleObservation {
    LifecycleKind kind = LifecycleKind::SessionReset;
    std::vector<uint8_t> payload;

    bool operator==(const LifecycleObservation& other) const
    {
        return kind == other.kind && payload == other.payload;
    }
};

struct LifecycleComparison {
    bool orderedEventsEqual = false;
    bool projectionsEqual = false;
    size_t rustEventCount = 0;
    size_t legacyEventCount = 0;
    size_t rustProjectionCount = 0;
    size_t legacyProjectionCount = 0;
};

struct LifecycleProfile {
    std::string name;
    std::string lastName;
    uint32_t classId = 0;
    uint8_t level = 0;
    uint32_t race = 0;
    uint32_t deity = 0;
    uint32_t currentHp = 0;
    uint32_t mana = 0;
    std::vector<uint32_t> aaIds;
    std::vector<uint32_t> aaValues;
    uint32_t aaSpent = 0;
    std::vector<uint32_t> skills;
    uint32_t classMask = 0;
    uint32_t strength = 0;
    uint32_t stamina = 0;
    uint32_t charisma = 0;
    uint32_t dexterity = 0;
    uint32_t intelligence = 0;
    uint32_t agility = 0;
    uint32_t wisdom = 0;
    uint32_t platinum = 0;
    uint32_t gold = 0;
    uint32_t silver = 0;
    uint32_t copper = 0;
};

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
std::vector<LifecycleObservation> lifecycleObservations(const Batch& batch);
std::vector<seq::v1::Envelope> projectLifecycle(const Batch& batch);
LifecycleObservation observeSessionReset(rust::EventSessionResetReason reason);
LifecycleObservation observeEnterWorld(const std::string& characterName);
LifecycleObservation observeZoneServer(const std::string& host, uint16_t port);
LifecycleObservation observeProfile(const LifecycleProfile& profile);
LifecycleObservation observeZoneTransition(
    const std::string& characterName, std::optional<uint32_t> zoneId,
    std::optional<uint32_t> instanceId, bool confirmed);
LifecycleObservation observeZoneChanged(const std::string& shortName,
                                        const std::string& longName);
LifecycleObservation observeZoneEnvironment(
    const std::string& zoneFile, float experienceMultiplier,
    float safeX, float safeY, float safeZ);
LifecycleObservation observeTimeOfDay(uint32_t year, uint32_t month,
                                      uint32_t day, uint32_t wireHour,
                                      uint32_t minute);
LifecycleComparison compareLifecycle(
    const Batch& rustBatch,
    const std::vector<LifecycleObservation>& legacyEvents,
    const std::vector<seq::v1::Envelope>& legacyProjections);
LifecycleComparison compareLifecycle(
    const std::vector<LifecycleObservation>& rustEvents,
    const std::vector<seq::v1::Envelope>& rustProjections,
    const std::vector<LifecycleObservation>& legacyEvents,
    const std::vector<seq::v1::Envelope>& legacyProjections);
bool isLifecycleEvent(const Event& event);

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
            size_t journalByteLimit = 4 * 1024 * 1024,
            LifecycleSelector lifecycleSelector = LifecycleSelector::Shadow);

    const Record& decode(Stream stream, uint16_t opcode, Direction direction,
                         const uint8_t* payload, size_t payloadSize,
                         int64_t timestamp);
    const Record& flush(FlushReason reason);
    const std::deque<Record>& journal() const { return m_journal; }
    uint64_t recordCount() const { return m_recordCount; }
    uint64_t droppedRecordCount() const { return m_droppedRecordCount; }
    size_t journalBytes() const { return m_journalBytes; }
    LifecycleSelector lifecycleSelector() const { return m_lifecycleSelector; }
    bool runsLegacyLifecycle() const
    { return m_lifecycleSelector != LifecycleSelector::Rust; }
    bool comparesLifecycle() const
    { return m_lifecycleSelector == LifecycleSelector::Shadow; }
    bool appliesRustLifecycle() const
    { return m_lifecycleSelector == LifecycleSelector::Rust; }
    const std::optional<LifecycleComparison>& lastLifecycleComparison() const
    { return m_lastLifecycleComparison; }
    void recordLifecycleComparison(LifecycleComparison comparison)
    { m_lastLifecycleComparison = std::move(comparison); }

private:
    const Record& append(Record record);

    ::rust::Box<rust::SessionResource> m_session;
    size_t m_journalLimit;
    size_t m_journalByteLimit;
    size_t m_journalBytes = 0;
    const LifecycleSelector m_lifecycleSelector;
    uint64_t m_recordCount = 0;
    uint64_t m_droppedRecordCount = 0;
    std::deque<Record> m_journal;
    std::optional<LifecycleComparison> m_lastLifecycleComparison;
};

} // namespace seq::shadow

#endif
