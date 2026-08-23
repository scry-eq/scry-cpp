#include <QtTest/QtTest>

#include <algorithm>
#include <array>
#include <stdexcept>
#include <type_traits>

#include "rustsession.h"
#include "datetimemgr.h"
#include "itempacket.h"
#include "protoencoder.h"
#include "spawn.h"

namespace {

using namespace seq::shadow;
namespace ffi = seq::rust;

// The adapter moves each generated payload into its matching wrapper without a
// field projection. These checks make that structural field preservation part
// of the C++ test contract for every payload-bearing event.
#define SAME_PAYLOAD(wrapper, type) \
    static_assert(std::is_same_v<decltype(wrapper::payload), ffi::type>)
SAME_PAYLOAD(SpawnAdded, EventSpawnInfo);
SAME_PAYLOAD(SpawnMoved, EventSpawnMoved);
SAME_PAYLOAD(SpawnRenamed, EventSpawnRenamed);
SAME_PAYLOAD(SpawnRemoved, EventSpawnId);
SAME_PAYLOAD(SpawnKilled, EventSpawnKilled);
SAME_PAYLOAD(SpawnHp, EventSpawnHp);
SAME_PAYLOAD(StatSync, EventStatSync);
SAME_PAYLOAD(SelfPos, EventSelfPos);
SAME_PAYLOAD(SpawnAnimation, EventSpawnAnimation);
SAME_PAYLOAD(SpawnIllusion, EventSpawnIllusion);
SAME_PAYLOAD(GuildsInZone, EventGuildsInZone);
SAME_PAYLOAD(TimeOfDay, EventTimeOfDay);
SAME_PAYLOAD(ZoneChanged, EventZoneInfo);
SAME_PAYLOAD(SessionReset, EventSessionReset);
SAME_PAYLOAD(ZoneTransition, EventZoneTransition);
SAME_PAYLOAD(ZoneEnvironmentChanged, EventZoneEnvironment);
SAME_PAYLOAD(PlayerProfile, EventProfileInfo);
SAME_PAYLOAD(Stance, EventNamed);
SAME_PAYLOAD(Invocation, EventNamed);
SAME_PAYLOAD(InspectAnswer, EventInspectAnswer);
SAME_PAYLOAD(GuildRoster, EventGuildRoster);
SAME_PAYLOAD(ZoneServerInfo, EventZoneServerInfo);
SAME_PAYLOAD(ItemSet, EventItemSet);
SAME_PAYLOAD(ItemLearned, EventItemLearned);
SAME_PAYLOAD(InventorySnapshot, EventInventorySnapshot);
SAME_PAYLOAD(InventoryItemUpdated, EventInventoryItemUpdated);
SAME_PAYLOAD(EquipmentSnapshot, EventEquipmentSnapshot);
SAME_PAYLOAD(EquipmentSlotUpdated, EventEquipmentSlotUpdated);
SAME_PAYLOAD(GuildMotd, EventGuildMotdPayload);
SAME_PAYLOAD(GuildRankName, EventGuildRankName);
SAME_PAYLOAD(LoadoutSwap, EventLoadoutSwap);
SAME_PAYLOAD(Doors, EventDoors);
SAME_PAYLOAD(GroundItemRemoved, EventGroundItemRemoved);
SAME_PAYLOAD(GroundItem, EventGroundItem);
SAME_PAYLOAD(CorpseLocated, EventCorpseLocated);
SAME_PAYLOAD(ZonePoints, EventZonePoints);
SAME_PAYLOAD(Combat, EventCombat);
SAME_PAYLOAD(SpawnCast, EventSpawnCast);
SAME_PAYLOAD(Targeted, EventSpawnId);
SAME_PAYLOAD(Considered, EventSpawnId);
SAME_PAYLOAD(AaTable, EventAaTable);
SAME_PAYLOAD(AlternateAbilityDefined, EventAlternateAbilityDefinition);
SAME_PAYLOAD(Exp, EventExp);
SAME_PAYLOAD(ExperienceUpdated, EventExperienceProgress);
SAME_PAYLOAD(AaExp, EventAaExp);
SAME_PAYLOAD(AlternateAdvancementSnapshot, EventAlternateAdvancementSnapshot);
SAME_PAYLOAD(AlternateAdvancementUpdated, EventAlternateAdvancementProgress);
SAME_PAYLOAD(Stamina, EventStaminaPayload);
SAME_PAYLOAD(ManaUpdate, EventManaUpdate);
SAME_PAYLOAD(SkillUpdate, EventSkillUpdatePayload);
SAME_PAYLOAD(SkillsSnapshot, EventSkillsSnapshot);
SAME_PAYLOAD(SkillValueUpdated, EventSkillValue);
SAME_PAYLOAD(LootTransaction, EventLootTransactionPayload);
SAME_PAYLOAD(LootDrops, EventLootDropsPayload);
SAME_PAYLOAD(CorpseLootSnapshot, EventCorpseLootSnapshot);
SAME_PAYLOAD(LootAcquired, EventLootAcquisition);
SAME_PAYLOAD(Money, EventMoney);
SAME_PAYLOAD(MoneyBalanceUpdated, EventMoneyBalance);
SAME_PAYLOAD(SimpleMessage, EventSimpleMessagePayload);
SAME_PAYLOAD(FormattedMessage, EventFormattedMessagePayload);
SAME_PAYLOAD(SpecialMessage, EventSpecialMessagePayload);
SAME_PAYLOAD(LootMessage, EventLootMessagePayload);
SAME_PAYLOAD(Chat, EventChat);
SAME_PAYLOAD(BuffList, EventBuffList);
SAME_PAYLOAD(GroupFollow, EventGroupFollowPayload);
SAME_PAYLOAD(GroupDisband, EventGroupDisbandPayload);
SAME_PAYLOAD(LevelUpdate, EventLevelUpdatePayload);
SAME_PAYLOAD(EnterWorld, EventEnterWorld);
SAME_PAYLOAD(PlayerIdentityUpdated, EventPlayerIdentity);
SAME_PAYLOAD(PlayerMoved, EventPlayerMoved);
SAME_PAYLOAD(PlayerVitalsUpdated, EventPlayerVitals);
SAME_PAYLOAD(SpawnHealthUpdated, EventSpawnHealth);
SAME_PAYLOAD(PlayerDied, EventPlayerDied);
SAME_PAYLOAD(SpawnDied, EventSpawnDied);
SAME_PAYLOAD(SpawnIdentityUpdated, EventSpawnIdentity);
SAME_PAYLOAD(PlayerAppearanceUpdated, EventPlayerAppearance);
#undef SAME_PAYLOAD

template <typename T>
void addPayload(ffi::SessionDecodeBatch& batch, ::rust::Vec<T>& payloads,
                ffi::SessionEventKind kind, T payload = {})
{
    ffi::SessionEventRef ref;
    ref.kind = kind;
    ref.payload_index = static_cast<uint32_t>(payloads.size());
    payloads.push_back(std::move(payload));
    batch.events.push_back(ref);
}

QString text(const ::rust::String& value)
{
    return QString::fromUtf8(value.data(), qsizetype(value.size()));
}

uint16_t deleteSpawnOpcode()
{
#if defined(SEQ_TARGET_LIVE)
    return 0x5d39;
#elif defined(SEQ_TARGET_TEST)
    return 0xa183;
#elif defined(SEQ_TARGET_EQL)
    return 0x7ba3;
#endif
}

uint16_t enterWorldOpcode()
{
#if defined(SEQ_TARGET_LIVE)
    return 0x5a59;
#elif defined(SEQ_TARGET_TEST)
    return 0xf31f;
#elif defined(SEQ_TARGET_EQL)
    return 0x0935;
#endif
}

ffi::SessionBackend backend()
{
#if defined(SEQ_TARGET_LIVE)
    return ffi::SessionBackend::Live;
#elif defined(SEQ_TARGET_TEST)
    return ffi::SessionBackend::Test;
#elif defined(SEQ_TARGET_EQL)
    return ffi::SessionBackend::Eql;
#endif
}

} // namespace

class RustSessionTest : public QObject
{
    Q_OBJECT
private slots:
    void everyVariantTranslatesInOrder();
    void payloadFieldsSurviveTranslation();
    void diagnosticsAndJournalAreOrdered();
    void sessionsAreIsolated();
    void enterWorldLifecycleIsOrdered();
    void lifecycleOrderAndProjectionMatchLegacy();
    void malformedLifecycleDoesNotReset();
    void lifecycleSelectorIsImmutablePerSession();
    void entityAdapterPreservesOptionalFieldsAndProjectionOrder();
    void entityMotionEquipmentMatchLegacyProjection();
    void playerAdapterPreservesAllPhaseSixEventsInOrder();
    void progressionAdapterPreservesOptionalFieldsAndProjectsExactly();
    void lootAdapterPreservesContextPresenceAndProjectionOrder();
    void resetPrecedesProfileAndUsesProductionProjection();
    void publicTimeContractNormalizesWireHourOnce();
    void reconnectResetsBeforeEachEnterWorld();
    void invalidAdapterPayloadFailsClosed();
    void journalHonorsByteBudget();
};

