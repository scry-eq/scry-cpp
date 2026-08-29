#ifndef RUSTSESSION_H
#define RUSTSESSION_H

#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "applicationtrace.h"
#include "seq-bridge-cxx/lib.h"
#include "seq/v1/events.pb.h"

namespace seq::shadow {

enum class Stream { World, Zone };
enum class Direction { ServerToClient, ClientToServer };
enum class FlushReason { Shutdown, ZoneTransition, ReplayEnd, Reset };
enum class Disposition { Decoded, Ignored, Unhandled, Malformed, Unmapped };
enum class LifecycleSelector { Legacy, Shadow, Rust };
using EntitySelector = LifecycleSelector;
using PlayerSelector = LifecycleSelector;
using ProgressionSelector = LifecycleSelector;
using LootSelector = LifecycleSelector;
using CombatSelector = LifecycleSelector;
using CommunicationSelector = LifecycleSelector;
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
enum class EntityKind {
    SpawnAdded,
    SpawnMoved,
    SpawnRemoved,
    SpawnRenamed,
    Doors,
    GroundItemRemoved,
    GroundItem,
    CorpseLocated,
    ZonePoints,
};
enum class PlayerKind {
    PlayerIdentityUpdated,
    PlayerMoved,
    PlayerVitalsUpdated,
    SpawnHealthUpdated,
    PlayerDied,
    SpawnDied,
    SpawnIdentityUpdated,
    PlayerAppearanceUpdated,
};
enum class ProgressionKind {
    InventorySnapshot,
    InventoryItemUpdated,
    EquipmentSnapshot,
    EquipmentSlotUpdated,
    MoneyBalanceUpdated,
    SkillsSnapshot,
    SkillValueUpdated,
    ExperienceUpdated,
    AlternateAdvancementSnapshot,
    AlternateAdvancementUpdated,
    AlternateAbilityDefined,
};
enum class LootKind { CorpseLootSnapshot, LootAcquired };
enum class CombatKind {
    CombatDamage,
    SpellActionResolved,
    SpellCastStarted,
    SpellCastInterrupted,
    BuffAdded,
    BuffUpdated,
    BuffRemoved,
};
enum class CommunicationKind {
    ChatMessage,
    GroupRosterUpdated,
    GuildRosterUpdated,
    GuildMotdUpdated,
    GuildRankNamesUpdated,
    DynamicZoneUpdated,
};

struct PlayerIdentityUpdated { rust::EventPlayerIdentity payload; };
struct PlayerMoved { rust::EventPlayerMoved payload; };
struct PlayerVitalsUpdated { rust::EventPlayerVitals payload; };
struct SpawnHealthUpdated { rust::EventSpawnHealth payload; };
struct PlayerDied { rust::EventPlayerDied payload; };
struct SpawnDied { rust::EventSpawnDied payload; };
struct SpawnIdentityUpdated { rust::EventSpawnIdentity payload; };
struct PlayerAppearanceUpdated { rust::EventPlayerAppearance payload; };

struct SpawnAdded { rust::EventSpawnInfo payload; };
struct SpawnMoved { rust::EventSpawnMoved payload; };
struct SpawnRenamed { rust::EventSpawnRenamed payload; };
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
struct InventorySnapshot { rust::EventInventorySnapshot payload; };
struct InventoryItemUpdated { rust::EventInventoryItemUpdated payload; };
struct EquipmentSnapshot { rust::EventEquipmentSnapshot payload; };
struct EquipmentSlotUpdated { rust::EventEquipmentSlotUpdated payload; };
struct GuildMotd { rust::EventGuildMotdPayload payload; };
struct GuildRankName { rust::EventGuildRankName payload; };
struct LoadoutSwap { rust::EventLoadoutSwap payload; };
struct Doors { rust::EventDoors payload; };
struct GroundItemRemoved { rust::EventGroundItemRemoved payload; };
struct GroundItem { rust::EventGroundItem payload; };
struct CorpseLocated { rust::EventCorpseLocated payload; };
struct ZonePoints { rust::EventZonePoints payload; };
struct Combat { rust::EventCombat payload; };
struct CombatDamage { rust::EventCombatDamage payload; };
struct SpellAction { rust::EventSpellAction payload; };
struct SpellActionResolved { rust::EventSpellActionResolved payload; };
struct SpellCastRequest { rust::EventSpellCastRequest payload; };
struct SpawnCast { rust::EventSpawnCast payload; };
struct SpellCastStarted { rust::EventSpellCastStarted payload; };
struct SpellCastInterrupted { rust::EventSpellCastInterrupted payload; };
struct Targeted { rust::EventSpawnId payload; };
struct Considered { rust::EventSpawnId payload; };
struct AaTable { rust::EventAaTable payload; };
struct AlternateAbilityDefined { rust::EventAlternateAbilityDefinition payload; };
struct Exp { rust::EventExp payload; };
struct ExperienceUpdated { rust::EventExperienceProgress payload; };
struct AaExp { rust::EventAaExp payload; };
struct AlternateAdvancementSnapshot { rust::EventAlternateAdvancementSnapshot payload; };
struct AlternateAdvancementUpdated { rust::EventAlternateAdvancementProgress payload; };
struct Stamina { rust::EventStaminaPayload payload; };
struct ManaUpdate { rust::EventManaUpdate payload; };
struct SkillUpdate { rust::EventSkillUpdatePayload payload; };
struct SkillsSnapshot { rust::EventSkillsSnapshot payload; };
struct SkillValueUpdated { rust::EventSkillValue payload; };
struct LootTransaction { rust::EventLootTransactionPayload payload; };
struct LootDrops { rust::EventLootDropsPayload payload; };
struct CorpseLootSnapshot { rust::EventCorpseLootSnapshot payload; };
struct LootAcquired { rust::EventLootAcquisition payload; };
struct Money { rust::EventMoney payload; };
struct MoneyBalanceUpdated { rust::EventMoneyBalance payload; };
struct SimpleMessage { rust::EventSimpleMessagePayload payload; };
struct FormattedMessage { rust::EventFormattedMessagePayload payload; };
struct SpecialMessage { rust::EventSpecialMessagePayload payload; };
struct LootMessage { rust::EventLootMessagePayload payload; };
struct Chat { rust::EventChat payload; };
struct ChatMessage { rust::EventChatMessage payload; };
struct UcsRecord { rust::EventUcsRecord payload; };
struct BuffList { rust::EventBuffList payload; };
struct BuffWire { rust::EventBuffWire payload; };
struct BuffAdded { rust::EventActiveBuff payload; };
struct BuffUpdated { rust::EventActiveBuff payload; };
struct BuffRemoved { rust::EventBuffRemoved payload; };
struct GroupFollow { rust::EventGroupFollowPayload payload; };
struct GroupDisband { rust::EventGroupDisbandPayload payload; };
struct GroupRosterWire { rust::EventGroupRosterWire payload; };
struct GroupRosterUpdated { rust::EventGroupRosterState payload; };
struct GuildRosterUpdated { rust::EventGuildRosterState payload; };
struct GuildRosterWire { rust::EventGuildRosterState payload; };
struct GuildMemberStatus { rust::EventGuildMemberStatus payload; };
struct GuildMotdUpdated { rust::EventGuildMotdState payload; };
struct GuildRankNamesUpdated { rust::EventGuildRankNamesState payload; };
struct DynamicZoneInfo { rust::EventDynamicZoneInfo payload; };
struct DynamicZoneSwitch { rust::EventDynamicZoneSwitch payload; };
struct DynamicZoneUpdated { rust::EventDynamicZoneState payload; };
struct LevelUpdate { rust::EventLevelUpdatePayload payload; };
struct EnterWorld { rust::EventEnterWorld payload; };

using Event = std::variant<
    PlayerIdentityUpdated, PlayerMoved, PlayerVitalsUpdated,
    SpawnHealthUpdated, PlayerDied, SpawnDied, SpawnIdentityUpdated,
    PlayerAppearanceUpdated,
    SpawnAdded, SpawnMoved, SpawnRenamed, SpawnRemoved, SpawnKilled, SpawnHp, StatSync,
    SelfPos, SpawnAnimation, SpawnIllusion, GuildsInZone, TimeOfDay,
    ZoneChanged, PlayerProfile, Stance, Invocation, InspectAnswer, GuildRoster,
    ZoneServerInfo, ItemSet, ItemLearned, InventorySnapshot,
    InventoryItemUpdated, EquipmentSnapshot, EquipmentSlotUpdated,
    GuildMotd, GuildRankName, LoadoutSwap,
    Doors, GroundItemRemoved, GroundItem, CorpseLocated, ZonePoints,
    Combat, CombatDamage, SpellAction, SpellActionResolved, SpellCastRequest,
    SpawnCast, SpellCastStarted, SpellCastInterrupted, Targeted,
    Considered, AaTable, AlternateAbilityDefined, Exp, ExperienceUpdated,
    AaExp, AlternateAdvancementSnapshot, AlternateAdvancementUpdated,
    Stamina, ManaUpdate, SkillUpdate, SkillsSnapshot, SkillValueUpdated,
    LootTransaction, LootDrops, CorpseLootSnapshot, LootAcquired,
    Money, MoneyBalanceUpdated, SimpleMessage, FormattedMessage,
    SpecialMessage, LootMessage, Chat, ChatMessage, UcsRecord,
    BuffList, BuffWire, BuffAdded,
    BuffUpdated, BuffRemoved, GroupFollow, GroupDisband,
    GroupRosterWire, GroupRosterUpdated, GuildRosterUpdated,
    GuildRosterWire, GuildMemberStatus, GuildMotdUpdated,
    GuildRankNamesUpdated, DynamicZoneInfo, DynamicZoneSwitch,
    DynamicZoneUpdated,
    LevelUpdate, EnterWorld, SessionReset, ZoneTransition,
    ZoneEnvironmentChanged>;

static_assert(std::variant_size_v<Event> == 98,
              "the C++ shadow event model must cover every Rust event kind");

// Copyable comparison form: a length-prefixed encoding of every field, so
// nothing references the bounded journal.
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

struct EntityObservation {
    EntityKind kind = EntityKind::SpawnAdded;
    std::vector<uint8_t> payload;

