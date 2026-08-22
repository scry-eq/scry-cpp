#include "rustsession.h"
#include "protoencoder.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace seq::shadow {
namespace {

rust::SessionStream toRust(Stream stream)
{
    switch (stream) {
    case Stream::World: return rust::SessionStream::World;
    case Stream::Zone: return rust::SessionStream::Zone;
    }
    throw std::logic_error("unknown shadow stream");
}

rust::SessionDirection toRust(Direction direction)
{
    switch (direction) {
    case Direction::ServerToClient:
        return rust::SessionDirection::ServerToClient;
    case Direction::ClientToServer:
        return rust::SessionDirection::ClientToServer;
    }
    throw std::logic_error("unknown shadow direction");
}

rust::SessionFlushReason toRust(FlushReason reason)
{
    switch (reason) {
    case FlushReason::Shutdown: return rust::SessionFlushReason::Shutdown;
    case FlushReason::ZoneTransition:
        return rust::SessionFlushReason::ZoneTransition;
    case FlushReason::ReplayEnd: return rust::SessionFlushReason::ReplayEnd;
    case FlushReason::Reset: return rust::SessionFlushReason::Reset;
    }
    throw std::logic_error("unknown shadow flush reason");
}

Disposition disposition(rust::SessionDisposition value)
{
    switch (value) {
    case rust::SessionDisposition::Decoded: return Disposition::Decoded;
    case rust::SessionDisposition::Ignored: return Disposition::Ignored;
    case rust::SessionDisposition::Unhandled: return Disposition::Unhandled;
    case rust::SessionDisposition::Malformed: return Disposition::Malformed;
    case rust::SessionDisposition::Unmapped: return Disposition::Unmapped;
    }
    throw std::logic_error("unknown Rust session disposition");
}

template <typename T>
T takePayload(::rust::Vec<T>& payloads, uint32_t index)
{
    if (index >= payloads.size())
        throw std::out_of_range("Rust session event payload index is out of range");
    return std::move(payloads[index]);
}

template <typename T>
std::vector<T> takeVector(::rust::Vec<T>& values)
{
    std::vector<T> out;
    out.reserve(values.size());
    for (size_t i = 0; i < values.size(); ++i)
        out.push_back(std::move(values[i]));
    return out;
}

template <typename T>
void appendScalar(std::vector<uint8_t>& out, T value)
{
    static_assert(std::is_arithmetic_v<T> || std::is_enum_v<T>);
    const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(value));
}

void appendString(std::vector<uint8_t>& out, const ::rust::String& value)
{
    appendScalar(out, uint64_t(value.size()));
    out.insert(out.end(), value.data(), value.data() + value.size());
}

template <typename T>
void appendVector(std::vector<uint8_t>& out, const ::rust::Vec<T>& values)
{
    appendScalar(out, uint64_t(values.size()));
    for (const T& value : values)
        appendScalar(out, value);
}

void appendProfile(std::vector<uint8_t>& out, const rust::EventProfileInfo& p)
{
    appendString(out, p.name); appendString(out, p.last_name);
    appendScalar(out, p.class_); appendScalar(out, p.level);
    appendScalar(out, p.race); appendScalar(out, p.deity);
    appendScalar(out, p.class_mask);
}

void appendProfile(std::vector<uint8_t>& out, const LifecycleProfile& p)
{
    auto appendStdString = [&](const std::string& value) {
        appendScalar(out, uint64_t(value.size()));
        out.insert(out.end(), value.begin(), value.end());
    };
    appendStdString(p.name); appendStdString(p.lastName);
    appendScalar(out, p.classId); appendScalar(out, p.level);
    appendScalar(out, p.race); appendScalar(out, p.deity);
    appendScalar(out, p.classMask);
}

bool sameEnvelope(const seq::v1::Envelope& lhs,
                  const seq::v1::Envelope& rhs)
{
    return lhs.SerializeAsString() == rhs.SerializeAsString();
}

} // namespace

LifecycleObservation observeSessionReset(rust::EventSessionResetReason reason)
{
    LifecycleObservation out{LifecycleKind::SessionReset, {}};
    appendScalar(out.payload, reason);
    return out;
}

LifecycleObservation observeEnterWorld(const std::string& characterName)
{
    LifecycleObservation out{LifecycleKind::EnterWorld, {}};
    appendScalar(out.payload, uint64_t(characterName.size()));
    out.payload.insert(out.payload.end(), characterName.begin(), characterName.end());
    return out;
}

LifecycleObservation observeZoneServer(const std::string& host, uint16_t port)
{
    LifecycleObservation out{LifecycleKind::ZoneServerInfo, {}};
    appendScalar(out.payload, uint64_t(host.size()));
    out.payload.insert(out.payload.end(), host.begin(), host.end());
    appendScalar(out.payload, port);
    return out;
}

LifecycleObservation observeProfile(const LifecycleProfile& profile)
{
    LifecycleObservation out{LifecycleKind::PlayerProfile, {}};
    appendProfile(out.payload, profile);
    return out;
}

LifecycleObservation observeZoneTransition(
    const std::string& characterName, std::optional<uint32_t> zoneId,
    std::optional<uint32_t> instanceId, bool confirmed)
{
    LifecycleObservation out{LifecycleKind::ZoneTransition, {}};
    appendScalar(out.payload, uint64_t(characterName.size()));
    out.payload.insert(out.payload.end(), characterName.begin(), characterName.end());
    appendScalar(out.payload, zoneId.has_value());
    appendScalar(out.payload, zoneId.value_or(0));
    appendScalar(out.payload, instanceId.has_value());
    appendScalar(out.payload, instanceId.value_or(0));
    appendScalar(out.payload, confirmed);
    return out;
}

LifecycleObservation observeZoneChanged(const std::string& shortName,
                                        const std::string& longName)
{
    LifecycleObservation out{LifecycleKind::ZoneChanged, {}};
    for (const std::string* value : {&shortName, &longName}) {
        appendScalar(out.payload, uint64_t(value->size()));
        out.payload.insert(out.payload.end(), value->begin(), value->end());
    }
    return out;
}