void RustSessionTest::everyVariantTranslatesInOrder()
{
    ffi::SessionDecodeBatch raw;
    raw.protocol_generation = 77;
    raw.disposition = ffi::SessionDisposition::Decoded;

#define ADD(member, kind, type) \
    addPayload(raw, raw.member, ffi::SessionEventKind::kind, ffi::type{})
    ADD(spawn_added, SpawnAdded, EventSpawnInfo);
    ADD(spawn_moved, SpawnMoved, EventSpawnMoved);
    ADD(spawn_renamed, SpawnRenamed, EventSpawnRenamed);
    ADD(spawn_removed, SpawnRemoved, EventSpawnId);
    ADD(spawn_killed, SpawnKilled, EventSpawnKilled);
    ADD(spawn_hp, SpawnHp, EventSpawnHp);
    ADD(stat_sync, StatSync, EventStatSync);
    ADD(self_pos, SelfPos, EventSelfPos);
    ADD(spawn_animation, SpawnAnimation, EventSpawnAnimation);
    ADD(spawn_illusion, SpawnIllusion, EventSpawnIllusion);
    ADD(guilds_in_zone, GuildsInZone, EventGuildsInZone);
    ADD(time_of_day, TimeOfDay, EventTimeOfDay);
    ADD(zone_changed, ZoneChanged, EventZoneInfo);
    ADD(player_profile, PlayerProfile, EventProfileInfo);
    ADD(named, Stance, EventNamed);
    ADD(named, Invocation, EventNamed);
    ADD(inspect_answer, InspectAnswer, EventInspectAnswer);
    ADD(guild_roster, GuildRoster, EventGuildRoster);
    ADD(zone_server_info, ZoneServerInfo, EventZoneServerInfo);
    ADD(item_set, ItemSet, EventItemSet);
    ADD(item_learned, ItemLearned, EventItemLearned);
    ADD(guild_motd, GuildMotd, EventGuildMotdPayload);
    ADD(guild_rank_name, GuildRankName, EventGuildRankName);
    ADD(loadout_swap, LoadoutSwap, EventLoadoutSwap);
    ADD(doors, Doors, EventDoors);
    ADD(ground_item_removed, GroundItemRemoved, EventGroundItemRemoved);
    ADD(ground_item, GroundItem, EventGroundItem);
    ADD(corpse_located, CorpseLocated, EventCorpseLocated);
    ADD(zone_points, ZonePoints, EventZonePoints);
    ADD(combat, Combat, EventCombat);
    ADD(spawn_cast, SpawnCast, EventSpawnCast);
    ADD(spawn_id, Targeted, EventSpawnId);
    ADD(spawn_id, Considered, EventSpawnId);
    ADD(aa_table, AaTable, EventAaTable);
    ADD(exp, Exp, EventExp);
    ADD(aa_exp, AaExp, EventAaExp);
    ADD(stamina, Stamina, EventStaminaPayload);
    ADD(mana_update, ManaUpdate, EventManaUpdate);
    ADD(skill_update, SkillUpdate, EventSkillUpdatePayload);
    ADD(loot_transaction, LootTransaction, EventLootTransactionPayload);
    ADD(loot_drops, LootDrops, EventLootDropsPayload);
    ADD(money, Money, EventMoney);
    ADD(simple_message, SimpleMessage, EventSimpleMessagePayload);
    ADD(formatted_message, FormattedMessage, EventFormattedMessagePayload);
    ADD(special_message, SpecialMessage, EventSpecialMessagePayload);
    ADD(loot_message, LootMessage, EventLootMessagePayload);
    ADD(chat, Chat, EventChat);
    ADD(buff_list, BuffList, EventBuffList);
    ADD(group_follow, GroupFollow, EventGroupFollowPayload);
    ADD(group_disband, GroupDisband, EventGroupDisbandPayload);
    ADD(level_update, LevelUpdate, EventLevelUpdatePayload);
    ADD(enter_world, EnterWorld, EventEnterWorld);
    ADD(session_reset, SessionReset, EventSessionReset);
    ADD(zone_transition, ZoneTransition, EventZoneTransition);
    ADD(zone_environment_changed, ZoneEnvironmentChanged, EventZoneEnvironment);
    ADD(player_identity_updated, PlayerIdentityUpdated, EventPlayerIdentity);
    ADD(player_moved, PlayerMoved, EventPlayerMoved);
    ADD(player_vitals_updated, PlayerVitalsUpdated, EventPlayerVitals);
    ADD(spawn_health_updated, SpawnHealthUpdated, EventSpawnHealth);
    ADD(player_died, PlayerDied, EventPlayerDied);
    ADD(spawn_died, SpawnDied, EventSpawnDied);
    ADD(spawn_identity_updated, SpawnIdentityUpdated, EventSpawnIdentity);
    ADD(player_appearance_updated, PlayerAppearanceUpdated,
        EventPlayerAppearance);
    ADD(inventory_snapshot, InventorySnapshot, EventInventorySnapshot);
    ADD(inventory_item_updated, InventoryItemUpdated,
        EventInventoryItemUpdated);
    ADD(equipment_snapshot, EquipmentSnapshot, EventEquipmentSnapshot);
    ADD(equipment_slot_updated, EquipmentSlotUpdated,
        EventEquipmentSlotUpdated);
    ADD(money_balance_updated, MoneyBalanceUpdated, EventMoneyBalance);
    ADD(skills_snapshot, SkillsSnapshot, EventSkillsSnapshot);
    ADD(skill_value_updated, SkillValueUpdated, EventSkillValue);
    ADD(experience_updated, ExperienceUpdated, EventExperienceProgress);
    ADD(alternate_advancement_snapshot, AlternateAdvancementSnapshot,
        EventAlternateAdvancementSnapshot);
    ADD(alternate_advancement_updated, AlternateAdvancementUpdated,
        EventAlternateAdvancementProgress);
    ADD(alternate_ability_defined, AlternateAbilityDefined,
        EventAlternateAbilityDefinition);
    ADD(corpse_loot_snapshot, CorpseLootSnapshot,
        EventCorpseLootSnapshot);
    ADD(loot_acquired, LootAcquired, EventLootAcquisition);
#undef ADD

    Batch batch = translate(std::move(raw));
    QCOMPARE(batch.protocolGeneration, uint64_t(77));
    QCOMPARE(batch.disposition, Disposition::Decoded);
    QCOMPARE(batch.events.size(), size_t(76));

#define CHECK(index, type) QVERIFY(std::holds_alternative<type>(batch.events[index]))
    CHECK(0, SpawnAdded); CHECK(1, SpawnMoved); CHECK(2, SpawnRenamed);
    CHECK(3, SpawnRemoved); CHECK(4, SpawnKilled); CHECK(5, SpawnHp);
    CHECK(6, StatSync); CHECK(7, SelfPos); CHECK(8, SpawnAnimation);
    CHECK(9, SpawnIllusion); CHECK(10, GuildsInZone); CHECK(11, TimeOfDay);
    CHECK(12, ZoneChanged); CHECK(13, PlayerProfile); CHECK(14, Stance);
    CHECK(15, Invocation); CHECK(16, InspectAnswer); CHECK(17, GuildRoster);
    CHECK(18, ZoneServerInfo); CHECK(19, ItemSet); CHECK(20, ItemLearned);
    CHECK(21, GuildMotd); CHECK(22, GuildRankName); CHECK(23, LoadoutSwap);
    CHECK(24, Doors); CHECK(25, GroundItemRemoved); CHECK(26, GroundItem);
    CHECK(27, CorpseLocated); CHECK(28, ZonePoints); CHECK(29, Combat);
    CHECK(30, SpawnCast); CHECK(31, Targeted); CHECK(32, Considered);
    CHECK(33, AaTable); CHECK(34, Exp); CHECK(35, AaExp);
    CHECK(36, Stamina); CHECK(37, ManaUpdate); CHECK(38, SkillUpdate);
    CHECK(39, LootTransaction); CHECK(40, LootDrops); CHECK(41, Money);
    CHECK(42, SimpleMessage); CHECK(43, FormattedMessage);
    CHECK(44, SpecialMessage); CHECK(45, LootMessage); CHECK(46, Chat);
    CHECK(47, BuffList); CHECK(48, GroupFollow); CHECK(49, GroupDisband);
    CHECK(50, LevelUpdate); CHECK(51, EnterWorld);
    CHECK(52, SessionReset); CHECK(53, ZoneTransition);
    CHECK(54, ZoneEnvironmentChanged);
    CHECK(55, PlayerIdentityUpdated); CHECK(56, PlayerMoved);
    CHECK(57, PlayerVitalsUpdated); CHECK(58, SpawnHealthUpdated);
    CHECK(59, PlayerDied); CHECK(60, SpawnDied);
    CHECK(61, SpawnIdentityUpdated); CHECK(62, PlayerAppearanceUpdated);
    CHECK(63, InventorySnapshot); CHECK(64, InventoryItemUpdated);
    CHECK(65, EquipmentSnapshot); CHECK(66, EquipmentSlotUpdated);
    CHECK(67, MoneyBalanceUpdated); CHECK(68, SkillsSnapshot);
    CHECK(69, SkillValueUpdated); CHECK(70, ExperienceUpdated);
    CHECK(71, AlternateAdvancementSnapshot);
    CHECK(72, AlternateAdvancementUpdated);
    CHECK(73, AlternateAbilityDefined);
    CHECK(74, CorpseLootSnapshot); CHECK(75, LootAcquired);
#undef CHECK
}

