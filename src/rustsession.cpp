#include "rustsession.h"

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
    appendScalar(out, p.cur_hp); appendScalar(out, p.mana);
    appendVector(out, p.aa_ids); appendVector(out, p.aa_values);
    appendScalar(out, p.aa_spent); appendVector(out, p.skills);
    appendScalar(out, p.class_mask);
    appendScalar(out, p.str_); appendScalar(out, p.sta);
    appendScalar(out, p.cha); appendScalar(out, p.dex);
    appendScalar(out, p.int_); appendScalar(out, p.agi);
    appendScalar(out, p.wis); appendScalar(out, p.platinum);
    appendScalar(out, p.gold); appendScalar(out, p.silver);
    appendScalar(out, p.copper);
}

bool sameEnvelope(const seq::v1::Envelope& lhs,
                  const seq::v1::Envelope& rhs)
{
    return lhs.SerializeAsString() == rhs.SerializeAsString();
}

} // namespace

Batch translate(rust::SessionDecodeBatch batch)
{
    Batch out;
    out.protocolGeneration = batch.protocol_generation;
    out.disposition = disposition(batch.disposition);
    out.events.reserve(batch.events.size());

    for (const rust::SessionEventRef& event : batch.events) {
        const uint32_t index = event.payload_index;
        switch (event.kind) {
        case rust::SessionEventKind::SpawnAdded:
            out.events.emplace_back(SpawnAdded{takePayload(batch.spawn_added, index)});
            break;
        case rust::SessionEventKind::SpawnMoved:
            out.events.emplace_back(SpawnMoved{takePayload(batch.spawn_moved, index)});
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

std::vector<seq::v1::Envelope> projectLifecycle(const Batch& batch)
{
    std::vector<seq::v1::Envelope> projections;
    for (const Event& event : batch.events) {
        if (const auto* value = std::get_if<ZoneChanged>(&event)) {
            seq::v1::Envelope envelope;
            auto* zone = envelope.mutable_zone_changed();
            zone->set_zone_short(std::string(value->payload.short_name));
            zone->set_zone_long(std::string(value->payload.long_name));
            projections.push_back(std::move(envelope));
        } else if (const auto* value = std::get_if<TimeOfDay>(&event)) {
            seq::v1::Envelope envelope;
            auto* time = envelope.mutable_eq_time_sync();
            time->set_year(value->payload.year);
            time->set_month(value->payload.month);
            time->set_day(value->payload.day);
            time->set_hour(value->payload.hour == 0 ? 0 : value->payload.hour - 1);
            time->set_minute(value->payload.minute);
            projections.push_back(std::move(envelope));
        } else if (const auto* value = std::get_if<ZoneServerInfo>(&event)) {
            seq::v1::Envelope envelope;
            auto* server = envelope.mutable_zone_server();
            server->set_host(std::string(value->payload.host));
            server->set_port(value->payload.port);
            projections.push_back(std::move(envelope));
        }
    }
    return projections;
}

LifecycleComparison compareLifecycle(
    const Batch& rustBatch,
    const std::vector<LifecycleObservation>& legacyEvents,
    const std::vector<seq::v1::Envelope>& legacyProjections)
{
    const auto rustEvents = lifecycleObservations(rustBatch);
    const auto rustProjections = projectLifecycle(rustBatch);
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
                 LifecycleSelector lifecycleSelector)
    : m_session(rust::session_new(registry.rustRegistry(), backend))
    , m_journalLimit(std::max<size_t>(journalLimit, 1))
    , m_journalByteLimit(std::max<size_t>(journalByteLimit, sizeof(Record)))
    , m_lifecycleSelector(lifecycleSelector)
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