LifecycleObservation observeZoneEnvironment(
    const std::string& zoneFile, float experienceMultiplier,
    float safeX, float safeY, float safeZ)
{
    LifecycleObservation out{LifecycleKind::ZoneEnvironmentChanged, {}};
    appendScalar(out.payload, uint64_t(zoneFile.size()));
    out.payload.insert(out.payload.end(), zoneFile.begin(), zoneFile.end());
    appendScalar(out.payload, experienceMultiplier);
    appendScalar(out.payload, safeX); appendScalar(out.payload, safeY);
    appendScalar(out.payload, safeZ);
    return out;
}

LifecycleObservation observeTimeOfDay(uint32_t year, uint32_t month,
                                      uint32_t day, uint32_t wireHour,
                                      uint32_t minute)
{
    LifecycleObservation out{LifecycleKind::TimeOfDay, {}};
    appendScalar(out.payload, year); appendScalar(out.payload, month);
    appendScalar(out.payload, day); appendScalar(out.payload, wireHour);
    appendScalar(out.payload, minute);
    return out;
}

Batch translate(rust::SessionDecodeBatch batch)
{
    Batch out;
    out.protocolGeneration = batch.protocol_generation;
    out.disposition = disposition(batch.disposition);
    out.events.reserve(batch.events.size());

    for (const rust::SessionEventRef& event : batch.events) {
        const uint32_t index = event.payload_index;
        switch (event.kind) {
        case rust::SessionEventKind::PlayerIdentityUpdated:
            out.events.emplace_back(PlayerIdentityUpdated{
                takePayload(batch.player_identity_updated, index)});
            break;
        case rust::SessionEventKind::PlayerMoved:
            out.events.emplace_back(PlayerMoved{
                takePayload(batch.player_moved, index)});
            break;
        case rust::SessionEventKind::PlayerVitalsUpdated:
            out.events.emplace_back(PlayerVitalsUpdated{
                takePayload(batch.player_vitals_updated, index)});
            break;
        case rust::SessionEventKind::SpawnHealthUpdated:
            out.events.emplace_back(SpawnHealthUpdated{
                takePayload(batch.spawn_health_updated, index)});
            break;
        case rust::SessionEventKind::PlayerDied:
            out.events.emplace_back(PlayerDied{
                takePayload(batch.player_died, index)});
            break;
        case rust::SessionEventKind::SpawnDied:
            out.events.emplace_back(SpawnDied{
                takePayload(batch.spawn_died, index)});
            break;
        case rust::SessionEventKind::SpawnIdentityUpdated:
            out.events.emplace_back(SpawnIdentityUpdated{
                takePayload(batch.spawn_identity_updated, index)});
            break;
        case rust::SessionEventKind::PlayerAppearanceUpdated:
            out.events.emplace_back(PlayerAppearanceUpdated{
                takePayload(batch.player_appearance_updated, index)});
            break;
        case rust::SessionEventKind::SpawnAdded:
            out.events.emplace_back(SpawnAdded{takePayload(batch.spawn_added, index)});
            break;
        case rust::SessionEventKind::SpawnMoved:
            out.events.emplace_back(SpawnMoved{takePayload(batch.spawn_moved, index)});
            break;
        case rust::SessionEventKind::SpawnRenamed:
            out.events.emplace_back(SpawnRenamed{
                takePayload(batch.spawn_renamed, index)});
            break;
        case rust::SessionEventKind::SpawnRemoved:
            out.events.emplace_back(SpawnRemoved{takePayload(batch.spawn_removed, index)});
            break;
        case rust::SessionEventKind::SpawnKilled:
            out.events.emplace_back(SpawnKilled{takePayload(batch.spawn_killed, index)});
            break;
        case rust::SessionEventKind::SpawnHp:
            out.events.emplace_back(SpawnHp{takePayload(batch.spawn_hp, index)});
            break;
        case rust::SessionEventKind::StatSync:
            out.events.emplace_back(StatSync{takePayload(batch.stat_sync, index)});
            break;
        case rust::SessionEventKind::SelfPos:
            out.events.emplace_back(SelfPos{takePayload(batch.self_pos, index)});
            break;
        case rust::SessionEventKind::SpawnAnimation:
            out.events.emplace_back(SpawnAnimation{
                takePayload(batch.spawn_animation, index)});
            break;
        case rust::SessionEventKind::SpawnIllusion:
            out.events.emplace_back(SpawnIllusion{
                takePayload(batch.spawn_illusion, index)});
            break;
        case rust::SessionEventKind::GuildsInZone:
            out.events.emplace_back(GuildsInZone{
                takePayload(batch.guilds_in_zone, index)});
            break;
        case rust::SessionEventKind::TimeOfDay:
            out.events.emplace_back(TimeOfDay{takePayload(batch.time_of_day, index)});
            break;
        case rust::SessionEventKind::ZoneChanged:
            out.events.emplace_back(ZoneChanged{takePayload(batch.zone_changed, index)});
            break;
        case rust::SessionEventKind::SessionReset:
            out.events.emplace_back(SessionReset{
                takePayload(batch.session_reset, index)});
            break;
        case rust::SessionEventKind::ZoneTransition:
            out.events.emplace_back(ZoneTransition{
                takePayload(batch.zone_transition, index)});
            break;
        case rust::SessionEventKind::ZoneEnvironmentChanged:
            out.events.emplace_back(ZoneEnvironmentChanged{
                takePayload(batch.zone_environment_changed, index)});
            break;
        case rust::SessionEventKind::PlayerProfile:
            out.events.emplace_back(PlayerProfile{
                takePayload(batch.player_profile, index)});
            break;
        case rust::SessionEventKind::Stance:
            out.events.emplace_back(Stance{takePayload(batch.named, index)});
            break;
        case rust::SessionEventKind::Invocation:
            out.events.emplace_back(Invocation{takePayload(batch.named, index)});
            break;
        case rust::SessionEventKind::InspectAnswer:
            out.events.emplace_back(InspectAnswer{
                takePayload(batch.inspect_answer, index)});
            break;
        case rust::SessionEventKind::GuildRoster:
            out.events.emplace_back(GuildRoster{takePayload(batch.guild_roster, index)});
            break;
        case rust::SessionEventKind::ZoneServerInfo:
            out.events.emplace_back(ZoneServerInfo{
                takePayload(batch.zone_server_info, index)});
            break;
        case rust::SessionEventKind::ItemSet:
            out.events.emplace_back(ItemSet{takePayload(batch.item_set, index)});
            break;
        case rust::SessionEventKind::ItemLearned:
            out.events.emplace_back(ItemLearned{takePayload(batch.item_learned, index)});
            break;
        case rust::SessionEventKind::GuildMotd:
            out.events.emplace_back(GuildMotd{takePayload(batch.guild_motd, index)});
            break;
        case rust::SessionEventKind::GuildRankName:
            out.events.emplace_back(GuildRankName{
                takePayload(batch.guild_rank_name, index)});
            break;
        case rust::SessionEventKind::LoadoutSwap:
            out.events.emplace_back(LoadoutSwap{takePayload(batch.loadout_swap, index)});
            break;
        case rust::SessionEventKind::Doors:
            out.events.emplace_back(Doors{takePayload(batch.doors, index)});
            break;
        case rust::SessionEventKind::GroundItemRemoved:
            out.events.emplace_back(GroundItemRemoved{
                takePayload(batch.ground_item_removed, index)});
            break;
        case rust::SessionEventKind::GroundItem:
            out.events.emplace_back(GroundItem{takePayload(batch.ground_item, index)});
            break;
        case rust::SessionEventKind::CorpseLocated:
            out.events.emplace_back(CorpseLocated{
                takePayload(batch.corpse_located, index)});
            break;
        case rust::SessionEventKind::ZonePoints:
            out.events.emplace_back(ZonePoints{
                takePayload(batch.zone_points, index)});
            break;
        case rust::SessionEventKind::Combat:
            out.events.emplace_back(Combat{takePayload(batch.combat, index)});
            break;
        case rust::SessionEventKind::SpawnCast:
            out.events.emplace_back(SpawnCast{takePayload(batch.spawn_cast, index)});
            break;
        case rust::SessionEventKind::Targeted:
            out.events.emplace_back(Targeted{takePayload(batch.spawn_id, index)});
            break;
        case rust::SessionEventKind::Considered:
            out.events.emplace_back(Considered{takePayload(batch.spawn_id, index)});
            break;
        case rust::SessionEventKind::AaTable:
            out.events.emplace_back(AaTable{takePayload(batch.aa_table, index)});
            break;
        case rust::SessionEventKind::Exp:
            out.events.emplace_back(Exp{takePayload(batch.exp, index)});
            break;
        case rust::SessionEventKind::AaExp:
            out.events.emplace_back(AaExp{takePayload(batch.aa_exp, index)});
            break;
        case rust::SessionEventKind::Stamina:
            out.events.emplace_back(Stamina{takePayload(batch.stamina, index)});
            break;
        case rust::SessionEventKind::ManaUpdate:
            out.events.emplace_back(ManaUpdate{takePayload(batch.mana_update, index)});
            break;
        case rust::SessionEventKind::SkillUpdate:
            out.events.emplace_back(SkillUpdate{takePayload(batch.skill_update, index)});
            break;
        case rust::SessionEventKind::LootTransaction:
            out.events.emplace_back(LootTransaction{
                takePayload(batch.loot_transaction, index)});
            break;
        case rust::SessionEventKind::LootDrops:
            out.events.emplace_back(LootDrops{takePayload(batch.loot_drops, index)});
            break;
        case rust::SessionEventKind::Money:
            out.events.emplace_back(Money{takePayload(batch.money, index)});
            break;
        case rust::SessionEventKind::SimpleMessage:
            out.events.emplace_back(SimpleMessage{
                takePayload(batch.simple_message, index)});
            break;
        case rust::SessionEventKind::FormattedMessage:
            out.events.emplace_back(FormattedMessage{
                takePayload(batch.formatted_message, index)});
            break;
        case rust::SessionEventKind::SpecialMessage:
            out.events.emplace_back(SpecialMessage{
                takePayload(batch.special_message, index)});
            break;
        case rust::SessionEventKind::LootMessage:
            out.events.emplace_back(LootMessage{takePayload(batch.loot_message, index)});
            break;
        case rust::SessionEventKind::Chat:
            out.events.emplace_back(Chat{takePayload(batch.chat, index)});
            break;
        case rust::SessionEventKind::BuffList:
            out.events.emplace_back(BuffList{takePayload(batch.buff_list, index)});
            break;
        case rust::SessionEventKind::GroupFollow:
            out.events.emplace_back(GroupFollow{takePayload(batch.group_follow, index)});
            break;
        case rust::SessionEventKind::GroupDisband:
            out.events.emplace_back(GroupDisband{
                takePayload(batch.group_disband, index)});
            break;
        case rust::SessionEventKind::LevelUpdate:
            out.events.emplace_back(LevelUpdate{takePayload(batch.level_update, index)});
            break;
        case rust::SessionEventKind::EnterWorld:
            out.events.emplace_back(EnterWorld{
                takePayload(batch.enter_world, index)});
            break;
        }
    }

    out.selfStats = takeVector(batch.self_stats);
    out.lootRows = takeVector(batch.loot_rows);
    return out;
}