    bool operator==(const EntityObservation& other) const
    {
        return kind == other.kind && payload == other.payload;
    }
};

using EntityComparison = LifecycleComparison;

struct PlayerObservation {
    PlayerKind kind = PlayerKind::PlayerIdentityUpdated;
    std::vector<uint8_t> payload;

    bool operator==(const PlayerObservation& other) const
    {
        return kind == other.kind && payload == other.payload;
    }
};

using PlayerComparison = LifecycleComparison;

struct ProgressionObservation {
    ProgressionKind kind = ProgressionKind::InventorySnapshot;
    std::vector<uint8_t> payload;

    bool operator==(const ProgressionObservation& other) const
    {
        return kind == other.kind && payload == other.payload;
    }
};

using ProgressionComparison = LifecycleComparison;

struct LootObservation {
    LootKind kind = LootKind::CorpseLootSnapshot;
    std::vector<uint8_t> payload;

    bool operator==(const LootObservation& other) const
    {
        return kind == other.kind && payload == other.payload;
    }
};

using LootComparison = LifecycleComparison;

struct CombatObservation {
    CombatKind kind = CombatKind::CombatDamage;
    std::vector<uint8_t> payload;

    bool operator==(const CombatObservation& other) const
    {
        return kind == other.kind && payload == other.payload;
    }
};

using CombatComparison = LifecycleComparison;

struct CommunicationObservation {
    CommunicationKind kind = CommunicationKind::ChatMessage;
    std::vector<uint8_t> payload;