void RustSessionTest::payloadFieldsSurviveTranslation()
{
    ffi::SessionDecodeBatch raw;
    raw.protocol_generation = 99;
    raw.disposition = ffi::SessionDisposition::Decoded;

    ffi::EventSpawnInfo spawn;
    spawn.id = 1;
    spawn.name = ::rust::String("name");
    spawn.last_name = ::rust::String("last");
    spawn.race = 2; spawn.class_ = 3; spawn.deity = 4; spawn.level = 5;
    spawn.npc = 6; spawn.cur_hp = 7; spawn.has_max_hp = true; spawn.max_hp = 8;
    spawn.guild_id = 9; spawn.guild_server_id = 10; spawn.class_mask = 11;
    spawn.has_pos = true;
    spawn.pos.x = 12; spawn.pos.y = 13; spawn.pos.z = 14;
    spawn.pos.heading_deg = 15;
    addPayload(raw, raw.spawn_added, ffi::SessionEventKind::SpawnAdded,
               std::move(spawn));

    ffi::EventProfileInfo profile;
    profile.name = ::rust::String("profile");
    profile.last_name = ::rust::String("surname");
    profile.class_ = 16; profile.level = 17; profile.race = 18;
    profile.deity = 19; profile.cur_hp = 20; profile.mana = 21;
    profile.aa_ids.push_back(22); profile.aa_values.push_back(23);
    profile.aa_spent = 24; profile.aa_assigned = 240;
    profile.aa_unspent = 241; profile.aa_experience = 242;
    profile.skills.push_back(25); profile.class_mask = 26;
    profile.str_ = 27; profile.sta = 28; profile.cha = 29; profile.dex = 30;
    profile.int_ = 31; profile.agi = 32; profile.wis = 33;
    profile.platinum = 34; profile.gold = 35; profile.silver = 36;
    profile.copper = 37;
    addPayload(raw, raw.player_profile, ffi::SessionEventKind::PlayerProfile,
               std::move(profile));

    ffi::EventItemTemplate item;
    item.serial = ::rust::String("serial");
    item.name = ::rust::String("item");
    item.lore_name = ::rust::String("lore");
    item.item_id = 38; item.has_icon = true; item.icon = 39;
    item.has_stack_count = true; item.stack_count = 390;
    item.has_weight_tenths = false;
    item.has_flags = true; item.flags = 391;
    item.has_corruption = true; item.corruption = -39;
    item.slot_mask = 40;
    item.container_id = 41; item.container_slot = 42; item.parent_slot = 43;
    item.stats.push_back(-44); item.resists.push_back(-45);
    item.hp = -46; item.mana = -47; item.endurance = -48; item.ac = -49;
    ffi::EventItemLearned learned;
    learned.item = std::move(item);
    addPayload(raw, raw.item_learned, ffi::SessionEventKind::ItemLearned,
               std::move(learned));

    ffi::SelfStat self;
    self.is_self = true; self.has_hp = true; self.hp_cur = 50; self.hp_max = 51;
    self.has_mana = true; self.mana_cur = 52; self.mana_max = 53;
    self.has_end = true; self.end_cur = 54; self.end_max = 55;
    raw.self_stats.push_back(std::move(self));

    ffi::LootRow loot;
    loot.ts = 56; loot.source = ::rust::String("source");
    loot.item_name = ::rust::String("loot"); loot.item_id = 57; loot.icon = 58;
    loot.qty = 59; loot.mob_name = ::rust::String("mob");
    loot.mob_norm = ::rust::String("norm"); loot.corpse_id = 60;
    loot.zone_short = ::rust::String("short");
    loot.zone_base = ::rust::String("base"); loot.instance = ::rust::String("inst");
    loot.sold = true; loot.money_copper = 61;
    loot.disposition = ::rust::String("acquired");
    loot.looter = ::rust::String("looter"); loot.sequence = 62;
    loot.complete = true;
    raw.loot_rows.push_back(std::move(loot));

    Batch batch = translate(std::move(raw));
    const auto& outSpawn = std::get<SpawnAdded>(batch.events[0]).payload;
    QCOMPARE(outSpawn.id, uint32_t(1)); QCOMPARE(text(outSpawn.name), QString("name"));
    QCOMPARE(text(outSpawn.last_name), QString("last")); QCOMPARE(outSpawn.race, uint32_t(2));
    QCOMPARE(outSpawn.class_, uint32_t(3)); QCOMPARE(outSpawn.deity, uint32_t(4));
    QCOMPARE(outSpawn.level, uint8_t(5)); QCOMPARE(outSpawn.npc, uint8_t(6));
    QCOMPARE(outSpawn.cur_hp, uint32_t(7)); QVERIFY(outSpawn.has_max_hp);
    QCOMPARE(outSpawn.max_hp, uint32_t(8)); QCOMPARE(outSpawn.guild_id, uint32_t(9));
    QCOMPARE(outSpawn.guild_server_id, uint32_t(10));
    QCOMPARE(outSpawn.class_mask, uint32_t(11)); QVERIFY(outSpawn.has_pos);
    QCOMPARE(outSpawn.pos.x, int32_t(12)); QCOMPARE(outSpawn.pos.y, int32_t(13));
    QCOMPARE(outSpawn.pos.z, int32_t(14)); QCOMPARE(outSpawn.pos.heading_deg, uint16_t(15));

    const auto& outProfile = std::get<PlayerProfile>(batch.events[1]).payload;
    QCOMPARE(text(outProfile.name), QString("profile"));
    QCOMPARE(text(outProfile.last_name), QString("surname"));
    QCOMPARE(outProfile.class_, uint32_t(16)); QCOMPARE(outProfile.level, uint8_t(17));
    QCOMPARE(outProfile.race, uint32_t(18)); QCOMPARE(outProfile.deity, uint32_t(19));
    QCOMPARE(outProfile.cur_hp, uint32_t(20)); QCOMPARE(outProfile.mana, uint32_t(21));
    QCOMPARE(outProfile.aa_ids[0], uint32_t(22));
    QCOMPARE(outProfile.aa_values[0], uint32_t(23));
    QCOMPARE(outProfile.aa_spent, uint32_t(24));
    QCOMPARE(outProfile.aa_assigned, uint32_t(240));
    QCOMPARE(outProfile.aa_unspent, uint32_t(241));
    QCOMPARE(outProfile.aa_experience, uint32_t(242));
    QCOMPARE(outProfile.skills[0], uint32_t(25));
    QCOMPARE(outProfile.class_mask, uint32_t(26)); QCOMPARE(outProfile.str_, uint32_t(27));
    QCOMPARE(outProfile.sta, uint32_t(28)); QCOMPARE(outProfile.cha, uint32_t(29));
    QCOMPARE(outProfile.dex, uint32_t(30)); QCOMPARE(outProfile.int_, uint32_t(31));
    QCOMPARE(outProfile.agi, uint32_t(32)); QCOMPARE(outProfile.wis, uint32_t(33));
    QCOMPARE(outProfile.platinum, uint32_t(34)); QCOMPARE(outProfile.gold, uint32_t(35));
    QCOMPARE(outProfile.silver, uint32_t(36)); QCOMPARE(outProfile.copper, uint32_t(37));

    const auto& outItem = std::get<ItemLearned>(batch.events[2]).payload.item;
    QCOMPARE(text(outItem.serial), QString("serial")); QCOMPARE(text(outItem.name), QString("item"));
    QCOMPARE(text(outItem.lore_name), QString("lore")); QCOMPARE(outItem.item_id, uint32_t(38));
    QVERIFY(outItem.has_icon); QCOMPARE(outItem.icon, uint32_t(39));
    QVERIFY(outItem.has_stack_count); QCOMPARE(outItem.stack_count, uint32_t(390));
    QVERIFY(!outItem.has_weight_tenths);
    QVERIFY(outItem.has_flags); QCOMPARE(outItem.flags, uint32_t(391));
    QVERIFY(outItem.has_corruption); QCOMPARE(outItem.corruption, int32_t(-39));
    QCOMPARE(outItem.slot_mask, uint32_t(40));
    QCOMPARE(outItem.container_id, uint32_t(41)); QCOMPARE(outItem.container_slot, uint16_t(42));
    QCOMPARE(outItem.parent_slot, uint16_t(43)); QCOMPARE(outItem.stats[0], int32_t(-44));
    QCOMPARE(outItem.resists[0], int32_t(-45)); QCOMPARE(outItem.hp, int32_t(-46));
    QCOMPARE(outItem.mana, int32_t(-47)); QCOMPARE(outItem.endurance, int32_t(-48));
    QCOMPARE(outItem.ac, int32_t(-49));

    QCOMPARE(batch.selfStats.size(), size_t(1));
    const auto& outSelf = batch.selfStats[0];
    QVERIFY(outSelf.is_self && outSelf.has_hp && outSelf.has_mana && outSelf.has_end);
    QCOMPARE(outSelf.hp_cur, int64_t(50)); QCOMPARE(outSelf.hp_max, int64_t(51));
    QCOMPARE(outSelf.mana_cur, int64_t(52)); QCOMPARE(outSelf.mana_max, int64_t(53));
    QCOMPARE(outSelf.end_cur, int64_t(54)); QCOMPARE(outSelf.end_max, int64_t(55));

    QCOMPARE(batch.lootRows.size(), size_t(1));
    const auto& outLoot = batch.lootRows[0];
    QCOMPARE(outLoot.ts, int64_t(56)); QCOMPARE(text(outLoot.source), QString("source"));
    QCOMPARE(text(outLoot.item_name), QString("loot")); QCOMPARE(outLoot.item_id, uint32_t(57));
    QCOMPARE(outLoot.icon, uint32_t(58)); QCOMPARE(outLoot.qty, uint32_t(59));
    QCOMPARE(text(outLoot.mob_name), QString("mob")); QCOMPARE(text(outLoot.mob_norm), QString("norm"));
    QCOMPARE(outLoot.corpse_id, uint32_t(60)); QCOMPARE(text(outLoot.zone_short), QString("short"));
    QCOMPARE(text(outLoot.zone_base), QString("base")); QCOMPARE(text(outLoot.instance), QString("inst"));
    QVERIFY(outLoot.sold); QCOMPARE(outLoot.money_copper, uint32_t(61));
    QCOMPARE(text(outLoot.disposition), QString("acquired"));
    QCOMPARE(text(outLoot.looter), QString("looter")); QCOMPARE(outLoot.sequence, uint32_t(62));
    QVERIFY(outLoot.complete);
}