std::vector<LifecycleObservation> lifecycleObservations(const Batch& batch)
{
    std::vector<LifecycleObservation> observations;
    for (const Event& event : batch.events) {
        LifecycleObservation observation;
        bool lifecycle = true;
        std::visit([&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            const auto& p = value.payload;
            if constexpr (std::is_same_v<T, SessionReset>) {
                observation.kind = LifecycleKind::SessionReset;
                appendScalar(observation.payload, p.reason);
            } else if constexpr (std::is_same_v<T, EnterWorld>) {
                observation.kind = LifecycleKind::EnterWorld;
                appendString(observation.payload, p.character_name);
            } else if constexpr (std::is_same_v<T, ZoneServerInfo>) {
                observation.kind = LifecycleKind::ZoneServerInfo;
                appendString(observation.payload, p.host);
                appendScalar(observation.payload, p.port);
            } else if constexpr (std::is_same_v<T, PlayerProfile>) {
                observation.kind = LifecycleKind::PlayerProfile;
                appendProfile(observation.payload, p);
            } else if constexpr (std::is_same_v<T, ZoneTransition>) {
                observation.kind = LifecycleKind::ZoneTransition;
                appendString(observation.payload, p.character_name);
                appendScalar(observation.payload, p.has_zone_id);
                appendScalar(observation.payload, p.zone_id);
                appendScalar(observation.payload, p.has_instance_id);
                appendScalar(observation.payload, p.instance_id);
                appendScalar(observation.payload, p.confirmed);
            } else if constexpr (std::is_same_v<T, ZoneChanged>) {
                observation.kind = LifecycleKind::ZoneChanged;
                appendString(observation.payload, p.short_name);
                appendString(observation.payload, p.long_name);
            } else if constexpr (std::is_same_v<T, ZoneEnvironmentChanged>) {
                observation.kind = LifecycleKind::ZoneEnvironmentChanged;
                appendString(observation.payload, p.zone_file);
                appendScalar(observation.payload, p.experience_multiplier);
                appendScalar(observation.payload, p.safe_x);
                appendScalar(observation.payload, p.safe_y);
                appendScalar(observation.payload, p.safe_z);
            } else if constexpr (std::is_same_v<T, TimeOfDay>) {
                observation.kind = LifecycleKind::TimeOfDay;
                appendScalar(observation.payload, p.year);
                appendScalar(observation.payload, p.month);
                appendScalar(observation.payload, p.day);
                appendScalar(observation.payload, p.hour);
                appendScalar(observation.payload, p.minute);
            } else {
                lifecycle = false;
            }
        }, event);
        if (lifecycle)
            observations.push_back(std::move(observation));
    }
    return observations;
}

