#include "protoencoder.h"

#include <QPoint>
#include <QRegularExpression>

#include "category.h"
#include "everquest.h"
#include "filter.h"
#include "filtermgr.h"
#include "group.h"
#include "guildshell.h"
#include "itemcache.h"
#include "itempacket.h"
#include "mapcore.h"
#include "player.h"
#include "spawn.h"
#include "spawnmonitor.h"
#include "spellshell.h"

QString print_item(uint16_t item);

namespace seq::encode {

QString compatibilityDoorName(const QString& semanticName, uint32_t id)
{
    return QStringLiteral("Door: %1 (%2) ").arg(semanticName).arg(id);
}

QString compatibilityGroundItemName(const QString& actorDefinition)
{
    QString itemCode = actorDefinition;
    if (itemCode.startsWith(QStringLiteral("IT")))
        itemCode = itemCode.mid(2).section(
            '_', 0, 0, QString::SectionCaseInsensitiveSeps);
    const int itemId = itemCode.toInt();
    return QStringLiteral("Drop: ") +
        (itemId > 0 ? print_item(uint16_t(itemId)) : actorDefinition);
}

QString compatibilitySpawnName(const QString& semanticName)
{
    QString value = semanticName;
    value.remove(QRegularExpression(QStringLiteral("[0-9]")));
    value.replace('_', ' ');
    QString article;
    if (value.startsWith(QStringLiteral("a "))) {
        value = value.mid(2);
        article = QStringLiteral("a");
    } else if (value.startsWith(QStringLiteral("an "))) {
        value = value.mid(3);
        article = QStringLiteral("an");
    } else if (value.startsWith(QStringLiteral("the "))) {
        value = value.mid(4);
        article = QStringLiteral("the");
    }
    if (!article.isEmpty())
        value += QStringLiteral(", ") + article;
    return value;
}

seq::v1::Envelope zoneChanged(const QString& shortName,
                              const QString& longName,
                              const MapData* map)
{
    seq::v1::Envelope envelope;
    auto* zone = envelope.mutable_zone_changed();
    zone->set_zone_short(shortName.toStdString());
    zone->set_zone_long(longName.toStdString());
    if (map && map->numLayers() > 0)
        fillMapGeometry(zone->mutable_geometry(), *map);
    return envelope;
}

seq::v1::Envelope eqTimeSync(const QDateTime& dateTime)
{
    seq::v1::Envelope envelope;
    if (!dateTime.isValid()) return envelope;
    auto* time = envelope.mutable_eq_time_sync();
    time->set_year(dateTime.date().year());
    time->set_month(dateTime.date().month());
    time->set_day(dateTime.date().day());
    time->set_hour(dateTime.time().hour());
    time->set_minute(dateTime.time().minute());
    return envelope;
}

seq::v1::Envelope zoneServer(const QString& host, quint16 port)
{
    seq::v1::Envelope envelope;
    auto* server = envelope.mutable_zone_server();
    server->set_host(host.toStdString());
    server->set_port(port);
    return envelope;
}

static seq::v1::SpawnType typeFromItem(const Item& it)
{
    switch (it.type()) {
    case tPlayer: return seq::v1::PC;
    case tSpawn:  break;
    case tDrop:   return seq::v1::DROP;
    case tDoors:  return seq::v1::DOOR;
    default:      return seq::v1::SPAWN_UNSPECIFIED;
    }
    if (const auto* sp = dynamic_cast<const Spawn*>(&it)) {
        if (sp->isCorpse()) {
            // m_NPC distinguishes the two corpse flavors (SPAWN_PC_CORPSE
            // vs SPAWN_NPC_CORPSE) — both are non-zero, so a truthiness
            // check would always resolve to CORPSE_NPC. Match on the
            // specific NPC-corpse value instead.
            return sp->NPC() == SPAWN_NPC_CORPSE
                ? seq::v1::CORPSE_NPC
                : seq::v1::CORPSE_PC;
        }
        // Other PCs in the zone live in SpawnShell as Spawns (Item::type
        // == tSpawn) with m_NPC == SPAWN_PLAYER. The local player is the
        // only one that comes through as tPlayer; without this branch all
        // other-player spawns get reported as NPC.
        if (sp->isPlayer()) {
            return seq::v1::PC;
        }
    }
    return seq::v1::NPC;
}

// X and Y are negated on the wire so clients can render coordinates
// directly in screen convention (+X right = East, +Y down = South).
// EQ's runtime convention has +X = West and +Y = North, the inverse.
// Z (height) is the same in both conventions and ships raw. Velocity
// (vx, vy) lives in the same coord system as position, so it gets the
// same negation. See seq/v1/events.proto Pos for the wire contract.
void fillPos(seq::v1::Pos* out, const Spawn& in)
{
    out->set_x(-in.x());
    out->set_y(-in.y());
    out->set_z(in.z());
    out->set_vx(-in.deltaX());
    out->set_vy(-in.deltaY());
    out->set_vz(in.deltaZ());

    // Convert the legacy heading representation to degrees 0..359.
    // - Player has a precomputed `headingDegrees()` from the full 12-bit
    //   wire value; the truncated int8_t in m_heading would lose the
    //   high four bits and alias compass directions onto each other.
    // - Other spawns store an 8-bit heading; convert the same way the
    //   legacy code does for headingDegrees but with a 256-step scale.
    int headingDegrees;
    if (const auto* p = dynamic_cast<const Player*>(&in)) {
        headingDegrees = p->headingDegrees();
    } else {
        const uint8_t raw8 = static_cast<uint8_t>(in.heading());
        headingDegrees = 360 - ((raw8 * 360) >> 8);
    }
    out->set_heading(headingDegrees);
    out->set_delta_heading(in.deltaHeading());
    out->set_animation(in.animation());
}

void fillSpawn(seq::v1::Spawn* out, const Item& it,
               const CategoryMgr* categories,
               const FilterMgr* filterMgr)
{
    out->set_id(it.id());
    out->set_type(typeFromItem(it));

    if (const auto* sp = dynamic_cast<const Spawn*>(&it)) {
        // EQ ships spawn names with underscores in place of spaces and a
        // trailing instance number (e.g. "Cizzar_J`Axx00"). transformedName
        // does the underscore + digit cleanup AND moves leading articles
        // to the end ("a goblin" -> "goblin, a") so the spawn list sorts
        // by noun. Mirrors showeq spawnlistcommon.cpp:188.
        out->set_name(sp->transformedName().toStdString());
        // lastName carries NPC titles and merchant roles like
        // "(Fletching Supplies)". showeq renders it as
        // "<name> (<lastName>)" — we ship the parts separately so
        // clients can format independently.
        out->set_last_name(sp->lastName().toStdString());
        out->set_level(sp->level());
        out->set_race(sp->race());
        out->set_class_(sp->classVal());
        out->set_class_mask(sp->classMask());   // EQL multiclass; 0 on live
        out->set_deity(sp->deity());
        out->set_hp_cur(sp->HP());
        out->set_hp_max(sp->maxHP());
        out->set_guild_id(sp->guildID());
        out->set_guild_server_id(sp->guildServerID());
        out->set_guild_tag(sp->guildTag().toStdString());
        out->set_is_gm(sp->gm() != 0);
        out->set_locked(sp->locked());
        out->set_filter_flags(sp->filterFlags());
        fillPos(out->mutable_pos(), *sp);

        for (int i = 0; i <= tLastCoreWearSlot; i++)
            out->add_equip_models(sp->equipment(i).itemId);
    } else {
        // Drops, doors: no Spawn state, just position on the Item base.
        // Their names don't carry the underscore/instance-suffix
        // convention so the raw Item::name is fine to send.
        // X/Y negation per the screen-convention contract documented in
        // fillPos above.
        if (const auto* door = dynamic_cast<const Door*>(&it))
            out->set_name(door->compatibilityName().toStdString());
        else if (const auto* drop = dynamic_cast<const Drop*>(&it))
            out->set_name(drop->compatibilityName().toStdString());
        else
            out->set_name(it.name().toStdString());
        auto* pos = out->mutable_pos();
        if (const auto* door = dynamic_cast<const Door*>(&it)) {
            pos->set_x(-int32_t(door->semanticX()));
            pos->set_y(-int32_t(door->semanticY()));
            pos->set_z(int32_t(door->semanticZ() * 10.0f));
        } else {
            pos->set_x(-it.x());
            pos->set_y(-it.y());
            pos->set_z(it.z());
        }
    }

    if (categories) {
        // Several seqdef.toml categories key off filter-type names that
        // never appear in Spawn::filterString itself — Hunting matches
        // ":Hunt:", Alert matches ":Alert:", Filtered matches
        // ":Filtered:". showeq's spawnlist2 prepends the FilterMgr's
        // text form of the spawn's filter mask before evaluating
        // categories; mirror that here so those categories actually
        // match. Falls back gracefully if filterMgr is null (categories
        // that only key off Name/Race/Class/etc. still work).
        QString fs;
        const Spawn* sp = dynamic_cast<const Spawn*>(&it);
        if (filterMgr) {
            fs = ":";
            fs += filterMgr->filterString(it.filterFlags());
            if (sp) {
                fs += filterMgr->runtimeFilterString(sp->runtimeFilterFlags());
            }
            fs += it.filterString();
        } else {
            fs = it.filterString();
        }
        const int8_t level = sp ? sp->level() : 0;
        const CategoryList& all = categories->getCategories();
        for (int i = 0; i < all.size(); ++i) {
            const Category* c = all.at(i);
            if (c && c->isFiltered(fs, level)) {
                out->add_category_ids(static_cast<uint32_t>(i));
            }
        }
    }
}

void fillMapGeometry(seq::v1::MapGeometry* out, const MapData& map)
{
    // Bounds swap min<->max under negation: if x ranges over [lo, hi]
    // then -x ranges over [-hi, -lo]. See screen-convention contract
    // documented in fillPos above.
    out->set_min_x(-map.maxX());
    out->set_min_y(-map.maxY());
    out->set_max_x(-map.minX());
    out->set_max_y(-map.minY());

    // MapData is const-correct, but mapLayer() is non-const because layers
    // are lazily created during editing. We only read here, so cast away.
    auto& mdata = const_cast<MapData&>(map);
    const uint8_t layerCount = mdata.numLayers();
    for (uint8_t i = 0; i < layerCount; ++i) {
        MapLayer* layer = mdata.mapLayer(i);
        if (!layer) continue;

        // 2D lines — L-lines carry an optional single shared z.
        for (const MapLineL* line : layer->lLines()) {
            if (!line || line->isEmpty()) continue;
            auto* out_line = out->add_lines();
            out_line->set_color(line->colorName().toStdString());
            out_line->set_layer(i);
            const int n = line->size();
            for (int p = 0; p < n; ++p) {
                const QPoint pt = line->at(p);
                out_line->add_x(-pt.x());
                out_line->add_y(-pt.y());
            }
            if (line->heightSet()) {
                out_line->add_z(line->z());
            }
        }

        // 3D lines — M-lines carry per-point z.
        for (const MapLineM* line : layer->mLines()) {
            if (!line || line->isEmpty()) continue;
            auto* out_line = out->add_lines();
            out_line->set_color(line->colorName().toStdString());
            out_line->set_layer(i);
            const int n = line->size();
            for (int p = 0; p < n; ++p) {
                const MapPoint& pt = line->point(p);
                out_line->add_x(-pt.x());
                out_line->add_y(-pt.y());
                out_line->add_z(pt.z());
            }
        }

        for (const MapLocation* loc : layer->locations()) {
            if (!loc) continue;
            auto* out_loc = out->add_locations();
            out_loc->set_name(loc->name().toStdString());
            out_loc->set_color(loc->colorName().toStdString());
            out_loc->set_x(-loc->x());
            out_loc->set_y(-loc->y());
            out_loc->set_z(loc->z());
            out_loc->set_z_valid(loc->heightSet());
            out_loc->set_layer(i);
        }
    }

    if (map.heightHintAbove() > 0) {
        out->set_height_hint_above(map.heightHintAbove());
        out->set_height_hint_below(map.heightHintBelow());
    }
}

void fillMapPackages(seq::v1::MapPackagesUpdate* out,
                     const std::vector<MapPackageInfo>& packages,
                     const QString& activeId)
{
    for (const auto& p : packages) {
        auto* pkg = out->add_packages();
        pkg->set_id(p.id.toStdString());
        pkg->set_label(p.label.toStdString());
        pkg->set_zone_count(p.zoneCount);
    }
    out->set_active_id(activeId.toStdString());
}

void fillPlayerStats(seq::v1::PlayerStats* out, const Player& p)
{
    // Cast away const because the legacy accessors (HP, maxHP, getMana,
    // ...) aren't const-qualified. They don't actually mutate.
    auto& mp = const_cast<Player&>(p);

    // EQ never sends max mana on the wire — the legacy `calcMaxMana`
    // formula is `(WIS/5 + 2) * level + m_plusMana`, where m_plusMana
    // accumulates from item add/remove packets. The daemon doesn't wire
    // OP_CharInventory yet, so m_plusMana stays 0 and the calc
    // undershoots by however much MANA the player has on gear / AAs.
    // Floor the reported max to whatever the live OP_ManaChange tells
    // us — current > max is impossible in EQ, so observed current is
    // a strictly better lower bound than our calculated max.
    //
    // HP is NOT the same case: there is no calcMaxHP anywhere: maxHP() is the
    // stored m_maxHP, written only from the wire (eql's stat-sync wide form via
    // setVitals, live's OP_HPUpdate) and deliberately saved/restored across
    // Player::update so a percentage spawn record can't clobber it. So this max
    // is already exact and the guard below is only a floor against a stale max.
    const uint16_t curHP = mp.HP();
    const uint16_t wireHPMax = mp.maxHP();
    out->set_hp_cur(curHP);
    out->set_hp_max(curHP > wireHPMax ? curHP : wireHPMax);

    const uint16_t curMana = mp.getMana();
    out->set_mana_cur(curMana);
    // eql sends the real gear+buff-inclusive max via the stat-sync wide form
    // (captured into m_wireMaxMana, which the calcMaxMana estimate never
    // clobbers) — prefer it, exact, and it fixes the long-standing off-by-one.
    // Fall back to the observed current-mana peak, then the estimate, only when
    // no wire max ever arrived (Live never sends max mana).
    uint32_t maxMana = mp.wireMaxMana();
    if (maxMana == 0) {
      maxMana = mp.observedMaxMana();
      if (curMana > maxMana) maxMana = curMana;
      if (maxMana == 0) maxMana = mp.getMaxMana();
    }
    out->set_mana_max(maxMana);
    out->set_endurance_cur(mp.getEnduranceCur());
    out->set_endurance_max(mp.getEnduranceMax());

    out->set_level(mp.level());
    out->set_exp_cur(mp.getCurrentExp());
    out->set_exp_max(mp.getMaxExp());
    // Live OP_PlayerProfile reports expAA on a 0..100000 per-AA-point
    // scale (matches OP_ExpUpdate's per-level scale). Display % is
    // value / 1000 — confirmed 2026-05-02 against aa_progress.vpk.
    out->set_aa_exp_cur(mp.getCurrentAltExp());
    out->set_aa_exp_max(100'000u);
    out->set_aa_points(mp.getCurrentAApts());
    out->set_aa_unspent(mp.getCurrentAAUnspent());
    out->set_money_copper(mp.money());
    out->set_food(mp.food());
    out->set_water(mp.water());

    out->set_name(mp.name().toStdString());
    out->set_class_(mp.classVal());
    out->set_class_mask(mp.classMask());
    // EQL active stance/invocation display names (empty on live/single-class).
    out->set_stance(mp.stance().toStdString());
    out->set_invocation(mp.invocation().toStdString());
    out->set_race(mp.race());

    out->set_str(mp.getMaxSTR());
    out->set_sta(mp.getMaxSTA());
    out->set_agi(mp.getMaxAGI());
    out->set_dex(mp.getMaxDEX());
    out->set_wis(mp.getMaxWIS());
    out->set_int_(mp.getMaxINT());
    out->set_cha(mp.getMaxCHA());

    // Skills — sparse: skip only the unlearned-slot sentinels. Live's
    // player profile uses 0xFFFFFFFF for "skill not available to this
    // class/race"; legacy showeq used 255. Value 0 is a *valid* trained
    // value (e.g. Bind Wound 0/35, Baking 0/300 right after training
    // the skill but before practicing), so don't filter it.
    for (uint8_t id = 0; id < MAX_KNOWN_SKILLS; ++id) {
        const uint32_t v = mp.getSkill(id);
        // Live carries only CUR (no CAP) in the player profile — caps
        // are class+level lookups the EQ client computes locally. Without
        // a wire-side "available to my class" bit, filter v==0 too: a
        // trained-but-unpracticed skill (e.g. Bind Wound 0/35) hides
        // until first use, but that's the price of not flooding the
        // panel with every skill the class doesn't have.
        if (v == 0 || v == 0xFFFFFFFFu) continue;
        auto* s = out->add_skills();
        s->set_skill_id(id);
        s->set_value(v);
    }

    // Purchased AAs (rank > 0). Auto-grants (rank=0) are pre-filtered
    // in Player::loadProfile so the client only sees abilities the
    // player actually owns ranks of.
    for (const auto& aa : p.getPurchasedAA()) {
        auto* e = out->add_purchased_aa();
        e->set_ability_id(aa.abilityId);
        e->set_rank(aa.rank);
        // AA title resolved from OP_SendAATable (eql; titleSID -> dbstr type-1).
        // Empty on live/test or before the table arrives -> web keeps "#<id>".
        const QString nm = p.aaName(aa.abilityId);
        if (!nm.isEmpty())
            e->set_name(nm.toStdString());
    }
}

void fillGuildMotd(seq::v1::GuildMotd* out, GuildShell& gs)
{
    out->set_guild_id(gs.guildId());
    out->set_message(gs.motd().toStdString());
    out->set_sender(gs.motdSender().toStdString());
}

void fillGuildRankNames(seq::v1::GuildRankNames* out, GuildShell& gs)
{
    out->set_guild_id(gs.guildId());
    // QMap iterates keys ascending, so the parallel arrays come out sorted by
    // rank ordinal — deterministic across runs (no golden flap).
    const QMap<uint32_t, QString>& ranks = gs.rankNames();
    for (auto it = ranks.constBegin(); it != ranks.constEnd(); ++it) {
        out->add_rank_ids(it.key());
        out->add_names(it.value().toStdString());
    }
}

void fillGuildRoster(seq::v1::GuildRoster* out, GuildShell& gs)
{
    out->set_guild_id(gs.guildId());
    // GuildShell keys members by name in a QHash, so emit in name order —
    // otherwise the row order flips with the hash seed and tier-2 goldens flap.
    QStringList names = gs.members().keys();
    names.sort();
    for (const QString& name : names) {
        const GuildMember* gm = gs.members().value(name, nullptr);
        if (!gm)
            continue;
        auto* m = out->add_members();
        m->set_name(gm->name().toStdString());
        m->set_level(gm->level());
        m->set_class_(gm->classVal());
        m->set_class_mask(gm->classMask());
        m->set_rank(gm->guildRank());
        m->set_last_on(uint32_t(gm->lastOn()));
        m->set_banker(gm->bankRank() != 0);
        m->set_alt(gm->altRank() != 0);
        m->set_full_member(gm->memberRank() != 0);
        m->set_public_note(gm->publicNote().toStdString());
        m->set_zone_id(gm->zoneId());
    }
}

void fillGroupUpdate(seq::v1::GroupUpdate* out, GroupMgr& g)
{
    for (uint16_t slot = 0; slot < MAX_GROUP_PEERS; ++slot) {
        auto* m = out->add_members();
        m->set_slot(slot);
        const QString name = g.memberNameBySlot(slot);
        m->set_name(name.toStdString());
        const Spawn* sp = g.memberBySlot(slot);
        if (sp) {
            m->set_in_zone(true);
            m->set_spawn_id(sp->id());
            m->set_level(sp->level());
            m->set_class_(sp->classVal());
        } else {
            m->set_in_zone(false);
        }
    }
}

static void appendFilterRules(seq::v1::FilterRulesUpdate* out,
                              const Filters* filters, bool perZone)
{
    if (!filters) return;
    // FilterTypeDefs values are 0..6 (Hunt..Tracer); SIZEOF_FILTERS is 7.
    for (uint8_t type = 0; type < SIZEOF_FILTERS; ++type) {
        const int n = filters->numFilters(type);
        for (int i = 0; i < n; ++i) {
            auto* row = out->add_rules();
            row->set_filter_type(type);
            row->set_pattern(filters->getOrigFilterString(type, i).toStdString());
            row->set_min_level(static_cast<uint32_t>(filters->getMinLevel(type, i)));
            row->set_max_level(static_cast<uint32_t>(filters->getMaxLevel(type, i)));
            row->set_per_zone(perZone);
        }
    }
}

void fillFilterRulesUpdate(seq::v1::FilterRulesUpdate* out, const FilterMgr& fm)
{
    appendFilterRules(out, fm.globalFilters(), /*perZone=*/false);
    appendFilterRules(out, fm.zoneFilters(),   /*perZone=*/true);
}

void fillCategoriesUpdate(seq::v1::CategoriesUpdate* out, CategoryMgr& cm)
{
    const CategoryList& all = cm.getCategories();
    for (int i = 0; i < all.size(); ++i) {
        const Category* c = all.at(i);
        if (!c) continue;
        auto* row = out->add_categories();
        row->set_id(static_cast<uint32_t>(i));
        row->set_name(c->name().toStdString());
        row->set_color(c->color().name().toStdString());
    }
}

void fillSpawnPoint(seq::v1::SpawnPoint* out, const SpawnPoint& sp,
                    bool deterministic)
{
    out->set_key(sp.key().toStdString());
    // X/Y negation per the screen-convention contract documented in
    // fillPos. The `key` string still uses the legacy decimal x|y|z
    // encoding (raw EQ coords) — opaque to the client, used by the
    // daemon as a stable hash key.
    out->set_x(-sp.x());
    out->set_y(-sp.y());
    out->set_z(sp.z());
    out->set_name(sp.name().toStdString());
    out->set_last(sp.last().toStdString());
    out->set_last_id(sp.lastID());
    out->set_count(static_cast<uint32_t>(sp.count()));
    if (deterministic) {
        out->set_spawn_time_s(0);
        out->set_death_time_s(0);
        out->set_diff_time_s(0);
    } else {
        out->set_spawn_time_s(static_cast<uint64_t>(sp.spawnTime()));
        out->set_death_time_s(static_cast<uint64_t>(sp.deathTime()));
        out->set_diff_time_s(static_cast<uint64_t>(sp.diffTime()));
    }
}

void fillBuff(seq::v1::Buff* out, const SpellItem& s)
{
    out->set_spell_id(s.spellId());
    out->set_spell_name(s.spellName().toStdString());
    out->set_duration_s(s.duration());
    out->set_caster_id(s.casterId());
    out->set_caster_name(s.casterName().toStdString());
    out->set_target_id(s.targetId());
    out->set_target_name(s.targetName().toStdString());
    out->set_is_song(s.isSong());
    out->set_beneficial(s.beneficial());
    out->set_icon(s.icon());
}

void fillItem(seq::v1::Item* out, const ItemTemplate& in)
{
    out->set_id(in.itemId);
    out->set_name(in.itemName.toStdString());
    if (in.loreName != in.itemName) {
        out->set_lore_name(in.loreName.toStdString());
    }
    out->set_slot_mask(in.slotBitmask);
    out->set_flags(in.flags);
    out->set_weight(in.weight);
    out->set_hp(in.hp);
    out->set_mana(in.mana);
    out->set_endurance(in.endurance);
    out->set_ac(in.ac);
    for (int i = 0; i < ITEM_STAT_COUNT; i++) {
        out->add_stats(in.stats[i]);
    }
    for (int i = 0; i < ITEM_RES_COUNT; i++) {
        out->add_resists(in.resists[i]);
    }
    out->set_corruption(in.corruption);
}

void fillItemTotals(seq::v1::ItemCacheTotals* out, const ItemCache& cache)
{
    auto t = cache.totals();
    out->set_item_count(static_cast<uint32_t>(t.itemCount));
    out->set_hp(t.hp);
    out->set_mana(t.mana);
    out->set_endurance(t.endurance);
    out->set_ac(t.ac);
    for (int i = 0; i < ITEM_STAT_COUNT; i++) {
        out->add_stats(t.stats[i]);
    }
    for (int i = 0; i < ITEM_RES_COUNT; i++) {
        out->add_resists(t.resists[i]);
    }
    out->set_corruption(t.corruption);
}

void fillWornSet(seq::v1::WornSet* out, const ItemCache& cache)
{
    auto worn = cache.wornSlots();
    auto slotIndices = worn.keys();
    std::sort(slotIndices.begin(), slotIndices.end());
    for (int slot : slotIndices) {
        out->add_slot_indices(slot);
        out->add_item_ids(worn.value(slot));
    }
}

void fillInspectAnswer(seq::v1::InspectAnswer* out, const inspectDataStruct& in)
{
    out->set_spawn_id(in.spawnId);
    for (int i = 0; i < 23; i++)
        out->add_item_names(std::string(in.itemNames[i],
                                        strnlen(in.itemNames[i], 64)));
    out->set_bio(std::string(in.mytext, strnlen(in.mytext, 200)));
}

void fillExperienceTick(seq::v1::ExperienceTick* out,
                        const QString& mobName, int mobLevel,
                        uint32_t xpGained, const QString& zoneName)
{
    out->set_mob_name(mobName.toStdString());
    out->set_mob_level(static_cast<uint32_t>(mobLevel));
    out->set_xp_gained(xpGained);
    out->set_zone_name(zoneName.toStdString());
}

} // namespace seq::encode