void RustSessionTest::diagnosticsAndJournalAreOrdered()
{
    ProtocolRegistry registry;
    Session session(registry, backend(), 3);
    const uint8_t bad[3] = {1, 2, 3};
    const uint8_t good[4] = {1, 0, 0, 0};

    const Record& unmapped = session.decode(Stream::Zone, 0,
        Direction::ServerToClient, good, sizeof(good), 1000);
    QCOMPARE(unmapped.batch.disposition, Disposition::Unmapped);
    QCOMPARE(unmapped.packet->timestamp, int64_t(1000));

    const Record& malformed = session.decode(Stream::Zone, deleteSpawnOpcode(),
        Direction::ServerToClient, bad, sizeof(bad), 1001);
    QCOMPARE(malformed.batch.disposition, Disposition::Malformed);

    const Record& decoded = session.decode(Stream::Zone, deleteSpawnOpcode(),
        Direction::ServerToClient, good, sizeof(good), 1002);
    QCOMPARE(decoded.batch.disposition, Disposition::Decoded);
    QVERIFY(std::holds_alternative<SpawnRemoved>(decoded.batch.events[0]));

    const Record& flushed = session.flush(FlushReason::ReplayEnd);
    QCOMPARE(flushed.sequence, uint64_t(4));
    QCOMPARE(session.recordCount(), uint64_t(4));
    QCOMPARE(session.droppedRecordCount(), uint64_t(1));
    QCOMPARE(session.journal().front().sequence, uint64_t(2));
    QCOMPARE(session.journal().back().flushReason, std::optional(FlushReason::ReplayEnd));
}

void RustSessionTest::sessionsAreIsolated()
{
    ProtocolRegistry registry;
    Session first(registry, backend());
    Session second(registry, backend());
    const uint8_t payload[4] = {2, 0, 0, 0};

    first.decode(Stream::Zone, deleteSpawnOpcode(),
                 Direction::ServerToClient, payload, sizeof(payload), 1);
    QCOMPARE(first.recordCount(), uint64_t(1));
    QCOMPARE(second.recordCount(), uint64_t(0));
    QVERIFY(second.journal().empty());

    second.flush(FlushReason::Reset);
    QCOMPARE(first.recordCount(), uint64_t(1));
    QCOMPARE(second.recordCount(), uint64_t(1));
}

void RustSessionTest::enterWorldLifecycleIsOrdered()
{
    ProtocolRegistry registry;
    Session session(registry, backend());
    std::array<uint8_t, 72> payload{};
    const QByteArray name("Firona");
    std::copy(name.begin(), name.end(), payload.begin());

    const Record& record = session.decode(
        Stream::World, enterWorldOpcode(), Direction::ClientToServer,
        payload.data(), payload.size(), 1000);
    QCOMPARE(record.batch.disposition, Disposition::Decoded);
    QCOMPARE(record.batch.events.size(), size_t(2));
    QVERIFY(std::holds_alternative<SessionReset>(record.batch.events[0]));
    QVERIFY(std::holds_alternative<EnterWorld>(record.batch.events[1]));
    const auto& entered = std::get<EnterWorld>(record.batch.events[1]);
    QCOMPARE(text(entered.payload.character_name), QStringLiteral("Firona"));
    QCOMPARE(session.recordCount(), uint64_t(1));
}

void RustSessionTest::lifecycleOrderAndProjectionMatchLegacy()
{
    ffi::SessionDecodeBatch raw;
    raw.disposition = ffi::SessionDisposition::Decoded;

    ffi::EventSessionReset reset;
    reset.reason = ffi::EventSessionResetReason::ZoneTransition;
    addPayload(raw, raw.session_reset, ffi::SessionEventKind::SessionReset,
               std::move(reset));

    ffi::EventZoneTransition transition;
    transition.character_name = ::rust::String("Firona");
    transition.has_zone_id = true; transition.zone_id = 57;
    transition.has_instance_id = true; transition.instance_id = 3;
    transition.confirmed = true;
    addPayload(raw, raw.zone_transition, ffi::SessionEventKind::ZoneTransition,
               std::move(transition));

    ffi::EventZoneInfo zone;
    zone.short_name = ::rust::String("qeynos");
    zone.long_name = ::rust::String("South Qeynos");
    addPayload(raw, raw.zone_changed, ffi::SessionEventKind::ZoneChanged,
               std::move(zone));

    ffi::EventZoneEnvironment environment;
    environment.zone_file = ::rust::String("qeynos.eqg");
    environment.experience_multiplier = 1.25f;
    environment.safe_x = 10.25f; environment.safe_y = 20.5f;
    environment.safe_z = 30.75f;
    addPayload(raw, raw.zone_environment_changed,
               ffi::SessionEventKind::ZoneEnvironmentChanged,
               std::move(environment));

    ffi::EventTimeOfDay clock;
    clock.year = 3789; clock.month = 11; clock.day = 27;
    clock.hour = 13; clock.minute = 42;
    addPayload(raw, raw.time_of_day, ffi::SessionEventKind::TimeOfDay,
               std::move(clock));

    ffi::EventZoneServerInfo server;
    server.host = ::rust::String("zone.example.test"); server.port = 9000;
    addPayload(raw, raw.zone_server_info,
               ffi::SessionEventKind::ZoneServerInfo, std::move(server));

    Batch batch = translate(std::move(raw));
    const auto expectedOrder = lifecycleObservations(batch);
    QCOMPARE(expectedOrder.size(), size_t(6));
    QCOMPARE(expectedOrder[0].kind, LifecycleKind::SessionReset);
    QCOMPARE(expectedOrder[1].kind, LifecycleKind::ZoneTransition);
    QCOMPARE(expectedOrder[2].kind, LifecycleKind::ZoneChanged);
    QCOMPARE(expectedOrder[3].kind, LifecycleKind::ZoneEnvironmentChanged);
    QCOMPARE(expectedOrder[4].kind, LifecycleKind::TimeOfDay);
    QCOMPARE(expectedOrder[5].kind, LifecycleKind::ZoneServerInfo);

    std::vector<seq::v1::Envelope> legacy;
#if defined(SEQ_TARGET_EQL)
    legacy.push_back(seq::encode::zoneChanged(
        QStringLiteral("qeynos"), QStringLiteral("South Qeynos"), nullptr));
#endif
    QDateTime dateTime;
    dateTime.setDate(QDate(3789, 11, 27));
    dateTime.setTime(QTime(12, 42));
    legacy.push_back(seq::encode::eqTimeSync(dateTime));
    legacy.push_back(seq::encode::zoneServer(
        QStringLiteral("zone.example.test"), 9000));

    const LifecycleComparison equal =
        compareLifecycle(batch, expectedOrder, legacy);
    QVERIFY(equal.orderedEventsEqual);
    QVERIFY(equal.projectionsEqual);

    auto wrongOrder = expectedOrder;
    std::swap(wrongOrder[0], wrongOrder[1]);
    QVERIFY(!compareLifecycle(batch, wrongOrder, legacy).orderedEventsEqual);
#if defined(SEQ_TARGET_EQL)
    legacy[0].mutable_zone_changed()->set_zone_long("North Qeynos");
#else
    legacy[0].mutable_eq_time_sync()->set_minute(41);
#endif
    QVERIFY(!compareLifecycle(batch, expectedOrder, legacy).projectionsEqual);
}

void RustSessionTest::malformedLifecycleDoesNotReset()
{
    ProtocolRegistry registry;
    Session session(registry, backend());
    const uint8_t malformed[3] = {1, 2, 3};
    const Record& record = session.decode(
        Stream::World, enterWorldOpcode(), Direction::ClientToServer,
        malformed, sizeof(malformed), 1000);
    QCOMPARE(record.batch.disposition, Disposition::Malformed);
    QVERIFY(lifecycleObservations(record.batch).empty());
    QCOMPARE(session.recordCount(), uint64_t(1));
}