    bool operator==(const CommunicationObservation& other) const
    {
        return kind == other.kind && payload == other.payload;
    }
};

using CommunicationComparison = LifecycleComparison;
using ChatTextResolver = std::function<std::string(
    uint32_t, const std::vector<std::string>&)>;

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
    std::optional<size_t> ucsPayloadSize;
    Batch batch;
    size_t retainedBytes = 0;
    bool detailsOmitted = false;
};

// Tag + payload-index translation only; no opcode lookup or state change.
Batch translate(rust::SessionDecodeBatch batch);
std::vector<LifecycleObservation> lifecycleObservations(const Batch& batch);
std::vector<seq::v1::Envelope> projectLifecycle(const Batch& batch);
std::vector<EntityObservation> entityObservations(const Batch& batch);
std::vector<seq::v1::Envelope> projectEntities(const Batch& batch);
EntityComparison compareEntities(
    const Batch& rustBatch,
    const std::vector<EntityObservation>& legacyEvents,
    const std::vector<seq::v1::Envelope>& legacyProjections);
std::vector<PlayerObservation> playerObservations(const Batch& batch);
std::vector<seq::v1::Envelope> projectPlayers(const Batch& batch);
PlayerComparison comparePlayers(
    const Batch& rustBatch,
    const std::vector<PlayerObservation>& legacyEvents,
    const std::vector<seq::v1::Envelope>& legacyProjections);