bool isLifecycleEvent(const Event& event)
{
    return std::visit([](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        return std::is_same_v<T, SessionReset> ||
               std::is_same_v<T, EnterWorld> ||
               std::is_same_v<T, ZoneServerInfo> ||
               std::is_same_v<T, PlayerProfile> ||
               std::is_same_v<T, ZoneTransition> ||
               std::is_same_v<T, ZoneChanged> ||
               std::is_same_v<T, ZoneEnvironmentChanged> ||
               std::is_same_v<T, TimeOfDay>;
    }, event);
}

bool isEntityEvent(const Event& event)
{
    return std::visit([](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        return std::is_same_v<T, SpawnAdded> ||
               std::is_same_v<T, SpawnMoved> ||
               std::is_same_v<T, SpawnRemoved> ||
               std::is_same_v<T, SpawnRenamed> ||
               std::is_same_v<T, Doors> ||
               std::is_same_v<T, GroundItemRemoved> ||
               std::is_same_v<T, GroundItem> ||
               std::is_same_v<T, CorpseLocated> ||
               std::is_same_v<T, ZonePoints>;
    }, event);
}

std::vector<EntityObservation> entityObservations(const Batch& batch)
{
    std::vector<EntityObservation> out;
    auto pushPoint = [](std::vector<uint8_t>& bytes,
                        const rust::EventPoint3& point) {
        appendScalar(bytes, point.x);
        appendScalar(bytes, point.y);
        appendScalar(bytes, point.z);
    };
    auto pushPos = [](std::vector<uint8_t>& bytes,
                      const rust::EventPos& pos) {
        appendScalar(bytes, pos.x);
        appendScalar(bytes, pos.y);
        appendScalar(bytes, pos.z);
        appendScalar(bytes, pos.heading_deg);
    };

    for (const Event& event : batch.events) {
        std::visit([&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            const auto& p = value.payload;
            if constexpr (std::is_same_v<T, SpawnAdded>) {
                EntityObservation observation{EntityKind::SpawnAdded, {}};
                appendScalar(observation.payload, p.id);
                appendString(observation.payload, p.name);
                appendString(observation.payload, p.last_name);
                appendScalar(observation.payload, p.race);
                appendScalar(observation.payload, p.class_);
                appendScalar(observation.payload, p.deity);
                appendScalar(observation.payload, p.level);
                appendScalar(observation.payload, p.npc);
                appendScalar(observation.payload, p.cur_hp);
                appendScalar(observation.payload, p.has_max_hp);
                if (p.has_max_hp) appendScalar(observation.payload, p.max_hp);
                appendScalar(observation.payload, p.guild_id);
                appendScalar(observation.payload, p.guild_server_id);
                appendScalar(observation.payload, p.class_mask);
                appendScalar(observation.payload, p.has_pos);
                if (p.has_pos) pushPos(observation.payload, p.pos);
                appendScalar(observation.payload, p.velocity.has_x);
                if (p.velocity.has_x) appendScalar(observation.payload, p.velocity.x);
                appendScalar(observation.payload, p.velocity.has_y);
                if (p.velocity.has_y) appendScalar(observation.payload, p.velocity.y);
                appendScalar(observation.payload, p.velocity.has_z);
                if (p.velocity.has_z) appendScalar(observation.payload, p.velocity.z);
                appendScalar(observation.payload, p.has_delta_heading);
                if (p.has_delta_heading)
                    appendScalar(observation.payload, p.delta_heading);
                appendScalar(observation.payload, p.has_animation);
                if (p.has_animation) appendScalar(observation.payload, p.animation);
                appendScalar(observation.payload, p.has_equipment_models);
                if (p.has_equipment_models)
                    appendVector(observation.payload, p.equipment_models);
                out.push_back(std::move(observation));
            } else if constexpr (std::is_same_v<T, SpawnMoved>) {
                EntityObservation observation{EntityKind::SpawnMoved, {}};
                appendScalar(observation.payload, p.id);
                pushPos(observation.payload, p.pos);
                appendScalar(observation.payload, p.velocity.has_x);
                if (p.velocity.has_x) appendScalar(observation.payload, p.velocity.x);
                appendScalar(observation.payload, p.velocity.has_y);
                if (p.velocity.has_y) appendScalar(observation.payload, p.velocity.y);
                appendScalar(observation.payload, p.velocity.has_z);
                if (p.velocity.has_z) appendScalar(observation.payload, p.velocity.z);
                appendScalar(observation.payload, p.has_delta_heading);
                if (p.has_delta_heading)
                    appendScalar(observation.payload, p.delta_heading);
                appendScalar(observation.payload, p.has_animation);
                if (p.has_animation) appendScalar(observation.payload, p.animation);
                out.push_back(std::move(observation));
            } else if constexpr (std::is_same_v<T, SpawnRemoved>) {
                EntityObservation observation{EntityKind::SpawnRemoved, {}};
                appendScalar(observation.payload, p.id);
                out.push_back(std::move(observation));
            } else if constexpr (std::is_same_v<T, SpawnRenamed>) {
                EntityObservation observation{EntityKind::SpawnRenamed, {}};
                appendScalar(observation.payload, p.has_id);
                if (p.has_id) appendScalar(observation.payload, p.id);
                appendString(observation.payload, p.old_name);
                appendString(observation.payload, p.new_name);
                out.push_back(std::move(observation));
            } else if constexpr (std::is_same_v<T, Doors>) {
                for (const auto& door : p.doors) {
                    EntityObservation observation{EntityKind::Doors, {}};
                    appendScalar(observation.payload, door.id);
                    appendString(observation.payload, door.name);
                    pushPoint(observation.payload, door.position);
                    appendScalar(observation.payload, door.heading);
                    appendScalar(observation.payload, door.incline);
                    appendScalar(observation.payload, door.size);
                    appendScalar(observation.payload, door.open_type);
                    appendScalar(observation.payload, door.state);
                    appendScalar(observation.payload, door.invert_state);
                    appendScalar(observation.payload, door.has_zone_point_id);
                    if (door.has_zone_point_id)
                        appendScalar(observation.payload, door.zone_point_id);
                    out.push_back(std::move(observation));
                }
            } else if constexpr (std::is_same_v<T, GroundItemRemoved>) {
                EntityObservation observation{EntityKind::GroundItemRemoved, {}};
                appendScalar(observation.payload, p.drop_id);
                out.push_back(std::move(observation));
            } else if constexpr (std::is_same_v<T, GroundItem>) {
                EntityObservation observation{EntityKind::GroundItem, {}};
                appendScalar(observation.payload, p.id);
                appendString(observation.payload, p.actor_definition);
                pushPoint(observation.payload, p.position);
                appendScalar(observation.payload, p.has_heading);
                if (p.has_heading) appendScalar(observation.payload, p.heading);
                out.push_back(std::move(observation));
            } else if constexpr (std::is_same_v<T, CorpseLocated>) {
                EntityObservation observation{EntityKind::CorpseLocated, {}};
                appendScalar(observation.payload, p.id);
                pushPoint(observation.payload, p.position);
                out.push_back(std::move(observation));
            } else if constexpr (std::is_same_v<T, ZonePoints>) {
                EntityObservation observation{EntityKind::ZonePoints, {}};
                appendScalar(observation.payload, uint64_t(p.points.size()));
                for (const auto& point : p.points) {
                    appendScalar(observation.payload, point.has_trigger_id);
                    if (point.has_trigger_id)
                        appendScalar(observation.payload, point.trigger_id);
                    appendScalar(observation.payload,
                                 point.has_actor_definition);
                    if (point.has_actor_definition)
                        appendString(observation.payload,
                                     point.actor_definition);
                    pushPoint(observation.payload, point.position);
                    appendScalar(observation.payload, point.heading);
                    appendScalar(observation.payload,
                                 point.has_destination_zone_id);
                    if (point.has_destination_zone_id)
                        appendScalar(observation.payload,
                                     point.destination_zone_id);
                    appendScalar(observation.payload,
                                 point.has_destination_instance_id);
                    if (point.has_destination_instance_id)
                        appendScalar(observation.payload,
                                     point.destination_instance_id);
                }
                out.push_back(std::move(observation));
            }
        }, event);
    }
    return out;
}