void RustSessionTest::lifecycleSelectorIsImmutablePerSession()
{
    ProtocolRegistry registry;
    Session legacy(registry, backend(), 256, 4 * 1024 * 1024,
                   LifecycleSelector::Legacy);
    Session shadow(registry, backend(), 256, 4 * 1024 * 1024,
                   LifecycleSelector::Shadow);
    Session rust(registry, backend(), 256, 4 * 1024 * 1024,
                 LifecycleSelector::Rust);
    QCOMPARE(legacy.lifecycleSelector(), LifecycleSelector::Legacy);
    QCOMPARE(shadow.lifecycleSelector(), LifecycleSelector::Shadow);
    QCOMPARE(rust.lifecycleSelector(), LifecycleSelector::Rust);
    QVERIFY(legacy.runsLegacyLifecycle());
    QVERIFY(!legacy.comparesLifecycle());
    QVERIFY(shadow.runsLegacyLifecycle());
    QVERIFY(shadow.comparesLifecycle());
    QVERIFY(!rust.runsLegacyLifecycle());
    QVERIFY(rust.appliesRustLifecycle());

    Session entityLegacy(registry, backend(), 256, 4 * 1024 * 1024,
                         LifecycleSelector::Shadow,
                         EntitySelector::Legacy);
    Session entityShadow(registry, backend(), 256, 4 * 1024 * 1024,
                         LifecycleSelector::Shadow,
                         EntitySelector::Shadow);
    Session entityRust(registry, backend(), 256, 4 * 1024 * 1024,
                       LifecycleSelector::Shadow,
                       EntitySelector::Rust);
    QCOMPARE(entityLegacy.entitySelector(), EntitySelector::Legacy);
    QCOMPARE(entityShadow.entitySelector(), EntitySelector::Shadow);
    QCOMPARE(entityRust.entitySelector(), EntitySelector::Rust);
    QVERIFY(entityLegacy.runsLegacyEntities());
    QVERIFY(entityShadow.comparesEntities());
    QVERIFY(!entityRust.runsLegacyEntities());
    QVERIFY(entityRust.appliesRustEntities());

    Session playerLegacy(registry, backend(), 256, 4 * 1024 * 1024,
                         LifecycleSelector::Shadow, EntitySelector::Legacy,
                         PlayerSelector::Legacy);
    Session playerShadow(registry, backend(), 256, 4 * 1024 * 1024,
                         LifecycleSelector::Shadow, EntitySelector::Legacy,
                         PlayerSelector::Shadow);
    Session playerRust(registry, backend(), 256, 4 * 1024 * 1024,
                       LifecycleSelector::Shadow, EntitySelector::Legacy,
                       PlayerSelector::Rust);
    QVERIFY(playerLegacy.runsLegacyPlayers());
    QVERIFY(playerShadow.comparesPlayers());
    QVERIFY(!playerRust.runsLegacyPlayers());
    QVERIFY(playerRust.appliesRustPlayers());

    Session progressionLegacy(
        registry, backend(), 256, 4 * 1024 * 1024,
        LifecycleSelector::Shadow, EntitySelector::Legacy,
        PlayerSelector::Legacy, ProgressionSelector::Legacy);
    Session progressionShadow(
        registry, backend(), 256, 4 * 1024 * 1024,
        LifecycleSelector::Shadow, EntitySelector::Legacy,
        PlayerSelector::Legacy, ProgressionSelector::Shadow);
    Session progressionRust(
        registry, backend(), 256, 4 * 1024 * 1024,
        LifecycleSelector::Shadow, EntitySelector::Legacy,
        PlayerSelector::Legacy, ProgressionSelector::Rust);
    QVERIFY(progressionLegacy.runsLegacyProgression());
    QVERIFY(progressionShadow.comparesProgression());
    QVERIFY(!progressionRust.runsLegacyProgression());
    QVERIFY(progressionRust.appliesRustProgression());

    Session lootLegacy(
        registry, backend(), 256, 4 * 1024 * 1024,
        LifecycleSelector::Shadow, EntitySelector::Legacy,
        PlayerSelector::Legacy, ProgressionSelector::Legacy,
        LootSelector::Legacy);
    Session lootShadow(
        registry, backend(), 256, 4 * 1024 * 1024,
        LifecycleSelector::Shadow, EntitySelector::Legacy,
        PlayerSelector::Legacy, ProgressionSelector::Legacy,
        LootSelector::Shadow);
    Session lootRust(
        registry, backend(), 256, 4 * 1024 * 1024,
        LifecycleSelector::Shadow, EntitySelector::Legacy,
        PlayerSelector::Legacy, ProgressionSelector::Legacy,
        LootSelector::Rust);
    QVERIFY(lootLegacy.runsLegacyLoot());
    QVERIFY(lootShadow.comparesLoot());
    QVERIFY(!lootRust.runsLegacyLoot());
    QVERIFY(lootRust.appliesRustLoot());
}

void RustSessionTest::entityAdapterPreservesOptionalFieldsAndProjectionOrder()
{
    ffi::SessionDecodeBatch raw;
    raw.disposition = ffi::SessionDisposition::Decoded;

    ffi::EventSpawnRenamed rename;
    rename.has_id = true;
    rename.id = 17;
    rename.old_name = ::rust::String("old");
    rename.new_name = ::rust::String("new");
    addPayload(raw, raw.spawn_renamed,
               ffi::SessionEventKind::SpawnRenamed, std::move(rename));

    ffi::EventDoorInfo door;
    door.id = 18;
    door.name = ::rust::String("POKTELE500");
    door.position.x = 1.25f;
    door.position.y = -2.5f;
    door.position.z = 3.75f;
    door.heading = 90.5f;
    door.incline = 4;
    door.size = 5;
    door.open_type = 6;
    door.state = 7;
    door.invert_state = 8;
    door.has_zone_point_id = false;
    ffi::EventDoors doors;
    doors.doors.push_back(std::move(door));
    addPayload(raw, raw.doors, ffi::SessionEventKind::Doors,
               std::move(doors));

    ffi::EventGroundItem ground;
    ground.id = 19;
    ground.actor_definition = ::rust::String("IT63_ACTORDEF");
    ground.position.x = 10.5f;
    ground.position.y = 11.5f;
    ground.position.z = 12.5f;
    ground.has_heading = false;
    addPayload(raw, raw.ground_item, ffi::SessionEventKind::GroundItem,
               std::move(ground));

    ffi::EventCorpseLocated corpse;
    corpse.id = 20;
    corpse.position.x = 20.25f;
    corpse.position.y = 21.25f;
    corpse.position.z = 22.25f;
    addPayload(raw, raw.corpse_located,
               ffi::SessionEventKind::CorpseLocated, std::move(corpse));

    ffi::EventZonePointInfo point;
    point.has_trigger_id = false;
    point.has_actor_definition = true;
    point.actor_definition = ::rust::String("OBJ_SWITCH");
    point.position.x = 30.5f;
    point.position.y = 31.5f;
    point.position.z = 32.5f;
    point.heading = 180.25f;
    point.has_destination_zone_id = true;
    point.destination_zone_id = 202;
    point.has_destination_instance_id = false;
    ffi::EventZonePoints points;
    points.points.push_back(std::move(point));
    addPayload(raw, raw.zone_points, ffi::SessionEventKind::ZonePoints,
               std::move(points));

    const Batch batch = translate(std::move(raw));
    QCOMPARE(batch.events.size(), size_t(5));
    const auto& outRename = std::get<SpawnRenamed>(batch.events[0]).payload;
    QVERIFY(outRename.has_id);
    QCOMPARE(outRename.id, uint32_t(17));
    QCOMPARE(text(outRename.old_name), QStringLiteral("old"));
    QCOMPARE(text(outRename.new_name), QStringLiteral("new"));
    const auto& outDoor = std::get<Doors>(batch.events[1]).payload.doors[0];
    QVERIFY(!outDoor.has_zone_point_id);
    QCOMPARE(outDoor.position.x, 1.25f);
    QCOMPARE(outDoor.heading, 90.5f);
    const auto& outGround =
        std::get<GroundItem>(batch.events[2]).payload;
    QVERIFY(!outGround.has_heading);
    QCOMPARE(outGround.position.z, 12.5f);
    const auto& outPoint =
        std::get<ZonePoints>(batch.events[4]).payload.points[0];
    QVERIFY(!outPoint.has_trigger_id);
    QVERIFY(outPoint.has_actor_definition);
    QVERIFY(outPoint.has_destination_zone_id);
    QVERIFY(!outPoint.has_destination_instance_id);
    QCOMPARE(outPoint.destination_zone_id, uint16_t(202));

    const auto observations = entityObservations(batch);
    QCOMPARE(observations.size(), size_t(5));
    QCOMPARE(observations[0].kind, EntityKind::SpawnRenamed);
    QCOMPARE(observations[1].kind, EntityKind::Doors);
    QCOMPARE(observations[2].kind, EntityKind::GroundItem);
    QCOMPARE(observations[3].kind, EntityKind::CorpseLocated);
    QCOMPARE(observations[4].kind, EntityKind::ZonePoints);

    const auto projections = projectEntities(batch);
    QCOMPARE(projections.size(), size_t(4));
    QVERIFY(projections[0].has_spawn_updated());
    QCOMPARE(projections[0].spawn_updated().id(), uint32_t(17));
    QCOMPARE(QString::fromStdString(projections[0].spawn_updated().name()),
             QStringLiteral("new"));
    QVERIFY(projections[1].has_spawn_added());
    QCOMPARE(projections[1].spawn_added().spawn().type(), seq::v1::DOOR);
    QVERIFY(projections[2].has_spawn_added());
    QCOMPARE(projections[2].spawn_added().spawn().type(), seq::v1::DROP);
    QVERIFY(projections[3].has_spawn_killed());
    QCOMPARE(projections[3].spawn_killed().deceased_id(), uint32_t(20));

    const EntityComparison equal =
        compareEntities(batch, observations, projections);
    QVERIFY(equal.orderedEventsEqual);
    QVERIFY(equal.projectionsEqual);
    auto wrong = projections;
    wrong[0].mutable_spawn_updated()->set_name("different");
    QVERIFY(!compareEntities(batch, observations, wrong).projectionsEqual);
}

