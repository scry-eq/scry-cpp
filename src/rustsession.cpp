#include "rustsession.h"
#include "protoencoder.h"

#include <algorithm>
#include <array>
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

void appendItem(std::vector<uint8_t>& out,
                const rust::EventItemTemplate& item)
{
    appendString(out, item.serial);
    appendString(out, item.name);
    appendString(out, item.lore_name);
    appendScalar(out, item.item_id);
    appendScalar(out, item.has_icon);
    if (item.has_icon) appendScalar(out, item.icon);
    appendScalar(out, item.has_stack_count);
    if (item.has_stack_count) appendScalar(out, item.stack_count);
    appendScalar(out, item.has_weight_tenths);
    if (item.has_weight_tenths) appendScalar(out, item.weight_tenths);
    appendScalar(out, item.has_flags);
    if (item.has_flags) appendScalar(out, item.flags);
    appendScalar(out, item.has_corruption);
    if (item.has_corruption) appendScalar(out, item.corruption);
    appendScalar(out, item.slot_mask);
    appendScalar(out, item.container_id);
    appendScalar(out, item.container_slot);
    appendScalar(out, item.parent_slot);
    appendVector(out, item.stats);
    appendVector(out, item.resists);
    appendScalar(out, item.hp);
    appendScalar(out, item.mana);
    appendScalar(out, item.endurance);
    appendScalar(out, item.ac);
}

void fillRustItem(seq::v1::Item* out, const rust::EventItemTemplate& item)
{
    out->set_id(item.item_id);
    out->set_name(std::string(item.name));
    if (item.lore_name != item.name)
        out->set_lore_name(std::string(item.lore_name));
    out->set_slot_mask(item.slot_mask);
    if (item.has_flags) out->set_flags(item.flags);
    if (item.has_weight_tenths)
        out->set_weight(float(item.weight_tenths) / 10.0f);
    out->set_hp(item.hp);
    out->set_mana(item.mana);
    out->set_endurance(item.endurance);
    out->set_ac(item.ac);
    for (int32_t value : item.stats) out->add_stats(value);
    for (int32_t value : item.resists) out->add_resists(value);
    if (item.has_corruption) out->set_corruption(item.corruption);
}