std::vector<ProgressionObservation> progressionObservations(const Batch& batch);
std::vector<seq::v1::Envelope> projectProgression(const Batch& batch);
ProgressionComparison compareProgression(
    const Batch& rustBatch,
    const std::vector<ProgressionObservation>& legacyEvents,
    const std::vector<seq::v1::Envelope>& legacyProjections);
std::vector<LootObservation> lootObservations(const Batch& batch);
std::vector<seq::v1::Envelope> projectLoot(const Batch& batch);
LootComparison compareLoot(
    const Batch& rustBatch,
    const std::vector<LootObservation>& legacyEvents,
    const std::vector<seq::v1::Envelope>& legacyProjections);
std::vector<CombatObservation> combatObservations(const Batch& batch);
std::vector<seq::v1::Envelope> projectCombat(const Batch& batch);
CombatComparison compareCombat(
    const Batch& rustBatch,
    const std::vector<CombatObservation>& legacyEvents,
    const std::vector<seq::v1::Envelope>& legacyProjections);
std::vector<CommunicationObservation> communicationObservations(
    const Batch& batch);
std::vector<seq::v1::Envelope> projectCommunication(
    const Batch& batch, const ChatTextResolver& resolveText = {});
CommunicationComparison compareCommunication(
    const Batch& rustBatch,
    const std::vector<CommunicationObservation>& legacyEvents,
    const std::vector<seq::v1::Envelope>& legacyProjections,
    const ChatTextResolver& resolveText = {});
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
bool isEntityEvent(const Event& event);
bool isPlayerEvent(const Event& event);
bool isProgressionEvent(const Event& event);
bool isLootEvent(const Event& event);
bool isCombatEvent(const Event& event);
bool isCommunicationEvent(const Event& event);

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
            LifecycleSelector lifecycleSelector = LifecycleSelector::Shadow,
            EntitySelector entitySelector = EntitySelector::Legacy,
            PlayerSelector playerSelector = PlayerSelector::Legacy,
            ProgressionSelector progressionSelector = ProgressionSelector::Legacy,
            LootSelector lootSelector = LootSelector::Legacy,
            CombatSelector combatSelector = CombatSelector::Legacy,
            CommunicationSelector communicationSelector =
                CommunicationSelector::Legacy);

    void setTraceWriter(std::unique_ptr<ApplicationTraceWriter> writer)
    { m_traceWriter = std::move(writer); }
    bool traceEnabled() const { return bool(m_traceWriter); }
    const ApplicationTraceWriter* traceWriter() const
    { return m_traceWriter.get(); }
    void finalizeTrace();

    const Record& decode(Stream stream, uint16_t opcode, Direction direction,
                         const uint8_t* payload, size_t payloadSize,
                         int64_t timestamp);
    const Record& decodeUcs(Direction direction, const uint8_t* payload,
                            size_t payloadSize);
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
    EntitySelector entitySelector() const { return m_entitySelector; }
    bool runsLegacyEntities() const
    { return m_entitySelector != EntitySelector::Rust; }
    bool comparesEntities() const
    { return m_entitySelector == EntitySelector::Shadow; }
    bool appliesRustEntities() const
    { return m_entitySelector == EntitySelector::Rust; }
    bool runsLegacyPlayers() const
    { return m_playerSelector != PlayerSelector::Rust; }
    bool comparesPlayers() const
    { return m_playerSelector == PlayerSelector::Shadow; }
    bool appliesRustPlayers() const
    { return m_playerSelector == PlayerSelector::Rust; }
    bool runsLegacyProgression() const
    { return m_progressionSelector != ProgressionSelector::Rust; }
    bool comparesProgression() const
    { return m_progressionSelector == ProgressionSelector::Shadow; }
    bool appliesRustProgression() const
    { return m_progressionSelector == ProgressionSelector::Rust; }
    bool runsLegacyLoot() const
    { return m_lootSelector != LootSelector::Rust; }
    bool comparesLoot() const
    { return m_lootSelector == LootSelector::Shadow; }
    bool appliesRustLoot() const
    { return m_lootSelector == LootSelector::Rust; }
    bool runsLegacyCombat() const
    { return m_combatSelector != CombatSelector::Rust; }
    bool comparesCombat() const
    { return m_combatSelector == CombatSelector::Shadow; }
    bool appliesRustCombat() const
    { return m_combatSelector == CombatSelector::Rust; }
    bool runsLegacyCommunication() const
    { return m_communicationSelector != CommunicationSelector::Rust; }
    bool comparesCommunication() const
    { return m_communicationSelector == CommunicationSelector::Shadow; }
    bool appliesRustCommunication() const
    { return m_communicationSelector == CommunicationSelector::Rust; }
    const std::optional<LifecycleComparison>& lastLifecycleComparison() const
    { return m_lastLifecycleComparison; }
    void recordLifecycleComparison(LifecycleComparison comparison)
    { m_lastLifecycleComparison = std::move(comparison); }
    const std::optional<EntityComparison>& lastEntityComparison() const
    { return m_lastEntityComparison; }
    void recordEntityComparison(EntityComparison comparison)
    { m_lastEntityComparison = std::move(comparison); }
    const std::optional<PlayerComparison>& lastPlayerComparison() const
    { return m_lastPlayerComparison; }
    void recordPlayerComparison(PlayerComparison comparison)
    { m_lastPlayerComparison = std::move(comparison); }
    const std::optional<ProgressionComparison>& lastProgressionComparison() const
    { return m_lastProgressionComparison; }
    void recordProgressionComparison(ProgressionComparison comparison)
    { m_lastProgressionComparison = std::move(comparison); }
    const std::optional<LootComparison>& lastLootComparison() const
    { return m_lastLootComparison; }
    void recordLootComparison(LootComparison comparison)
    { m_lastLootComparison = std::move(comparison); }
    const std::optional<CombatComparison>& lastCombatComparison() const
    { return m_lastCombatComparison; }
    void recordCombatComparison(CombatComparison comparison)
    { m_lastCombatComparison = std::move(comparison); }
    const std::optional<CommunicationComparison>& lastCommunicationComparison() const
    { return m_lastCommunicationComparison; }
    void recordCommunicationComparison(CommunicationComparison comparison)
    { m_lastCommunicationComparison = std::move(comparison); }