void RustSessionTest::entityMotionEquipmentMatchLegacyProjection()
{
    ffi::SessionDecodeBatch raw;
    raw.disposition = ffi::SessionDisposition::Decoded;
    ffi::EventSpawnInfo added;
    added.id = 77;
    added.name = ::rust::String("a_goblin00");
    added.last_name = ::rust::String("Scout");
    added.race = 46; added.class_ = 1; added.deity = 140;
    added.level = 42; added.npc = 1; added.cur_hp = 80;
    added.has_max_hp = true; added.max_hp = 100;
    added.has_pos = true;
    added.pos.x = 100; added.pos.y = -200; added.pos.z = 30;
    added.pos.heading_deg = 90;
    added.velocity.has_x = true; added.velocity.x = 4;
    added.velocity.has_y = true; added.velocity.y = -5;
    added.velocity.has_z = true; added.velocity.z = 6;
    added.has_delta_heading = true; added.delta_heading = -3;
    added.has_animation = true; added.animation = 7;
    added.has_equipment_models = true;
    for (uint32_t i = 1; i <= 9; ++i) added.equipment_models.push_back(i);
    addPayload(raw, raw.spawn_added, ffi::SessionEventKind::SpawnAdded,
               std::move(added));

    ffi::EventSpawnMoved moved;
    moved.id = 77; moved.pos.x = 101; moved.pos.y = -201;
    moved.pos.z = 31; moved.pos.heading_deg = 180;
    moved.velocity.has_x = true; moved.velocity.x = -8;
    moved.velocity.has_y = false;
    moved.velocity.has_z = true; moved.velocity.z = 9;
    moved.has_delta_heading = true; moved.delta_heading = 2;
    moved.has_animation = true; moved.animation = 10;
    addPayload(raw, raw.spawn_moved, ffi::SessionEventKind::SpawnMoved,
               std::move(moved));

    const Batch batch = translate(std::move(raw));
    const auto projections = projectEntities(batch);
    QCOMPARE(projections.size(), size_t(2));
    const auto& spawn = projections[0].spawn_added().spawn();
    QCOMPARE(spawn.pos().vx(), -4); QCOMPARE(spawn.pos().vy(), 5);
    QCOMPARE(spawn.pos().vz(), 6);
    QCOMPARE(spawn.pos().delta_heading(), -3);
    QCOMPARE(spawn.pos().animation(), uint32_t(7));
    QCOMPARE(spawn.equip_models_size(), 9);
    for (int i = 0; i < 9; ++i)
        QCOMPARE(spawn.equip_models(i), uint32_t(i + 1));
    const auto& update = projections[1].spawn_updated();
    QCOMPARE(update.pos().vx(), 8); QCOMPARE(update.pos().vy(), 0);
    QCOMPARE(update.pos().vz(), 9);
    QCOMPARE(update.pos().delta_heading(), 2);
    QCOMPARE(update.pos().animation(), uint32_t(10));

    Spawn legacy(77, 100, -200, 30, 4, -5, 6, int8_t(192), -3, 7);
    legacy.setName("a_goblin00"); legacy.setLastName("Scout");
    legacy.setRace(46); legacy.setClassVal(1); legacy.setDeity(140);
    legacy.setLevel(42); legacy.setNPC(SPAWN_NPC);
    legacy.setHP(80); legacy.setMaxHP(100);
    legacy.setGuildID(0); legacy.setGuildServerID(0);
    for (uint8_t i = 0; i < 9; ++i) {
        EquipStruct equipment{}; equipment.itemId = i + 1;
        legacy.setEquipment(i, equipment);
    }
    seq::v1::Envelope legacyEnvelope;
    seq::encode::fillSpawn(
        legacyEnvelope.mutable_spawn_added()->mutable_spawn(), legacy);
    QCOMPARE(projections[0].SerializeAsString(),
             legacyEnvelope.SerializeAsString());
    legacy.setPos(101, -201, 31);
    legacy.setDeltas(-8, 0, 9);
    legacy.setHeading(int8_t(128), 2);
    legacy.setAnimation(10);
    seq::v1::Envelope legacyMove;
    auto* legacyUpdate = legacyMove.mutable_spawn_updated();
    legacyUpdate->set_id(77);
    seq::encode::fillPos(legacyUpdate->mutable_pos(), legacy);
    QCOMPARE(projections[1].SerializeAsString(),
             legacyMove.SerializeAsString());
}

void RustSessionTest::playerAdapterPreservesAllPhaseSixEventsInOrder()
{
    ffi::SessionDecodeBatch raw;
    raw.disposition = ffi::SessionDisposition::Decoded;
    ffi::EventPlayerIdentity identity;
    identity.has_spawn_id = true; identity.spawn_id = 10;
    identity.name = ::rust::String("Firona");
    identity.last_name = ::rust::String("Vie");
    identity.race = 1; identity.class_ = 2; identity.deity = 3;
    identity.level = 60; identity.class_mask = 4;
    addPayload(raw, raw.player_identity_updated,
               ffi::SessionEventKind::PlayerIdentityUpdated,
               std::move(identity));
    ffi::EventPlayerMoved moved;
    moved.has_spawn_id = true; moved.spawn_id = 10;
    moved.pos.x = 11; moved.pos.y = 12; moved.pos.z = 13;
    moved.pos.heading_deg = 14;
    addPayload(raw, raw.player_moved, ffi::SessionEventKind::PlayerMoved,
               std::move(moved));
    ffi::EventPlayerVitals vitals;
    vitals.has_health = true; vitals.health.current = 90;
    vitals.health.has_maximum = true; vitals.health.maximum = 100;
    vitals.has_mana = true; vitals.mana.current = 80;
    addPayload(raw, raw.player_vitals_updated,
               ffi::SessionEventKind::PlayerVitalsUpdated, std::move(vitals));
    ffi::EventSpawnHealth health;
    health.id = 20; health.current = 30; health.maximum = 40;
    addPayload(raw, raw.spawn_health_updated,
               ffi::SessionEventKind::SpawnHealthUpdated, std::move(health));
    ffi::EventPlayerDied playerDied;
    playerDied.has_killer_id = true; playerDied.killer_id = 21;
    addPayload(raw, raw.player_died, ffi::SessionEventKind::PlayerDied,
               std::move(playerDied));
    ffi::EventSpawnDied spawnDied;
    spawnDied.id = 20; spawnDied.has_killer_id = true;
    spawnDied.killer_id = 10;
    addPayload(raw, raw.spawn_died, ffi::SessionEventKind::SpawnDied,
               std::move(spawnDied));
    ffi::EventSpawnIdentity spawnIdentity;
    spawnIdentity.id = 20; spawnIdentity.level = 61;
    spawnIdentity.class_ = 5; spawnIdentity.race = 6;
    addPayload(raw, raw.spawn_identity_updated,
               ffi::SessionEventKind::SpawnIdentityUpdated,
               std::move(spawnIdentity));
    ffi::EventPlayerAppearance appearance;
    appearance.has_race = true; appearance.race = 7;
    appearance.has_gender = true; appearance.gender = 1;
    appearance.has_animation = true; appearance.animation = 110;
    addPayload(raw, raw.player_appearance_updated,
               ffi::SessionEventKind::PlayerAppearanceUpdated,
               std::move(appearance));

    const Batch batch = translate(std::move(raw));
    const auto observations = playerObservations(batch);
    QCOMPARE(observations.size(), size_t(8));
    QCOMPARE(observations[0].kind, PlayerKind::PlayerIdentityUpdated);
    QCOMPARE(observations[7].kind, PlayerKind::PlayerAppearanceUpdated);
    const auto projections = projectPlayers(batch);
    QCOMPARE(projections.size(), size_t(6));
    QVERIFY(projections[0].has_player_stats());
    QVERIFY(projections[1].has_spawn_updated());
    QVERIFY(projections[2].has_player_stats());
    QVERIFY(projections[3].has_spawn_updated());
    QVERIFY(projections[4].has_spawn_killed());
    QVERIFY(projections[5].has_spawn_updated());
    const auto comparison = comparePlayers(batch, observations, projections);
    QVERIFY(comparison.orderedEventsEqual);
    QVERIFY(comparison.projectionsEqual);
}