std::vector<seq::v1::Envelope> projectEntities(const Batch& batch)
{
    std::vector<seq::v1::Envelope> out;
    auto position = [](seq::v1::Pos* target, int32_t x, int32_t y,
                       int32_t z, uint32_t heading) {
        target->set_x(-x);
        target->set_y(-y);
        target->set_z(z);
        target->set_heading(int32_t(heading));
    };
    auto motion = [](seq::v1::Pos* target,
                     const rust::EventVelocity& velocity,
                     bool hasDeltaHeading, int16_t deltaHeading,
                     bool hasAnimation, int16_t animation) {
        target->set_vx(velocity.has_x ? -velocity.x : 0);
        target->set_vy(velocity.has_y ? -velocity.y : 0);
        target->set_vz(velocity.has_z ? velocity.z : 0);
        target->set_delta_heading(
            hasDeltaHeading ? int32_t(int8_t(deltaHeading)) : 0);
        target->set_animation(
            hasAnimation ? uint32_t(uint8_t(animation)) : 0);
    };
    for (const Event& event : batch.events) {
        std::visit([&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            const auto& p = value.payload;
            if constexpr (std::is_same_v<T, SpawnAdded>) {
                seq::v1::Envelope envelope;
                auto* spawn = envelope.mutable_spawn_added()->mutable_spawn();
                spawn->set_id(p.id);
                spawn->set_name(seq::encode::compatibilitySpawnName(
                    QString::fromUtf8(p.name.data(), int(p.name.size())))
                    .toStdString());
                spawn->set_last_name(std::string(p.last_name));
                spawn->set_race(p.race);
                spawn->set_class_(p.class_);
                spawn->set_deity(p.deity);
                spawn->set_level(p.level);
                spawn->set_hp_cur(p.cur_hp);
                spawn->set_hp_max(p.has_max_hp ? p.max_hp : 100);
                spawn->set_guild_id(p.guild_id);
                spawn->set_guild_server_id(p.guild_server_id);
                spawn->set_class_mask(p.class_mask);
                switch (p.npc) {
                case 0: spawn->set_type(seq::v1::PC); break;
                case 2: spawn->set_type(seq::v1::CORPSE_PC); break;
                case 3: spawn->set_type(seq::v1::CORPSE_NPC); break;
                default: spawn->set_type(seq::v1::NPC); break;
                }
                if (p.has_pos) {
                    position(spawn->mutable_pos(), p.pos.x, p.pos.y,
                             p.pos.z, p.pos.heading_deg);
                    motion(spawn->mutable_pos(), p.velocity,
                           p.has_delta_heading, p.delta_heading,
                           p.has_animation, p.animation);
                }
                if (p.has_equipment_models) {
                    for (uint32_t model : p.equipment_models)
                        spawn->add_equip_models(model);
                }
                out.push_back(std::move(envelope));
            } else if constexpr (std::is_same_v<T, SpawnMoved>) {
                seq::v1::Envelope envelope;
                auto* update = envelope.mutable_spawn_updated();
                update->set_id(p.id);
                position(update->mutable_pos(), p.pos.x, p.pos.y, p.pos.z,
                         p.pos.heading_deg);
                motion(update->mutable_pos(), p.velocity,
                       p.has_delta_heading, p.delta_heading,
                       p.has_animation, p.animation);
                out.push_back(std::move(envelope));
            } else if constexpr (std::is_same_v<T, SpawnRemoved>) {
                seq::v1::Envelope envelope;
                envelope.mutable_spawn_removed()->set_id(p.id);
                out.push_back(std::move(envelope));
            } else if constexpr (std::is_same_v<T, SpawnRenamed>) {
                if (!p.has_id) return;
                seq::v1::Envelope envelope;
                auto* update = envelope.mutable_spawn_updated();
                update->set_id(p.id);
                update->set_name(std::string(p.new_name));
                out.push_back(std::move(envelope));
            } else if constexpr (std::is_same_v<T, Doors>) {
                for (const auto& door : p.doors) {
                    seq::v1::Envelope envelope;
                    auto* spawn = envelope.mutable_spawn_added()->mutable_spawn();
                    spawn->set_id(door.id);
                    spawn->set_name(seq::encode::compatibilityDoorName(
                        QString::fromUtf8(door.name.data(),
                                          int(door.name.size())),
                        door.id).toStdString());
                    spawn->set_type(seq::v1::DOOR);
                    position(spawn->mutable_pos(), int32_t(door.position.x),
                             int32_t(door.position.y),
                             int32_t(door.position.z * 10.0f),
                             0);
                    out.push_back(std::move(envelope));
                }
            } else if constexpr (std::is_same_v<T, GroundItemRemoved>) {
                seq::v1::Envelope envelope;
                envelope.mutable_spawn_removed()->set_id(p.drop_id);
                out.push_back(std::move(envelope));
            } else if constexpr (std::is_same_v<T, GroundItem>) {
                seq::v1::Envelope envelope;
                auto* spawn = envelope.mutable_spawn_added()->mutable_spawn();
                spawn->set_id(p.id);
                spawn->set_name(seq::encode::compatibilityGroundItemName(
                    QString::fromUtf8(p.actor_definition.data(),
                                      int(p.actor_definition.size())))
                    .toStdString());
                spawn->set_type(seq::v1::DROP);
                position(spawn->mutable_pos(), int32_t(p.position.x),
                         int32_t(p.position.y), int32_t(p.position.z),
                         0);
                out.push_back(std::move(envelope));
            } else if constexpr (std::is_same_v<T, CorpseLocated>) {
                seq::v1::Envelope envelope;
                auto* killed = envelope.mutable_spawn_killed();
                killed->set_deceased_id(p.id);
                killed->set_killer_id(0);
                out.push_back(std::move(envelope));
            }
        }, event);
    }
    return out;
}