private:
    const Record& append(Record record);

    ::rust::Box<rust::SessionResource> m_session;
    size_t m_journalLimit;
    size_t m_journalByteLimit;
    size_t m_journalBytes = 0;
    const LifecycleSelector m_lifecycleSelector;
    const EntitySelector m_entitySelector;
    const PlayerSelector m_playerSelector;
    const ProgressionSelector m_progressionSelector;
    const LootSelector m_lootSelector;
    const CombatSelector m_combatSelector;
    const CommunicationSelector m_communicationSelector;
    uint64_t m_recordCount = 0;
    uint64_t m_droppedRecordCount = 0;
    std::deque<Record> m_journal;
    std::optional<LifecycleComparison> m_lastLifecycleComparison;
    std::optional<EntityComparison> m_lastEntityComparison;
    std::optional<PlayerComparison> m_lastPlayerComparison;
    std::optional<ProgressionComparison> m_lastProgressionComparison;
    std::optional<LootComparison> m_lastLootComparison;
    std::optional<CombatComparison> m_lastCombatComparison;
    std::optional<CommunicationComparison> m_lastCommunicationComparison;
    std::unique_ptr<ApplicationTraceWriter> m_traceWriter;

    void disableTrace(const std::exception& error);
};

} // namespace seq::shadow

#endif