void fillItemTotals(seq::v1::ItemCacheTotals* out,
                    const ::rust::Vec<rust::EventItemTemplate>& items)
{
    out->set_item_count(uint32_t(items.size()));
    std::array<int64_t, 7> stats{};
    std::array<int64_t, 5> resists{};
    int64_t hp = 0, mana = 0, endurance = 0, ac = 0, corruption = 0;
    for (const auto& item : items) {
        hp += item.hp; mana += item.mana; endurance += item.endurance;
        ac += item.ac;
        if (item.has_corruption) corruption += item.corruption;
        for (size_t i = 0; i < stats.size() && i < item.stats.size(); ++i)
            stats[i] += item.stats[i];
        for (size_t i = 0; i < resists.size() && i < item.resists.size(); ++i)
            resists[i] += item.resists[i];
    }
    out->set_hp(int32_t(hp)); out->set_mana(int32_t(mana));
    out->set_endurance(int32_t(endurance)); out->set_ac(int32_t(ac));
    for (int64_t value : stats) out->add_stats(int32_t(value));
    for (int64_t value : resists) out->add_resists(int32_t(value));
    out->set_corruption(int32_t(corruption));
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
        case rust::SessionEventKind::InventorySnapshot:
            out.events.emplace_back(InventorySnapshot{
                takePayload(batch.inventory_snapshot, index)});
            break;
        case rust::SessionEventKind::InventoryItemUpdated:
            out.events.emplace_back(InventoryItemUpdated{
                takePayload(batch.inventory_item_updated, index)});
            break;
        case rust::SessionEventKind::EquipmentSnapshot:
            out.events.emplace_back(EquipmentSnapshot{
                takePayload(batch.equipment_snapshot, index)});
            break;
        case rust::SessionEventKind::EquipmentSlotUpdated:
            out.events.emplace_back(EquipmentSlotUpdated{
                takePayload(batch.equipment_slot_updated, index)});
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
        case rust::SessionEventKind::CombatDamage:
            out.events.emplace_back(CombatDamage{
                takePayload(batch.combat_damage, index)});
            break;
        case rust::SessionEventKind::SpellAction:
            out.events.emplace_back(SpellAction{
                takePayload(batch.spell_action, index)});
            break;
        case rust::SessionEventKind::SpellActionResolved:
            out.events.emplace_back(SpellActionResolved{
                takePayload(batch.spell_action_resolved, index)});
            break;
        case rust::SessionEventKind::SpellCastRequest:
            out.events.emplace_back(SpellCastRequest{
                takePayload(batch.spell_cast_request, index)});
            break;
        case rust::SessionEventKind::SpawnCast:
            out.events.emplace_back(SpawnCast{takePayload(batch.spawn_cast, index)});
            break;
        case rust::SessionEventKind::SpellCastStarted:
            out.events.emplace_back(SpellCastStarted{
                takePayload(batch.spell_cast_started, index)});
            break;
        case rust::SessionEventKind::SpellCastInterrupted:
            out.events.emplace_back(SpellCastInterrupted{
                takePayload(batch.spell_cast_interrupted, index)});
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
        case rust::SessionEventKind::AlternateAbilityDefined:
            out.events.emplace_back(AlternateAbilityDefined{
                takePayload(batch.alternate_ability_defined, index)});
            break;
        case rust::SessionEventKind::Exp:
            out.events.emplace_back(Exp{takePayload(batch.exp, index)});
            break;
        case rust::SessionEventKind::ExperienceUpdated:
            out.events.emplace_back(ExperienceUpdated{
                takePayload(batch.experience_updated, index)});
            break;
        case rust::SessionEventKind::AaExp:
            out.events.emplace_back(AaExp{takePayload(batch.aa_exp, index)});
            break;
        case rust::SessionEventKind::AlternateAdvancementSnapshot:
            out.events.emplace_back(AlternateAdvancementSnapshot{
                takePayload(batch.alternate_advancement_snapshot, index)});
            break;
        case rust::SessionEventKind::AlternateAdvancementUpdated:
            out.events.emplace_back(AlternateAdvancementUpdated{
                takePayload(batch.alternate_advancement_updated, index)});
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
        case rust::SessionEventKind::SkillsSnapshot:
            out.events.emplace_back(SkillsSnapshot{
                takePayload(batch.skills_snapshot, index)});
            break;
        case rust::SessionEventKind::SkillValueUpdated:
            out.events.emplace_back(SkillValueUpdated{
                takePayload(batch.skill_value_updated, index)});
            break;
        case rust::SessionEventKind::LootTransaction:
            out.events.emplace_back(LootTransaction{
                takePayload(batch.loot_transaction, index)});
            break;
        case rust::SessionEventKind::LootDrops:
            out.events.emplace_back(LootDrops{takePayload(batch.loot_drops, index)});
            break;
        case rust::SessionEventKind::CorpseLootSnapshot:
            out.events.emplace_back(CorpseLootSnapshot{
                takePayload(batch.corpse_loot_snapshot, index)});
            break;
        case rust::SessionEventKind::LootAcquired:
            out.events.emplace_back(LootAcquired{
                takePayload(batch.loot_acquired, index)});
            break;
        case rust::SessionEventKind::Money:
            out.events.emplace_back(Money{takePayload(batch.money, index)});
            break;
        case rust::SessionEventKind::MoneyBalanceUpdated:
            out.events.emplace_back(MoneyBalanceUpdated{
                takePayload(batch.money_balance_updated, index)});
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
        case rust::SessionEventKind::ChatMessage:
            out.events.emplace_back(ChatMessage{
                takePayload(batch.chat_message, index)});
            break;
        case rust::SessionEventKind::UcsRecord:
            out.events.emplace_back(UcsRecord{
                takePayload(batch.ucs_record, index)});
            break;
        case rust::SessionEventKind::BuffList:
            out.events.emplace_back(BuffList{takePayload(batch.buff_list, index)});
            break;
        case rust::SessionEventKind::BuffWire:
            out.events.emplace_back(BuffWire{
                takePayload(batch.buff_wire, index)});
            break;
        case rust::SessionEventKind::BuffAdded:
            out.events.emplace_back(BuffAdded{
                takePayload(batch.buff_added, index)});
            break;
        case rust::SessionEventKind::BuffUpdated:
            out.events.emplace_back(BuffUpdated{
                takePayload(batch.buff_updated, index)});
            break;
        case rust::SessionEventKind::BuffRemoved:
            out.events.emplace_back(BuffRemoved{
                takePayload(batch.buff_removed, index)});
            break;
        case rust::SessionEventKind::GroupFollow:
            out.events.emplace_back(GroupFollow{takePayload(batch.group_follow, index)});
            break;
        case rust::SessionEventKind::GroupDisband:
            out.events.emplace_back(GroupDisband{
                takePayload(batch.group_disband, index)});
            break;
        case rust::SessionEventKind::GroupRosterWire:
            out.events.emplace_back(GroupRosterWire{
                takePayload(batch.group_roster_wire, index)});
            break;
        case rust::SessionEventKind::GroupRosterUpdated:
            out.events.emplace_back(GroupRosterUpdated{
                takePayload(batch.group_roster_updated, index)});
            break;
        case rust::SessionEventKind::GuildRosterUpdated:
            out.events.emplace_back(GuildRosterUpdated{
                takePayload(batch.guild_roster_updated, index)});
            break;
        case rust::SessionEventKind::GuildRosterWire:
            out.events.emplace_back(GuildRosterWire{
                takePayload(batch.guild_roster_wire, index)});
            break;
        case rust::SessionEventKind::GuildMemberStatus:
            out.events.emplace_back(GuildMemberStatus{
                takePayload(batch.guild_member_status, index)});
            break;
        case rust::SessionEventKind::GuildMotdUpdated:
            out.events.emplace_back(GuildMotdUpdated{
                takePayload(batch.guild_motd_updated, index)});
            break;
        case rust::SessionEventKind::GuildRankNamesUpdated:
            out.events.emplace_back(GuildRankNamesUpdated{
                takePayload(batch.guild_rank_names_updated, index)});
            break;
        case rust::SessionEventKind::DynamicZoneInfo:
            out.events.emplace_back(DynamicZoneInfo{
                takePayload(batch.dynamic_zone_info, index)});
            break;
        case rust::SessionEventKind::DynamicZoneSwitch:
            out.events.emplace_back(DynamicZoneSwitch{
                takePayload(batch.dynamic_zone_switch, index)});
            break;
        case rust::SessionEventKind::DynamicZoneUpdated:
            out.events.emplace_back(DynamicZoneUpdated{
                takePayload(batch.dynamic_zone_updated, index)});
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

bool isProgressionEvent(const Event& event)
{
    return std::visit([](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        return std::is_same_v<T, InventorySnapshot> ||
               std::is_same_v<T, InventoryItemUpdated> ||
               std::is_same_v<T, EquipmentSnapshot> ||
               std::is_same_v<T, EquipmentSlotUpdated> ||
               std::is_same_v<T, MoneyBalanceUpdated> ||
               std::is_same_v<T, SkillsSnapshot> ||
               std::is_same_v<T, SkillValueUpdated> ||
               std::is_same_v<T, ExperienceUpdated> ||
               std::is_same_v<T, AlternateAdvancementSnapshot> ||
               std::is_same_v<T, AlternateAdvancementUpdated> ||
               std::is_same_v<T, AlternateAbilityDefined>;
    }, event);
}

std::vector<ProgressionObservation> progressionObservations(const Batch& batch)
{
    std::vector<ProgressionObservation> out;
    for (const Event& event : batch.events) {
        std::visit([&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            const auto& p = value.payload;
            ProgressionObservation observation;
            bool selected = true;
            if constexpr (std::is_same_v<T, InventorySnapshot>) {
                observation.kind = ProgressionKind::InventorySnapshot;
                appendScalar(observation.payload, uint64_t(p.items.size()));
                for (const auto& item : p.items) appendItem(observation.payload, item);
            } else if constexpr (std::is_same_v<T, InventoryItemUpdated>) {
                observation.kind = ProgressionKind::InventoryItemUpdated;
                appendItem(observation.payload, p.item);
                appendScalar(observation.payload, p.has_previous_location);
                if (p.has_previous_location) {
                    appendScalar(observation.payload, p.previous_location.container_id);
                    appendScalar(observation.payload, p.previous_location.container_slot);
                    appendScalar(observation.payload, p.previous_location.parent_slot);
                }
            } else if constexpr (std::is_same_v<T, EquipmentSnapshot>) {
                observation.kind = ProgressionKind::EquipmentSnapshot;
                appendScalar(observation.payload, uint64_t(p.items.size()));
                for (const auto& item : p.items) appendItem(observation.payload, item);
            } else if constexpr (std::is_same_v<T, EquipmentSlotUpdated>) {
                observation.kind = ProgressionKind::EquipmentSlotUpdated;
                appendScalar(observation.payload, p.slot);
                appendScalar(observation.payload, p.has_item);
                if (p.has_item) appendItem(observation.payload, p.item);
            } else if constexpr (std::is_same_v<T, MoneyBalanceUpdated>) {
                observation.kind = ProgressionKind::MoneyBalanceUpdated;
                appendScalar(observation.payload, p.platinum);
                appendScalar(observation.payload, p.gold);
                appendScalar(observation.payload, p.silver);
                appendScalar(observation.payload, p.copper);
            } else if constexpr (std::is_same_v<T, SkillsSnapshot>) {
                observation.kind = ProgressionKind::SkillsSnapshot;
                appendScalar(observation.payload, uint64_t(p.skills.size()));
                for (const auto& skill : p.skills) {
                    appendScalar(observation.payload, skill.skill_id);
                    appendScalar(observation.payload, skill.value);
                }
            } else if constexpr (std::is_same_v<T, SkillValueUpdated>) {
                observation.kind = ProgressionKind::SkillValueUpdated;
                appendScalar(observation.payload, p.skill_id);
                appendScalar(observation.payload, p.value);
            } else if constexpr (std::is_same_v<T, ExperienceUpdated>) {
                observation.kind = ProgressionKind::ExperienceUpdated;
                appendScalar(observation.payload, p.experience);
                appendScalar(observation.payload, p.has_level);
                if (p.has_level) appendScalar(observation.payload, p.level);
                appendScalar(observation.payload, p.has_previous_level);
                if (p.has_previous_level)
                    appendScalar(observation.payload, p.previous_level);
            } else if constexpr (std::is_same_v<T, AlternateAdvancementSnapshot>) {
                observation.kind = ProgressionKind::AlternateAdvancementSnapshot;
                appendScalar(observation.payload, uint64_t(p.purchased.size()));
                for (const auto& aa : p.purchased) {
                    appendScalar(observation.payload, aa.ability_id);
                    appendScalar(observation.payload, aa.rank);
                }
                appendScalar(observation.payload, p.has_spent_points);
                if (p.has_spent_points) appendScalar(observation.payload, p.spent_points);
                appendScalar(observation.payload, p.has_assigned_points);
                if (p.has_assigned_points) appendScalar(observation.payload, p.assigned_points);
                appendScalar(observation.payload, p.unspent_points);
                appendScalar(observation.payload, p.experience);
            } else if constexpr (std::is_same_v<T, AlternateAdvancementUpdated>) {
                observation.kind = ProgressionKind::AlternateAdvancementUpdated;
                appendScalar(observation.payload, p.experience);
                appendScalar(observation.payload, p.unspent_points);
            } else if constexpr (std::is_same_v<T, AlternateAbilityDefined>) {
                observation.kind = ProgressionKind::AlternateAbilityDefined;
                appendScalar(observation.payload, p.ability_id);
                appendScalar(observation.payload, p.title_string_id);
            } else {
                selected = false;
            }
            if (selected) out.push_back(std::move(observation));
        }, event);
    }
    return out;
}

std::vector<seq::v1::Envelope> projectProgression(const Batch& batch)
{
    std::vector<seq::v1::Envelope> out;
    for (const Event& event : batch.events) {
        std::visit([&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            const auto& p = value.payload;
            if constexpr (std::is_same_v<T, InventorySnapshot>) {
                for (const auto& item : p.items) {
                    seq::v1::Envelope envelope;
                    fillRustItem(envelope.mutable_item_learned()->mutable_item(), item);
                    out.push_back(std::move(envelope));
                }
            } else if constexpr (std::is_same_v<T, InventoryItemUpdated>) {
                seq::v1::Envelope envelope;
                fillRustItem(envelope.mutable_item_learned()->mutable_item(), p.item);
                out.push_back(std::move(envelope));
            } else if constexpr (std::is_same_v<T, EquipmentSnapshot>) {
                seq::v1::Envelope wornEnvelope;
                auto* worn = wornEnvelope.mutable_worn_set();
                for (const auto& item : p.items) {
                    worn->add_slot_indices(item.container_slot);
                    worn->add_item_ids(item.item_id);
                }
                out.push_back(std::move(wornEnvelope));
                seq::v1::Envelope totals;
                fillItemTotals(totals.mutable_item_totals(), p.items);
                out.push_back(std::move(totals));
            } else if constexpr (std::is_same_v<T, EquipmentSlotUpdated>) {
                seq::v1::Envelope wornEnvelope;
                if (p.has_item) {
                    wornEnvelope.mutable_worn_set()->add_slot_indices(p.slot);
                    wornEnvelope.mutable_worn_set()->add_item_ids(p.item.item_id);
                } else {
                    wornEnvelope.mutable_worn_set();
                }
                out.push_back(std::move(wornEnvelope));
                seq::v1::Envelope totals;
                if (p.has_item) {
                    // CXX vectors cannot copy their element, so fill this one
                    // projection directly instead of manufacturing a vector.
                    auto* target = totals.mutable_item_totals();
                    target->set_item_count(1);
                    target->set_hp(p.item.hp); target->set_mana(p.item.mana);
                    target->set_endurance(p.item.endurance); target->set_ac(p.item.ac);
                    for (int32_t v : p.item.stats) target->add_stats(v);
                    for (int32_t v : p.item.resists) target->add_resists(v);
                    if (p.item.has_corruption) target->set_corruption(p.item.corruption);
                } else {
                    totals.mutable_item_totals();
                }
                out.push_back(std::move(totals));
            } else if constexpr (std::is_same_v<T, MoneyBalanceUpdated>) {
                seq::v1::Envelope envelope;
                const uint64_t total = uint64_t(p.platinum) * 1000 +
                    uint64_t(p.gold) * 100 + uint64_t(p.silver) * 10 + p.copper;
                envelope.mutable_player_stats()->set_money_copper(
                    uint32_t(std::min<uint64_t>(total, UINT32_MAX)));
                out.push_back(std::move(envelope));
            } else if constexpr (std::is_same_v<T, SkillsSnapshot>) {
                seq::v1::Envelope envelope;
                for (const auto& skill : p.skills) {
                    auto* target = envelope.mutable_player_stats()->add_skills();
                    target->set_skill_id(skill.skill_id); target->set_value(skill.value);
                }
                out.push_back(std::move(envelope));
            } else if constexpr (std::is_same_v<T, SkillValueUpdated>) {
                seq::v1::Envelope envelope;
                auto* target = envelope.mutable_player_stats()->add_skills();
                target->set_skill_id(p.skill_id); target->set_value(p.value);
                out.push_back(std::move(envelope));
            } else if constexpr (std::is_same_v<T, ExperienceUpdated>) {
                seq::v1::Envelope envelope;
                auto* stats = envelope.mutable_player_stats();
                stats->set_exp_cur(p.experience); stats->set_exp_max(100000);
                if (p.has_level) stats->set_level(p.level);
                out.push_back(std::move(envelope));
            } else if constexpr (std::is_same_v<T, AlternateAdvancementSnapshot>) {
                seq::v1::Envelope envelope;
                auto* stats = envelope.mutable_player_stats();
                stats->set_aa_exp_cur(p.experience); stats->set_aa_exp_max(100000);
                if (p.has_spent_points) stats->set_aa_points(p.spent_points);
                stats->set_aa_unspent(p.unspent_points);
                for (const auto& aa : p.purchased) {
                    auto* target = stats->add_purchased_aa();
                    target->set_ability_id(aa.ability_id); target->set_rank(aa.rank);
                }
                out.push_back(std::move(envelope));
            } else if constexpr (std::is_same_v<T, AlternateAdvancementUpdated>) {
                seq::v1::Envelope envelope;
                auto* stats = envelope.mutable_player_stats();
                stats->set_aa_exp_cur(p.experience); stats->set_aa_exp_max(100000);
                stats->set_aa_unspent(p.unspent_points);
                out.push_back(std::move(envelope));
            }
        }, event);
    }
    return out;
}

ProgressionComparison compareProgression(
    const Batch& rustBatch,
    const std::vector<ProgressionObservation>& legacyEvents,
    const std::vector<seq::v1::Envelope>& legacyProjections)
{
    ProgressionComparison comparison;
    const auto rustEvents = progressionObservations(rustBatch);
    const auto rustProjections = projectProgression(rustBatch);
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

bool isLootEvent(const Event& event)
{
    return std::holds_alternative<CorpseLootSnapshot>(event) ||
           std::holds_alternative<LootAcquired>(event);
}

bool isCombatEvent(const Event& event)
{
    return std::holds_alternative<CombatDamage>(event) ||
           std::holds_alternative<SpellActionResolved>(event) ||
           std::holds_alternative<SpellCastStarted>(event) ||
           std::holds_alternative<SpellCastInterrupted>(event) ||
           std::holds_alternative<BuffAdded>(event) ||
           std::holds_alternative<BuffUpdated>(event) ||
           std::holds_alternative<BuffRemoved>(event);
}

std::vector<LootObservation> lootObservations(const Batch& batch)
{
    std::vector<LootObservation> out;
    for (const Event& event : batch.events) {
        std::visit([&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            const auto& p = value.payload;
            LootObservation observation;
            if constexpr (std::is_same_v<T, CorpseLootSnapshot>) {
                observation.kind = LootKind::CorpseLootSnapshot;
                appendScalar(observation.payload, p.timestamp);
                appendScalar(observation.payload, p.corpse_id);
                appendString(observation.payload, p.corpse_name);
                appendString(observation.payload, p.corpse_name_normalized);
                appendString(observation.payload, p.zone_short);
                appendString(observation.payload, p.zone_base);
                appendString(observation.payload, p.instance);
                appendString(observation.payload, p.looter);
                appendScalar(observation.payload, uint64_t(p.items.size()));
                for (const auto& item : p.items) {
                    appendString(observation.payload, item.name);
                    appendScalar(observation.payload, item.icon);
                    appendScalar(observation.payload, item.item_id);
                }
                out.push_back(std::move(observation));
            } else if constexpr (std::is_same_v<T, LootAcquired>) {
                observation.kind = LootKind::LootAcquired;
                appendScalar(observation.payload, p.timestamp);
                appendString(observation.payload, p.item_name);
                appendScalar(observation.payload, p.has_item_id);
                if (p.has_item_id) appendScalar(observation.payload, p.item_id);
                appendScalar(observation.payload, p.quantity);
                appendString(observation.payload, p.corpse_name);
                appendString(observation.payload, p.corpse_name_normalized);
                appendScalar(observation.payload, p.has_corpse_id);
                if (p.has_corpse_id) appendScalar(observation.payload, p.corpse_id);
                appendString(observation.payload, p.zone_short);
                appendString(observation.payload, p.zone_base);
                appendString(observation.payload, p.instance);
                appendScalar(observation.payload, p.sold);
                appendScalar(observation.payload, p.coin_copper);
                appendString(observation.payload, p.disposition);
                appendString(observation.payload, p.looter);
                appendScalar(observation.payload, p.has_sequence);
                if (p.has_sequence) appendScalar(observation.payload, p.sequence);
                appendScalar(observation.payload, p.from_corpse);
                appendScalar(observation.payload, p.complete);
                out.push_back(std::move(observation));
            }
        }, event);
    }
    return out;
}

std::vector<seq::v1::Envelope> projectLoot(const Batch& batch)
{
    std::vector<seq::v1::Envelope> out;
    for (const Event& event : batch.events) {
        std::visit([&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            const auto& p = value.payload;
            if constexpr (std::is_same_v<T, CorpseLootSnapshot>) {
                seq::v1::Envelope envelope;
                auto* loot = envelope.mutable_loot_drops();
                loot->set_corpse_id(p.corpse_id);
                loot->set_corpse_name(std::string(p.corpse_name));
                for (const auto& item : p.items) {
                    auto* target = loot->add_items();
                    target->set_name(std::string(item.name));
                    target->set_icon(item.icon);
                    target->set_item_id(item.item_id);
                }
                out.push_back(std::move(envelope));
            } else if constexpr (std::is_same_v<T, LootAcquired>) {
                // The legacy public event represented a server confirmation.
                // A narration abandoned at a boundary belongs in history but
                // must not manufacture a live LootTransaction.
                if (!p.complete && !p.has_sequence) return;
                seq::v1::Envelope envelope;
                auto* loot = envelope.mutable_loot_transaction();
                loot->set_corpse_id(p.has_corpse_id ? p.corpse_id : 0);
                loot->set_item_id(p.has_item_id ? p.item_id : 0);
                loot->set_quantity(p.quantity);
                loot->set_coin_copper(p.coin_copper);
                loot->set_coin_from_corpse(p.from_corpse);
                out.push_back(std::move(envelope));
            }
        }, event);
    }
    return out;
}

LootComparison compareLoot(
    const Batch& rustBatch,
    const std::vector<LootObservation>& legacyEvents,
    const std::vector<seq::v1::Envelope>& legacyProjections)
{
    LootComparison comparison;
    const auto rustEvents = lootObservations(rustBatch);
    const auto rustProjections = projectLoot(rustBatch);
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

std::vector<CombatObservation> combatObservations(const Batch& batch)
{
    std::vector<CombatObservation> out;
    for (const Event& event : batch.events) {
        std::visit([&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            const auto& p = value.payload;
            CombatObservation observation;
            if constexpr (std::is_same_v<T, CombatDamage>) {
                observation.kind = CombatKind::CombatDamage;
                appendScalar(observation.payload, p.has_source_id);
                if (p.has_source_id) appendScalar(observation.payload, p.source_id);
                appendScalar(observation.payload, p.has_target_id);
                if (p.has_target_id) appendScalar(observation.payload, p.target_id);
                appendScalar(observation.payload, p.kind);
                appendScalar(observation.payload, p.damage);
                appendScalar(observation.payload, p.has_spell_id);
                if (p.has_spell_id) appendScalar(observation.payload, p.spell_id);
                out.push_back(std::move(observation));
            } else if constexpr (std::is_same_v<T, SpellActionResolved>) {
                observation.kind = CombatKind::SpellActionResolved;
                appendScalar(observation.payload, p.has_source_id);
                if (p.has_source_id) appendScalar(observation.payload, p.source_id);
                appendScalar(observation.payload, p.has_target_id);
                if (p.has_target_id) appendScalar(observation.payload, p.target_id);
                appendScalar(observation.payload, p.spell_id);
                appendScalar(observation.payload, p.has_caster_level);
                if (p.has_caster_level)
                    appendScalar(observation.payload, p.caster_level);
                appendScalar(observation.payload, p.kind);
                out.push_back(std::move(observation));
            } else if constexpr (std::is_same_v<T, SpellCastStarted>) {
                observation.kind = CombatKind::SpellCastStarted;
                appendScalar(observation.payload, p.has_caster_id);
                if (p.has_caster_id) appendScalar(observation.payload, p.caster_id);
                appendScalar(observation.payload, p.has_target_id);
                if (p.has_target_id) appendScalar(observation.payload, p.target_id);
                appendScalar(observation.payload, p.spell_id);
                appendScalar(observation.payload, p.has_cast_time_ms);
                if (p.has_cast_time_ms)
                    appendScalar(observation.payload, p.cast_time_ms);
                appendScalar(observation.payload, p.has_slot);
                if (p.has_slot) appendScalar(observation.payload, p.slot);
                out.push_back(std::move(observation));
            } else if constexpr (std::is_same_v<T, SpellCastInterrupted>) {
                observation.kind = CombatKind::SpellCastInterrupted;
                appendScalar(observation.payload, p.has_caster_id);
                if (p.has_caster_id) appendScalar(observation.payload, p.caster_id);
                appendScalar(observation.payload, p.has_target_id);
                if (p.has_target_id) appendScalar(observation.payload, p.target_id);
                appendScalar(observation.payload, p.spell_id);
                appendScalar(observation.payload, p.reason);
                out.push_back(std::move(observation));
            } else if constexpr (std::is_same_v<T, BuffAdded> ||
                                 std::is_same_v<T, BuffUpdated>) {
                observation.kind = std::is_same_v<T, BuffAdded>
                    ? CombatKind::BuffAdded : CombatKind::BuffUpdated;
                appendScalar(observation.payload, p.has_owner_id);
                if (p.has_owner_id) appendScalar(observation.payload, p.owner_id);
                appendScalar(observation.payload, p.spell_id);
                appendScalar(observation.payload, p.has_remaining_ticks);
                if (p.has_remaining_ticks)
                    appendScalar(observation.payload, p.remaining_ticks);
                appendScalar(observation.payload, p.has_slot);
                if (p.has_slot) appendScalar(observation.payload, p.slot);
                appendScalar(observation.payload, p.has_caster_id);
                if (p.has_caster_id) appendScalar(observation.payload, p.caster_id);
                appendScalar(observation.payload, p.has_caster_name);
                if (p.has_caster_name) appendString(observation.payload, p.caster_name);
                out.push_back(std::move(observation));
            } else if constexpr (std::is_same_v<T, BuffRemoved>) {
                observation.kind = CombatKind::BuffRemoved;
                appendScalar(observation.payload, p.has_owner_id);
                if (p.has_owner_id) appendScalar(observation.payload, p.owner_id);
                appendScalar(observation.payload, p.spell_id);
                appendScalar(observation.payload, p.has_slot);
                if (p.has_slot) appendScalar(observation.payload, p.slot);
                out.push_back(std::move(observation));
            }
        }, event);
    }
    return out;
}

std::vector<seq::v1::Envelope> projectCombat(const Batch& batch)
{
    std::vector<seq::v1::Envelope> out;
    for (const Event& event : batch.events) {
        if (const auto* value = std::get_if<CombatDamage>(&event)) {
            const auto& p = value->payload;
            seq::v1::Envelope envelope;
            auto* combat = envelope.mutable_combat();
            combat->set_source_id(p.has_source_id ? p.source_id : 0);
            combat->set_target_id(p.has_target_id ? p.target_id : 0);
            combat->set_type(p.kind);
            combat->set_damage(p.damage);
            combat->set_spell_id(p.has_spell_id ? p.spell_id : 0);
            out.push_back(std::move(envelope));
        } else if (const auto* value = std::get_if<SpellCastStarted>(&event)) {
            const auto& p = value->payload;
            if (!p.has_cast_time_ms) continue;
            seq::v1::Envelope envelope;
            auto* cast = envelope.mutable_spawn_cast();
            cast->set_caster_id(p.has_caster_id ? p.caster_id : 0);
            cast->set_spell_id(p.spell_id);
            cast->set_cast_time_ms(p.has_cast_time_ms ? p.cast_time_ms : 0);
            out.push_back(std::move(envelope));
        }
    }
    return out;
}

CombatComparison compareCombat(
    const Batch& rustBatch,
    const std::vector<CombatObservation>& legacyEvents,
    const std::vector<seq::v1::Envelope>& legacyProjections)
{
    CombatComparison comparison;
    const auto rustEvents = combatObservations(rustBatch);
    const auto rustProjections = projectCombat(rustBatch);
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

bool isCommunicationEvent(const Event& event)
{
    return std::holds_alternative<ChatMessage>(event) ||
           std::holds_alternative<GroupRosterUpdated>(event) ||
           std::holds_alternative<GuildRosterUpdated>(event) ||
           std::holds_alternative<GuildMotdUpdated>(event) ||
           std::holds_alternative<GuildRankNamesUpdated>(event) ||
           std::holds_alternative<DynamicZoneUpdated>(event);
}

std::vector<CommunicationObservation> communicationObservations(
    const Batch& batch)
{
    std::vector<CommunicationObservation> out;
    for (const Event& event : batch.events) {
        std::visit([&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            const auto& p = value.payload;
            CommunicationObservation observation;
            if constexpr (std::is_same_v<T, ChatMessage>) {
                observation.kind = CommunicationKind::ChatMessage;
                appendScalar(observation.payload, p.kind);
                appendScalar(observation.payload, p.channel);
                appendString(observation.payload, p.from);
                appendString(observation.payload, p.target);
                appendString(observation.payload, p.text);
                appendScalar(observation.payload, p.chat_color);
                appendString(observation.payload, p.channel_name);
                appendScalar(observation.payload, p.has_format_id);
                if (p.has_format_id)
                    appendScalar(observation.payload, p.format_id);
                appendScalar(observation.payload, uint64_t(p.args.size()));
                for (const auto& arg : p.args)
                    appendString(observation.payload, arg);
                out.push_back(std::move(observation));
            } else if constexpr (std::is_same_v<T, GroupRosterUpdated>) {
                observation.kind = CommunicationKind::GroupRosterUpdated;
                appendScalar(observation.payload, p.has_group_id);
                if (p.has_group_id)
                    appendScalar(observation.payload, p.group_id);
                appendScalar(observation.payload, uint64_t(p.members.size()));
                for (const auto& member : p.members) {
                    appendScalar(observation.payload, member.slot);
                    appendString(observation.payload, member.name);
                    appendScalar(observation.payload, member.has_level);
                    if (member.has_level)
                        appendScalar(observation.payload, member.level);
                }
                appendScalar(observation.payload, p.complete);
                out.push_back(std::move(observation));
            } else if constexpr (std::is_same_v<T, GuildRosterUpdated>) {
                observation.kind = CommunicationKind::GuildRosterUpdated;
                appendScalar(observation.payload, p.guild_id);
                appendScalar(observation.payload, uint64_t(p.members.size()));
                for (const auto& member : p.members) {
                    appendString(observation.payload, member.name);
                    appendScalar(observation.payload, member.level);
                    appendScalar(observation.payload, member.class_);
                    appendScalar(observation.payload, member.class_mask);
                    appendScalar(observation.payload, member.rank);
                    appendScalar(observation.payload, member.last_on);
                    appendScalar(observation.payload, member.banker);
                    appendScalar(observation.payload, member.alt);
                    appendScalar(observation.payload, member.full_member);
                    appendString(observation.payload, member.public_note);
                    appendScalar(observation.payload, member.zone_id);
                }
                appendScalar(observation.payload, p.complete);
                out.push_back(std::move(observation));
            } else if constexpr (std::is_same_v<T, GuildMotdUpdated>) {
                observation.kind = CommunicationKind::GuildMotdUpdated;
                appendScalar(observation.payload, p.guild_id);
                appendString(observation.payload, p.message);
                appendString(observation.payload, p.sender);
                out.push_back(std::move(observation));
            } else if constexpr (std::is_same_v<T, GuildRankNamesUpdated>) {
                observation.kind = CommunicationKind::GuildRankNamesUpdated;
                appendScalar(observation.payload, p.guild_id);
                appendScalar(observation.payload, uint64_t(p.ranks.size()));
                for (const auto& rank : p.ranks) {
                    appendScalar(observation.payload, rank.rank_index);
                    appendString(observation.payload, rank.rank_name);
                }
                out.push_back(std::move(observation));
            } else if constexpr (std::is_same_v<T, DynamicZoneUpdated>) {
                observation.kind = CommunicationKind::DynamicZoneUpdated;
                appendScalar(observation.payload, p.active);
                appendScalar(observation.payload, p.has_zone_id);
                if (p.has_zone_id) appendScalar(observation.payload, p.zone_id);
                appendScalar(observation.payload, p.has_instance_id);
                if (p.has_instance_id)
                    appendScalar(observation.payload, p.instance_id);
                appendScalar(observation.payload, p.has_kind);
                if (p.has_kind) appendScalar(observation.payload, p.kind);
                appendScalar(observation.payload, p.has_position);
                if (p.has_position) {
                    appendScalar(observation.payload, p.position.x);
                    appendScalar(observation.payload, p.position.y);
                    appendScalar(observation.payload, p.position.z);
                }
                appendScalar(observation.payload, p.has_max_players);
                if (p.has_max_players)
                    appendScalar(observation.payload, p.max_players);
                appendString(observation.payload, p.expedition_name);
                appendString(observation.payload, p.leader_name);
                appendScalar(observation.payload, p.complete);
                out.push_back(std::move(observation));
            }
        }, event);
    }
    return out;
}

std::vector<seq::v1::Envelope> projectCommunication(
    const Batch& batch, const ChatTextResolver& resolveText)
{
    std::vector<seq::v1::Envelope> out;
    for (const Event& event : batch.events) {
        if (const auto* value = std::get_if<ChatMessage>(&event)) {
            const auto& p = value->payload;
            std::string text(p.text);
            if (p.has_format_id && resolveText) {
                std::vector<std::string> args;
                args.reserve(p.args.size());
                for (const auto& arg : p.args) args.emplace_back(arg);
                text = resolveText(p.format_id, args);
            }
            if (text.empty()) continue;
            seq::v1::Envelope envelope;
            auto* chat = envelope.mutable_chat();
            chat->set_channel(p.channel);
            chat->set_from(std::string(p.from));
            chat->set_target(std::string(p.target));
            chat->set_text(std::move(text));
            chat->set_chat_color(p.chat_color);
            chat->set_channel_name(std::string(p.channel_name));
            out.push_back(std::move(envelope));
        } else if (const auto* value =
                       std::get_if<GroupRosterUpdated>(&event)) {
            seq::v1::Envelope envelope;
            auto* group = envelope.mutable_group();
            for (uint32_t slot = 0; slot < 5; ++slot) {
                auto* target = group->add_members();
                target->set_slot(slot);
                const auto& members = value->payload.members;
                const auto found = std::find_if(
                    members.begin(), members.end(), [slot](const auto& member) {
                        return member.slot == slot;
                    });
                if (found != members.end()) {
                    target->set_name(std::string(found->name));
                    if (found->has_level) target->set_level(found->level);
                }
                target->set_in_zone(false);
            }
            out.push_back(std::move(envelope));
        } else if (const auto* value =
                       std::get_if<GuildRosterUpdated>(&event)) {
            seq::v1::Envelope envelope;
            auto* guild = envelope.mutable_guild_roster();
            guild->set_guild_id(value->payload.guild_id);
            std::vector<const rust::EventGuildRosterMember*> members;
            members.reserve(value->payload.members.size());
            for (const auto& member : value->payload.members)
                members.push_back(&member);
            std::sort(members.begin(), members.end(), [](const auto* left,
                                                         const auto* right) {
                return std::string(left->name) < std::string(right->name);
            });
            for (const auto* member : members) {
                auto* target = guild->add_members();
                target->set_name(std::string(member->name));
                target->set_level(member->level);
                target->set_class_(member->class_);
                target->set_class_mask(member->class_mask);
                target->set_rank(member->rank);
                target->set_last_on(member->last_on);
                target->set_banker(member->banker);
                target->set_alt(member->alt);
                target->set_full_member(member->full_member);
                target->set_public_note(std::string(member->public_note));
                target->set_zone_id(member->zone_id);
            }
            out.push_back(std::move(envelope));
        } else if (const auto* value =
                       std::get_if<GuildMotdUpdated>(&event)) {
            seq::v1::Envelope envelope;
            auto* motd = envelope.mutable_guild_motd();
            motd->set_guild_id(value->payload.guild_id);
            motd->set_message(std::string(value->payload.message));
            motd->set_sender(std::string(value->payload.sender));
            out.push_back(std::move(envelope));
        } else if (const auto* value =
                       std::get_if<GuildRankNamesUpdated>(&event)) {
            seq::v1::Envelope envelope;
            auto* ranks = envelope.mutable_guild_rank_names();
            ranks->set_guild_id(value->payload.guild_id);
            for (const auto& rank : value->payload.ranks) {
                ranks->add_rank_ids(rank.rank_index);
                ranks->add_names(std::string(rank.rank_name));
            }
            out.push_back(std::move(envelope));
        }
    }
    return out;
}

CommunicationComparison compareCommunication(
    const Batch& rustBatch,
    const std::vector<CommunicationObservation>& legacyEvents,
    const std::vector<seq::v1::Envelope>& legacyProjections,
    const ChatTextResolver& resolveText)
{
    CommunicationComparison comparison;
    const auto rustEvents = communicationObservations(rustBatch);
    const auto rustProjections = projectCommunication(rustBatch, resolveText);
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
                 PlayerSelector playerSelector,
                 ProgressionSelector progressionSelector,
                 LootSelector lootSelector,
                 CombatSelector combatSelector,
                 CommunicationSelector communicationSelector)
    : m_session(rust::session_new(registry.rustRegistry(), backend))
    , m_journalLimit(std::max<size_t>(journalLimit, 1))
    , m_journalByteLimit(std::max<size_t>(journalByteLimit, sizeof(Record)))
    , m_lifecycleSelector(lifecycleSelector)
    , m_entitySelector(entitySelector)
    , m_playerSelector(playerSelector)
    , m_progressionSelector(progressionSelector)
    , m_lootSelector(lootSelector)
    , m_combatSelector(combatSelector)
    , m_communicationSelector(communicationSelector)
{
}

const Record& Session::decodeUcs(Direction direction, const uint8_t* payload,
                                 size_t payloadSize)
{
    auto raw = m_session->decode_ucs(
        toRust(direction), ::rust::Slice<const uint8_t>(payload, payloadSize));
    Record record;
    record.ucsPayloadSize = payloadSize;
    record.batch = translate(std::move(raw));
    return append(std::move(record));
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
    const size_t payloadBytes = record.packet
        ? record.packet->payloadSize : record.ucsPayloadSize.value_or(0);
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