EntityComparison compareEntities(
    const Batch& rustBatch,
    const std::vector<EntityObservation>& legacyEvents,
    const std::vector<seq::v1::Envelope>& legacyProjections)
{
    EntityComparison comparison;
    const auto rustEvents = entityObservations(rustBatch);
    const auto rustProjections = projectEntities(rustBatch);
    comparison.rustEventCount = rustEvents.size();
    comparison.legacyEventCount = legacyEvents.size();
    comparison.rustProjectionCount = rustProjections.size();
    comparison.legacyProjectionCount = legacyProjections.size();
    comparison.orderedEventsEqual = rustEvents == legacyEvents;
    comparison.projectionsEqual =
        rustProjections.size() == legacyProjections.size() &&
        std::equal(rustProjections.begin(), rustProjections.end(),
                   legacyProjections.begin(), sameEnvelope);
    return comparison;
}

bool isPlayerEvent(const Event& event)
{
    return std::visit([](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        return std::is_same_v<T, PlayerIdentityUpdated> ||
               std::is_same_v<T, PlayerMoved> ||
               std::is_same_v<T, PlayerVitalsUpdated> ||
               std::is_same_v<T, SpawnHealthUpdated> ||
               std::is_same_v<T, PlayerDied> ||
               std::is_same_v<T, SpawnDied> ||
               std::is_same_v<T, SpawnIdentityUpdated> ||
               std::is_same_v<T, PlayerAppearanceUpdated>;
    }, event);
}