void RustSessionTest::progressionAdapterPreservesOptionalFieldsAndProjectsExactly()
{
    auto makeItem = [] {
        ffi::EventItemTemplate item;
        item.serial = ::rust::String("instance-1");
        item.name = ::rust::String("Lantern");
        item.lore_name = ::rust::String("A lantern");
        item.item_id = 9979;
        item.has_icon = false;
        item.has_stack_count = true; item.stack_count = 7;
        item.has_weight_tenths = true; item.weight_tenths = 5;
        item.has_flags = true; item.flags = 0x12345678;
        item.has_corruption = true; item.corruption = -6;
        item.slot_mask = 0x4800;
        item.container_id = 0; item.container_slot = 2;
        item.parent_slot = 0xFFFF;
        for (int32_t value : {1, 2, 3, 4, 5, 6, 7})
            item.stats.push_back(value);
        for (int32_t value : {8, 9, 10, 11, 12})
            item.resists.push_back(value);
        item.hp = 13; item.mana = 14; item.endurance = 15; item.ac = 16;
        return item;
    };

    ffi::SessionDecodeBatch raw;
    raw.disposition = ffi::SessionDisposition::Decoded;
    ffi::EventInventorySnapshot inventory;
    inventory.items.push_back(makeItem());
    addPayload(raw, raw.inventory_snapshot,
               ffi::SessionEventKind::InventorySnapshot,
               std::move(inventory));
    ffi::EventInventoryItemUpdated updated;
    updated.item = makeItem();
    updated.has_previous_location = true;
    updated.previous_location.container_id = 0;
    updated.previous_location.container_slot = 24;
    updated.previous_location.parent_slot = 0xFFFF;
    addPayload(raw, raw.inventory_item_updated,
               ffi::SessionEventKind::InventoryItemUpdated,
               std::move(updated));
    ffi::EventEquipmentSnapshot equipment;
    equipment.items.push_back(makeItem());
    addPayload(raw, raw.equipment_snapshot,
               ffi::SessionEventKind::EquipmentSnapshot,
               std::move(equipment));
    ffi::EventEquipmentSlotUpdated slot;
    slot.slot = 2; slot.has_item = true; slot.item = makeItem();
    addPayload(raw, raw.equipment_slot_updated,
               ffi::SessionEventKind::EquipmentSlotUpdated,
               std::move(slot));
    ffi::EventMoneyBalance money;
    money.platinum = 1; money.gold = 2; money.silver = 3; money.copper = 4;
    addPayload(raw, raw.money_balance_updated,
               ffi::SessionEventKind::MoneyBalanceUpdated, std::move(money));
    ffi::EventSkillsSnapshot skills;
    ffi::EventSkillValue skill;
    skill.skill_id = 30; skill.value = 12; skills.skills.push_back(skill);
    addPayload(raw, raw.skills_snapshot, ffi::SessionEventKind::SkillsSnapshot,
               std::move(skills));
    ffi::EventSkillValue skillUpdate;
    skillUpdate.skill_id = 31; skillUpdate.value = 13;
    addPayload(raw, raw.skill_value_updated,
               ffi::SessionEventKind::SkillValueUpdated,
               std::move(skillUpdate));
    ffi::EventExperienceProgress experience;
    experience.experience = 97900; experience.has_level = true;
    experience.level = 60; experience.has_previous_level = true;
    experience.previous_level = 59;
    addPayload(raw, raw.experience_updated,
               ffi::SessionEventKind::ExperienceUpdated,
               std::move(experience));
    ffi::EventAlternateAdvancementSnapshot aa;
    ffi::EventAlternateAbilityRank rank;
    rank.ability_id = 501; rank.rank = 3; aa.purchased.push_back(rank);
    aa.has_spent_points = true; aa.spent_points = 9;
    aa.has_assigned_points = false; aa.unspent_points = 7;
    aa.experience = 91234;
    addPayload(raw, raw.alternate_advancement_snapshot,
               ffi::SessionEventKind::AlternateAdvancementSnapshot,
               std::move(aa));
    ffi::EventAlternateAdvancementProgress aaUpdate;
    aaUpdate.experience = 1234; aaUpdate.unspent_points = 8;
    addPayload(raw, raw.alternate_advancement_updated,
               ffi::SessionEventKind::AlternateAdvancementUpdated,
               std::move(aaUpdate));
    ffi::EventAlternateAbilityDefinition definition;
    definition.ability_id = 501; definition.title_string_id = 601;
    addPayload(raw, raw.alternate_ability_defined,
               ffi::SessionEventKind::AlternateAbilityDefined,
               std::move(definition));

    const Batch batch = translate(std::move(raw));
    const auto observations = progressionObservations(batch);
    QCOMPARE(observations.size(), size_t(11));
    QCOMPARE(observations.front().kind, ProgressionKind::InventorySnapshot);
    QCOMPARE(observations.back().kind,
             ProgressionKind::AlternateAbilityDefined);
    const auto& item =
        std::get<InventoryItemUpdated>(batch.events[1]).payload.item;
    QVERIFY(!item.has_icon);
    QVERIFY(item.has_stack_count); QCOMPARE(item.stack_count, uint32_t(7));
    QVERIFY(item.has_weight_tenths); QCOMPARE(item.weight_tenths, uint32_t(5));
    QVERIFY(item.has_flags); QCOMPARE(item.flags, uint32_t(0x12345678));
    QVERIFY(item.has_corruption); QCOMPARE(item.corruption, int32_t(-6));
    const auto& moved =
        std::get<InventoryItemUpdated>(batch.events[1]).payload;
    QVERIFY(moved.has_previous_location);
    QCOMPARE(moved.previous_location.container_slot, uint16_t(24));

    const auto projections = projectProgression(batch);
    QCOMPARE(projections.size(), size_t(12));
    seq::v1::Envelope expectedItem;
    ItemTemplate legacy;
    legacy.itemId = 9979; legacy.itemName = QStringLiteral("Lantern");
    legacy.loreName = QStringLiteral("A lantern");
    legacy.slotBitmask = 0x4800; legacy.flags = 0x12345678;
    legacy.weight = 0.5f; legacy.hp = 13; legacy.mana = 14;
    legacy.endurance = 15; legacy.ac = 16; legacy.corruption = -6;
    for (int i = 0; i < ITEM_STAT_COUNT; ++i) legacy.stats[i] = int8_t(i + 1);
    for (int i = 0; i < ITEM_RES_COUNT; ++i) legacy.resists[i] = int8_t(i + 8);
    seq::encode::fillItem(
        expectedItem.mutable_item_learned()->mutable_item(), legacy);
    QCOMPARE(projections[0].SerializeAsString(),
             expectedItem.SerializeAsString());
    QCOMPARE(projections[1].SerializeAsString(),
             expectedItem.SerializeAsString());
    QCOMPARE(projections[6].player_stats().money_copper(), uint32_t(1234));
    QCOMPARE(projections[7].player_stats().skills(0).skill_id(), uint32_t(30));
    QCOMPARE(projections[9].player_stats().exp_cur(), uint32_t(97900));
    QCOMPARE(projections[10].player_stats().purchased_aa(0).ability_id(),
             uint32_t(501));
    QCOMPARE(projections[11].player_stats().aa_unspent(), uint32_t(8));

    const auto comparison =
        compareProgression(batch, observations, projections);
    QVERIFY(comparison.orderedEventsEqual);
    QVERIFY(comparison.projectionsEqual);
}

