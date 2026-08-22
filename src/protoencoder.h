#pragma once

// Pure translation functions between in-memory showeq types (Item, Spawn)
// and seq.v1 protobuf messages. Side-effect free so sessionadapter.cpp can
// stay focused on wire I/O, and so the future Rust decoder (Phase 4) has a
// clear spec to match byte-for-byte.

#include <vector>

#include <QDateTime>
#include <QString>

#include "seq/v1/events.pb.h"
#include "mappackagehost.h"

class CategoryMgr;
class FilterMgr;
struct inspectDataStruct;
class GroupMgr;
class GuildShell;
class Item;
class ItemCache;
struct ItemTemplate;
class Player;
class Spawn;
class SpawnPoint;
class SpellItem;
class MapData;

namespace seq::encode {

// Fills `out` from `in`. Populates pos, type, ids, name, levels, HP — all
// fields the Phase 1 web client needs to draw and label a moving dot.
// When `categories` is non-null, also fills `category_ids` with the
// indices of any matching CategoryMgr Categories. `filterMgr`, when
// passed alongside, lets categories that key off filter-type names
// (e.g. seqdef Hunting → ":Hunt:") match — see fillSpawn impl for the
// filterString prefix it inserts.
void fillSpawn(seq::v1::Spawn* out, const Item& in,
               const CategoryMgr* categories = nullptr,
               const FilterMgr* filterMgr = nullptr);

// Translates an OP_InspectAnswer payload into the wire InspectAnswer proto.
// item_names has 23 entries in EQ worn-slot order (empty string = empty slot).
void fillInspectAnswer(seq::v1::InspectAnswer* out, const inspectDataStruct& in);

// Fills a Pos proto from a Spawn's current position + velocity + heading.
void fillPos(seq::v1::Pos* out, const Spawn& in);

// Flattens every layer of `map` into a MapGeometry (lines + named
// locations + bounding rect). Safe to call on an unloaded MapData — the
// output will be empty geometry with zeroed bounds, which callers are
// expected to treat as "no map available for this zone".
void fillMapGeometry(seq::v1::MapGeometry* out, const MapData& map);

// Fills `out` with one MapPackage per discovered package and sets
// active_id. Trivial id/label/zone_count copy; discovery + active-state
// tracking live in DaemonApp (IMapPackageHost).
void fillMapPackages(seq::v1::MapPackagesUpdate* out,
                     const std::vector<MapPackageInfo>& packages,
                     const QString& activeId);

// Fills `out` from current Player state (HP/mana/stamina/exp/level/stats).
// Always fills every field; fields are 0 before the player profile loads,
// which the client renders as "—".
void fillPlayerStats(seq::v1::PlayerStats* out, const Player& p);

// Fills `out` with all 6 group slots from `g`. Slots without a member
// produce an empty GroupMember (in_zone=false, name="").
void fillGroupUpdate(seq::v1::GroupUpdate* out, GroupMgr& g);
// Roster rows are emitted in name order: GuildShell keys them by name in a
// QHash, whose iteration order is seed-dependent.
void fillGuildRoster(seq::v1::GuildRoster* out, GuildShell& gs);
void fillGuildMotd(seq::v1::GuildMotd* out, GuildShell& gs);
void fillGuildRankNames(seq::v1::GuildRankNames* out, GuildShell& gs);

// Fills `out` with one Buff message from a SpellShell SpellItem.
void fillBuff(seq::v1::Buff* out, const SpellItem& spell);

// Fills `out` with one Category entry per CategoryMgr category, in the
// daemon's iteration order — id is the index into that order, which is
// what fillSpawn writes into Spawn.category_ids.
void fillCategoriesUpdate(seq::v1::CategoriesUpdate* out, CategoryMgr& cm);

// Walks both the global and per-zone Filters in `fm` and emits one
// FilterRule per concrete pattern. per_zone is set on rules that came
// from the zone overlay file.
void fillFilterRulesUpdate(seq::v1::FilterRulesUpdate* out, const FilterMgr& fm);

// Snapshots a SpawnMonitor SpawnPoint into the wire form. The `key`
// field uses the legacy SpawnPoint::key() format so resume + replay
// keep stable identity across reconnects.
//
// `deterministic` zeroes the wall-clock time_t fields (spawn/death/
// diff) so the regression-harness byte-cmp stays stable across runs.
// SessionAdapter passes its m_deterministic state through; only the
// golden recorder sets it today.
void fillSpawnPoint(seq::v1::SpawnPoint* out, const SpawnPoint& sp,
                    bool deterministic = false);

// Translates a parsed ItemTemplate into the wire seq.v1.Item. Stats and
// resists arrays are written in their canonical order (see itempacket.h
// ItemStatIndex / ItemResistIndex).
void fillItem(seq::v1::Item* out, const ItemTemplate& in);

// Aggregate sums over the player's currently equipped gear. See the
// proto comment on ItemCacheTotals for semantics.
void fillItemTotals(seq::v1::ItemCacheTotals* out, const ItemCache& cache);

// Slot → itemId map of currently equipped gear, mirroring
// ItemCache::wornSlots().
void fillWornSet(seq::v1::WornSet* out, const ItemCache& cache);

// Fills `out` from the per-kill experience tick emitted by Player::expGained.
// xpGained is on the 0-100000 per-level scale (1000 = 0.1% of a level).
void fillExperienceTick(seq::v1::ExperienceTick* out,
                        const QString& mobName, int mobLevel,
                        uint32_t xpGained, const QString& zoneName);

// Phase-4 lifecycle projection. These are the production constructors used by
// SessionAdapter and by the shadow comparator. Keeping one implementation is
// what makes the comparison exercise the public seq.v1 contract instead of a
// second set of test-only expected messages.
seq::v1::Envelope zoneChanged(const QString& shortName,
                              const QString& longName,
                              const MapData* map);
seq::v1::Envelope eqTimeSync(const QDateTime& dateTime);
seq::v1::Envelope zoneServer(const QString& host, quint16 port);
QString compatibilityDoorName(const QString& semanticName, uint32_t id);
QString compatibilityGroundItemName(const QString& actorDefinition);
QString compatibilitySpawnName(const QString& semanticName);

} // namespace seq::encode