std::vector<PlayerObservation> playerObservations(const Batch& batch)
{
    std::vector<PlayerObservation> out;
    auto point = [](std::vector<uint8_t>& bytes, const rust::EventPos& pos) {
        appendScalar(bytes, pos.x); appendScalar(bytes, pos.y);
        appendScalar(bytes, pos.z); appendScalar(bytes, pos.heading_deg);
    };
    auto vital = [](std::vector<uint8_t>& bytes,
                    const rust::EventVitalValue& value) {
        appendScalar(bytes, value.current);
        appendScalar(bytes, value.has_maximum);
        if (value.has_maximum) appendScalar(bytes, value.maximum);
    };
    for (const Event& event : batch.events) {
        std::visit([&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            const auto& p = value.payload;
            PlayerObservation observation;
            bool selected = true;
            if constexpr (std::is_same_v<T, PlayerIdentityUpdated>) {
                observation.kind = PlayerKind::PlayerIdentityUpdated;
                appendScalar(observation.payload, p.has_spawn_id);
                if (p.has_spawn_id) appendScalar(observation.payload, p.spawn_id);
                appendString(observation.payload, p.name);
                appendString(observation.payload, p.last_name);
                appendScalar(observation.payload, p.race);
                appendScalar(observation.payload, p.class_);
                appendScalar(observation.payload, p.deity);
                appendScalar(observation.payload, p.level);
                appendScalar(observation.payload, p.class_mask);
            } else if constexpr (std::is_same_v<T, PlayerMoved>) {
                observation.kind = PlayerKind::PlayerMoved;
                appendScalar(observation.payload, p.has_spawn_id);
                if (p.has_spawn_id) appendScalar(observation.payload, p.spawn_id);
                point(observation.payload, p.pos);
            } else if constexpr (std::is_same_v<T, PlayerVitalsUpdated>) {
                observation.kind = PlayerKind::PlayerVitalsUpdated;
                appendScalar(observation.payload, p.has_health);
                if (p.has_health) vital(observation.payload, p.health);
                appendScalar(observation.payload, p.has_mana);
                if (p.has_mana) vital(observation.payload, p.mana);
                appendScalar(observation.payload, p.has_endurance);
                if (p.has_endurance) vital(observation.payload, p.endurance);
            } else if constexpr (std::is_same_v<T, SpawnHealthUpdated>) {
                observation.kind = PlayerKind::SpawnHealthUpdated;
                appendScalar(observation.payload, p.id);
                appendScalar(observation.payload, p.current);
                appendScalar(observation.payload, p.maximum);
            } else if constexpr (std::is_same_v<T, PlayerDied>) {
                observation.kind = PlayerKind::PlayerDied;
                appendScalar(observation.payload, p.has_killer_id);
                if (p.has_killer_id) appendScalar(observation.payload, p.killer_id);
            } else if constexpr (std::is_same_v<T, SpawnDied>) {
                observation.kind = PlayerKind::SpawnDied;
                appendScalar(observation.payload, p.id);
                appendScalar(observation.payload, p.has_killer_id);
                if (p.has_killer_id) appendScalar(observation.payload, p.killer_id);
            } else if constexpr (std::is_same_v<T, SpawnIdentityUpdated>) {
                observation.kind = PlayerKind::SpawnIdentityUpdated;
                appendScalar(observation.payload, p.id);
                appendScalar(observation.payload, p.level);
                appendScalar(observation.payload, p.class_);
                appendScalar(observation.payload, p.race);
            } else if constexpr (std::is_same_v<T, PlayerAppearanceUpdated>) {
                observation.kind = PlayerKind::PlayerAppearanceUpdated;
                appendScalar(observation.payload, p.has_race);
                if (p.has_race) appendScalar(observation.payload, p.race);
                appendScalar(observation.payload, p.has_gender);
                if (p.has_gender) appendScalar(observation.payload, p.gender);
                appendScalar(observation.payload, p.has_animation);
                if (p.has_animation) appendScalar(observation.payload, p.animation);
            } else {
                selected = false;
            }
            if (selected) out.push_back(std::move(observation));
        }, event);
    }
    return out;
}

std::vector<seq::v1::Envelope> projectPlayers(const Batch& batch)
{
    std::vector<seq::v1::Envelope> out;
    auto pos = [](seq::v1::Pos* target, const rust::EventPos& source) {
        target->set_x(-source.x); target->set_y(-source.y);
        target->set_z(source.z); target->set_heading(source.heading_deg);
    };
    for (const Event& event : batch.events) {
        std::visit([&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            const auto& p = value.payload;
            if constexpr (std::is_same_v<T, PlayerIdentityUpdated>) {
                seq::v1::Envelope envelope;
                auto* stats = envelope.mutable_player_stats();
                stats->set_name(std::string(p.name));
                stats->set_class_(p.class_); stats->set_race(p.race);
                stats->set_level(p.level); stats->set_class_mask(p.class_mask);
                out.push_back(std::move(envelope));
            } else if constexpr (std::is_same_v<T, PlayerMoved>) {
                if (!p.has_spawn_id) return;
                seq::v1::Envelope envelope;
                auto* update = envelope.mutable_spawn_updated();
                update->set_id(p.spawn_id); pos(update->mutable_pos(), p.pos);
                out.push_back(std::move(envelope));
            } else if constexpr (std::is_same_v<T, PlayerVitalsUpdated>) {
                seq::v1::Envelope envelope;
                auto* stats = envelope.mutable_player_stats();
                if (p.has_health) {
                    stats->set_hp_cur(uint32_t(std::max(p.health.current, 0)));
                    if (p.health.has_maximum)
                        stats->set_hp_max(uint32_t(std::max(p.health.maximum, 0)));
                }
                if (p.has_mana) {
                    stats->set_mana_cur(uint32_t(std::max(p.mana.current, 0)));
                    if (p.mana.has_maximum)
                        stats->set_mana_max(uint32_t(std::max(p.mana.maximum, 0)));
                }
                if (p.has_endurance) {
                    stats->set_endurance_cur(
                        uint32_t(std::max(p.endurance.current, 0)));
                    if (p.endurance.has_maximum)
                        stats->set_endurance_max(
                            uint32_t(std::max(p.endurance.maximum, 0)));
                }
                out.push_back(std::move(envelope));
            } else if constexpr (std::is_same_v<T, SpawnHealthUpdated>) {
                seq::v1::Envelope envelope;
                auto* update = envelope.mutable_spawn_updated();
                update->set_id(p.id);
                update->set_hp_cur(uint32_t(std::max(p.current, 0)));
                out.push_back(std::move(envelope));
            } else if constexpr (std::is_same_v<T, SpawnDied>) {
                seq::v1::Envelope envelope;
                auto* killed = envelope.mutable_spawn_killed();
                killed->set_deceased_id(p.id);
                killed->set_killer_id(p.has_killer_id ? p.killer_id : 0);
                out.push_back(std::move(envelope));
            } else if constexpr (std::is_same_v<T, SpawnIdentityUpdated>) {
                seq::v1::Envelope envelope;
                auto* update = envelope.mutable_spawn_updated();
                update->set_id(p.id); update->set_level(p.level);
                out.push_back(std::move(envelope));
            }
        }, event);
    }
    return out;
}

PlayerComparison comparePlayers(
    const Batch& rustBatch,
    const std::vector<PlayerObservation>& legacyEvents,
    const std::vector<seq::v1::Envelope>& legacyProjections)
{
    PlayerComparison comparison;
    const auto rustEvents = playerObservations(rustBatch);
    const auto rustProjections = projectPlayers(rustBatch);
    comparison.rustEventCount = rustEvents.size();
    comparison.legacyEventCount = legacyEvents.size();
    comparison.rustProjectionCount = rustProjections.size();
    comparison.legacyProjectionCount = legacyProjections.size();
    comparison.orderedEventsEqual = rustEvents == legacyEvents;
    comparison.projectionsEqual =
        rustProjections.size() == legacyProjections.size() &&
        std::equal(rustProjections.begin(), rustProjections.end(),
                   legacyProjections.begin(), sameEnvelope);
    return comparison;
}