void RustSessionTest::lootAdapterPreservesContextPresenceAndProjectionOrder()
{
    ffi::SessionDecodeBatch raw;
    raw.disposition = ffi::SessionDisposition::Decoded;

    ffi::EventCorpseLootSnapshot snapshot;
    snapshot.timestamp = 1001;
    snapshot.corpse_id = 17;
    snapshot.corpse_name = ::rust::String("an ice giant");
    snapshot.corpse_name_normalized = ::rust::String("ice giant");
    snapshot.zone_short = ::rust::String("permafrost");
    snapshot.zone_base = ::rust::String("permafrost");
    snapshot.instance = ::rust::String("solo");
    snapshot.looter = ::rust::String("Alice");
    ffi::EventLootItemInfo item;
    item.name = ::rust::String("Fine Steel Sword");
    item.icon = 611;
    item.item_id = 1007;
    snapshot.items.push_back(std::move(item));
    addPayload(raw, raw.corpse_loot_snapshot,
               ffi::SessionEventKind::CorpseLootSnapshot,
               std::move(snapshot));

    ffi::EventLootAcquisition acquired;
    acquired.timestamp = 1002;
    acquired.item_name = ::rust::String("Fine Steel Sword");
    acquired.has_item_id = true; acquired.item_id = 1007;
    acquired.quantity = 2;
    acquired.corpse_name = ::rust::String("an ice giant");
    acquired.corpse_name_normalized = ::rust::String("ice giant");
    acquired.has_corpse_id = true; acquired.corpse_id = 17;
    acquired.zone_short = ::rust::String("permafrost");
    acquired.zone_base = ::rust::String("permafrost");
    acquired.instance = ::rust::String("solo");
    acquired.sold = true; acquired.coin_copper = 123;
    acquired.disposition = ::rust::String("sold");
    acquired.looter = ::rust::String("Alice");
    acquired.has_sequence = true; acquired.sequence = 42;
    acquired.from_corpse = false; acquired.complete = true;
    addPayload(raw, raw.loot_acquired,
               ffi::SessionEventKind::LootAcquired,
               std::move(acquired));

    ffi::EventLootAcquisition incomplete;
    incomplete.timestamp = 1003;
    incomplete.item_name = ::rust::String("Destroyed Thing");
    incomplete.has_item_id = false;
    incomplete.quantity = 1;
    incomplete.has_corpse_id = false;
    incomplete.disposition = ::rust::String("destroyed");
    incomplete.has_sequence = false;
    incomplete.complete = false;
    addPayload(raw, raw.loot_acquired,
               ffi::SessionEventKind::LootAcquired,
               std::move(incomplete));
    ffi::EventLootAcquisition orphanConfirmation;
    orphanConfirmation.timestamp = 1004;
    orphanConfirmation.has_item_id = true;
    orphanConfirmation.item_id = 2001;
    orphanConfirmation.quantity = 1;
    orphanConfirmation.has_sequence = true;
    orphanConfirmation.sequence = 43;
    orphanConfirmation.complete = false;
    addPayload(raw, raw.loot_acquired,
               ffi::SessionEventKind::LootAcquired,
               std::move(orphanConfirmation));

    const Batch batch = translate(std::move(raw));
    QCOMPARE(batch.events.size(), size_t(4));
    const auto& outSnapshot =
        std::get<CorpseLootSnapshot>(batch.events[0]).payload;
    QCOMPARE(outSnapshot.timestamp, int64_t(1001));
    QCOMPARE(text(outSnapshot.corpse_name_normalized),
             QString("ice giant"));
    QCOMPARE(text(outSnapshot.instance), QString("solo"));
    QCOMPARE(outSnapshot.items.size(), size_t(1));
    const auto& outAcquired = std::get<LootAcquired>(batch.events[1]).payload;
    QVERIFY(outAcquired.has_item_id && outAcquired.has_corpse_id);
    QVERIFY(outAcquired.has_sequence && outAcquired.complete);
    QCOMPARE(outAcquired.sequence, uint32_t(42));
    const auto& outIncomplete =
        std::get<LootAcquired>(batch.events[2]).payload;
    QVERIFY(!outIncomplete.has_item_id && !outIncomplete.has_corpse_id);
    QVERIFY(!outIncomplete.has_sequence && !outIncomplete.complete);

    const auto observations = lootObservations(batch);
    QCOMPARE(observations.size(), size_t(4));
    QCOMPARE(observations[0].kind, LootKind::CorpseLootSnapshot);
    QCOMPARE(observations[1].kind, LootKind::LootAcquired);
    QVERIFY(observations[1].payload != observations[2].payload);

    const auto projections = projectLoot(batch);
    QCOMPARE(projections.size(), size_t(3));
    QCOMPARE(projections[0].loot_drops().corpse_id(), uint32_t(17));
    QCOMPARE(projections[0].loot_drops().items(0).item_id(), uint32_t(1007));
    QCOMPARE(projections[1].loot_transaction().item_id(), uint32_t(1007));
    QCOMPARE(projections[1].loot_transaction().quantity(), uint32_t(2));
    QCOMPARE(projections[1].loot_transaction().coin_copper(), uint32_t(123));
    QCOMPARE(projections[2].loot_transaction().item_id(), uint32_t(2001));

    const auto comparison = compareLoot(batch, observations, projections);
    QVERIFY(comparison.orderedEventsEqual);
    QVERIFY(comparison.projectionsEqual);
}

void RustSessionTest::resetPrecedesProfileAndUsesProductionProjection()
{
    ffi::SessionDecodeBatch raw;
    raw.disposition = ffi::SessionDisposition::Decoded;
    ffi::EventSessionReset reset;
    reset.reason = ffi::EventSessionResetReason::PlayerProfile;
    addPayload(raw, raw.session_reset, ffi::SessionEventKind::SessionReset,
               std::move(reset));
    ffi::EventProfileInfo profile;
    profile.name = ::rust::String("Firona");
    profile.last_name = ::rust::String("Vie");
    profile.class_ = 2;
    profile.level = 60;
    profile.race = 1;
    profile.deity = 201;
    profile.cur_hp = 1000;
    profile.mana = 800;
    addPayload(raw, raw.player_profile, ffi::SessionEventKind::PlayerProfile,
               std::move(profile));

    const Batch batch = translate(std::move(raw));
    const auto rustEvents = lifecycleObservations(batch);
    LifecycleProfile actual;
    actual.name = "Firona";
    actual.lastName = "Vie";
    actual.classId = 2;
    actual.level = 60;
    actual.race = 1;
    actual.deity = 201;
    actual.currentHp = 1000;
    actual.mana = 800;
    const std::vector<LifecycleObservation> legacyEvents = {
        observeSessionReset(ffi::EventSessionResetReason::PlayerProfile),
        observeProfile(actual),
    };
    QCOMPARE(rustEvents, legacyEvents);
    QCOMPARE(rustEvents[0].kind, LifecycleKind::SessionReset);
    QCOMPARE(rustEvents[1].kind, LifecycleKind::PlayerProfile);
}

void RustSessionTest::publicTimeContractNormalizesWireHourOnce()
{
    DateTimeMgr manager;
    QSignalSpy decoded(&manager, &DateTimeMgr::decodedTimeOfDay);
    QSignalSpy projected(&manager, &DateTimeMgr::syncDateTime);
    manager.applyTimeOfDay(3789, 11, 27, 24, 42);
    QCOMPARE(decoded.count(), 1);
    QCOMPARE(projected.count(), 1);
    QCOMPARE(manager.eqDateTime().time().hour(), 23);
    const seq::v1::Envelope envelope =
        seq::encode::eqTimeSync(manager.eqDateTime());
    QVERIFY(envelope.has_eq_time_sync());
    QCOMPARE(envelope.eq_time_sync().hour(), uint32_t(23));
}

void RustSessionTest::reconnectResetsBeforeEachEnterWorld()
{
    ProtocolRegistry registry;
    Session session(registry, backend());
    std::array<uint8_t, 72> first{};
    std::copy_n("Firona", 6, first.begin());
    const Record& initial = session.decode(
        Stream::World, enterWorldOpcode(), Direction::ClientToServer,
        first.data(), first.size(), 1000);
    auto initialEvents = lifecycleObservations(initial.batch);
    QCOMPARE(initialEvents.size(), size_t(2));
    QCOMPARE(initialEvents[0].kind, LifecycleKind::SessionReset);
    QCOMPARE(initialEvents[1].kind, LifecycleKind::EnterWorld);

    std::array<uint8_t, 72> second{};
    std::copy_n("Firona", 6, second.begin());
    const Record& reconnect = session.decode(
        Stream::World, enterWorldOpcode(), Direction::ClientToServer,
        second.data(), second.size(), 2000);
    const auto reconnectEvents = lifecycleObservations(reconnect.batch);
    QCOMPARE(reconnectEvents.size(), size_t(2));
    QCOMPARE(reconnectEvents[0].kind, LifecycleKind::SessionReset);
    QCOMPARE(reconnectEvents[1].kind, LifecycleKind::EnterWorld);
    QCOMPARE(session.recordCount(), uint64_t(2));
}

void RustSessionTest::invalidAdapterPayloadFailsClosed()
{
    ffi::SessionDecodeBatch raw;
    raw.disposition = ffi::SessionDisposition::Decoded;
    ffi::SessionEventRef event;
    event.kind = ffi::SessionEventKind::ZoneChanged;
    event.payload_index = 7;
    raw.events.push_back(event);
    QVERIFY_EXCEPTION_THROWN(translate(std::move(raw)), std::out_of_range);
}

void RustSessionTest::journalHonorsByteBudget()
{
    ProtocolRegistry registry;
    const size_t byteLimit = sizeof(Record) + 8;
    Session session(registry, backend(), 10, byteLimit);
    const uint8_t payload[8] = {};

    session.decode(Stream::Zone, 0, Direction::ServerToClient,
                   payload, sizeof(payload), 1);
    session.decode(Stream::Zone, 0, Direction::ServerToClient,
                   payload, sizeof(payload), 2);
    QCOMPARE(session.journal().size(), size_t(1));
    QCOMPARE(session.droppedRecordCount(), uint64_t(1));
    QVERIFY(session.journalBytes() <= byteLimit);

    Session summary(registry, backend(), 10, sizeof(Record));
    const uint8_t removed[4] = {1, 0, 0, 0};
    const Record& record = summary.decode(
        Stream::Zone, deleteSpawnOpcode(), Direction::ServerToClient,
        removed, sizeof(removed), 3);
    QVERIFY(record.detailsOmitted);
    QVERIFY(record.batch.events.empty());
    QCOMPARE(record.batch.disposition, Disposition::Decoded);
    QVERIFY(summary.journalBytes() <= sizeof(Record));
}

QTEST_APPLESS_MAIN(RustSessionTest)
#include "rustsession_test.moc"