std::vector<seq::v1::Envelope> projectLifecycle(const Batch& batch)
{
    std::vector<seq::v1::Envelope> projections;
    for (const Event& event : batch.events) {
        if (const auto* value = std::get_if<ZoneChanged>(&event)) {
#if defined(SEQ_TARGET_EQL)
            projections.push_back(seq::encode::zoneChanged(
                QString::fromUtf8(value->payload.short_name.data(),
                                  int(value->payload.short_name.size())),
                QString::fromUtf8(value->payload.long_name.data(),
                                  int(value->payload.long_name.size())),
                nullptr));
#else
            (void)value;
#endif
        } else if (const auto* value = std::get_if<TimeOfDay>(&event)) {
            QDateTime dateTime;
            dateTime.setDate(QDate(int(value->payload.year),
                                   int(value->payload.month),
                                   int(value->payload.day)));
            if (value->payload.hour >= 1 && value->payload.hour <= 24)
                dateTime.setTime(QTime(int(value->payload.hour - 1),
                                       int(value->payload.minute), 0));
            projections.push_back(seq::encode::eqTimeSync(dateTime));
        } else if (const auto* value = std::get_if<ZoneServerInfo>(&event)) {
            projections.push_back(seq::encode::zoneServer(
                QString::fromUtf8(value->payload.host.data(),
                                  int(value->payload.host.size())),
                value->payload.port));
        }
    }
    return projections;
}

LifecycleComparison compareLifecycle(
    const Batch& rustBatch,
    const std::vector<LifecycleObservation>& legacyEvents,
    const std::vector<seq::v1::Envelope>& legacyProjections)
{
    return compareLifecycle(lifecycleObservations(rustBatch),
                            projectLifecycle(rustBatch), legacyEvents,
                            legacyProjections);
}

LifecycleComparison compareLifecycle(
    const std::vector<LifecycleObservation>& rustEvents,
    const std::vector<seq::v1::Envelope>& rustProjections,
    const std::vector<LifecycleObservation>& legacyEvents,
    const std::vector<seq::v1::Envelope>& legacyProjections)
{
    LifecycleComparison comparison;
    comparison.rustEventCount = rustEvents.size();
    comparison.legacyEventCount = legacyEvents.size();
    comparison.rustProjectionCount = rustProjections.size();
    comparison.legacyProjectionCount = legacyProjections.size();
    comparison.orderedEventsEqual = rustEvents == legacyEvents;
    comparison.projectionsEqual =
        rustProjections.size() == legacyProjections.size() &&
        std::equal(rustProjections.begin(), rustProjections.end(),
                   legacyProjections.begin(), sameEnvelope);
    return comparison;
}

ProtocolRegistry::ProtocolRegistry(const std::string& protocolDir)
    : m_registry(rust::session_protocol_registry_new(::rust::Str(protocolDir)))
{
}

std::string ProtocolRegistry::contentHash(rust::SessionBackend backend) const
{
    return std::string(m_registry->content_hash(backend));
}

Session::Session(const ProtocolRegistry& registry,
                 rust::SessionBackend backend, size_t journalLimit,
                 size_t journalByteLimit,
                 LifecycleSelector lifecycleSelector,
                 EntitySelector entitySelector,
                 PlayerSelector playerSelector)
    : m_session(rust::session_new(registry.rustRegistry(), backend))
    , m_journalLimit(std::max<size_t>(journalLimit, 1))
    , m_journalByteLimit(std::max<size_t>(journalByteLimit, sizeof(Record)))
    , m_lifecycleSelector(lifecycleSelector)
    , m_entitySelector(entitySelector)
    , m_playerSelector(playerSelector)
{
}

const Record& Session::decode(Stream stream, uint16_t opcode,
                              Direction direction, const uint8_t* payload,
                              size_t payloadSize, int64_t timestamp)
{
    auto raw = m_session->decode(toRust(stream), opcode, toRust(direction),
                                 ::rust::Slice<const uint8_t>(payload, payloadSize),
                                 timestamp);
    Record record;
    record.packet = PacketRecord{stream, opcode, direction, payloadSize, timestamp};
    record.batch = translate(std::move(raw));
    return append(std::move(record));
}

const Record& Session::flush(FlushReason reason)
{
    Record record;
    record.flushReason = reason;
    record.batch = translate(m_session->flush(toRust(reason)));
    const Record& appended = append(std::move(record));
    return appended;
}

const Record& Session::append(Record record)
{
    record.sequence = ++m_recordCount;

    // Charge retained diagnostics by their source packet bytes plus fixed
    // record storage. Decoded fields are derived from that source and may use
    // more container overhead, so large single records become summaries before
    // they can dominate a per-Box journal.
    const size_t payloadBytes = record.packet ? record.packet->payloadSize : 0;
    record.retainedBytes = sizeof(Record) + payloadBytes;
    if (record.retainedBytes > m_journalByteLimit) {
        // Swap with fresh containers so an oversized decoded batch releases
        // its backing allocations; clear() alone would retain their capacity.
        decltype(record.batch.events){}.swap(record.batch.events);
        decltype(record.batch.selfStats){}.swap(record.batch.selfStats);
        decltype(record.batch.lootRows){}.swap(record.batch.lootRows);
        record.detailsOmitted = true;
        record.retainedBytes = sizeof(Record);
    }

    while (!m_journal.empty() &&
           (m_journal.size() >= m_journalLimit ||
            m_journalBytes + record.retainedBytes > m_journalByteLimit)) {
        m_journalBytes -= m_journal.front().retainedBytes;
        m_journal.pop_front();
        ++m_droppedRecordCount;
    }
    m_journal.push_back(std::move(record));
    m_journalBytes += m_journal.back().retainedBytes;
    return m_journal.back();
}

} // namespace seq::shadow
