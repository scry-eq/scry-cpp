/*
 * itemcache.cpp - see itemcache.h.
 *
 * JSON schema: a top-level array of item objects, sorted by itemId for
 * stable diffs. Zero-valued numeric fields are omitted to keep the file
 * compact. Unknown JSON keys are tolerated on load so future
 * parsedItemTemplateStruct fields can be added without invalidating
 * existing caches.
 */

#include "itemcache.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>

#include <algorithm>

namespace {

constexpr int kDefaultFlushMs = 15 * 60 * 1000;

// EQ slot enum: 0=Charm .. 22=Ammo are worn slots; 0x23=35 is the cursor.
// See itempacket.h for the full mapping.
constexpr uint16_t kMaxWornSlot   = 22;
constexpr uint16_t kCursorSlot    = 0x23;

QJsonObject toJson(const ItemTemplate& t)
{
    QJsonObject o;
    if (!t.serial.isEmpty()) o.insert(QStringLiteral("serial"), t.serial);
    o.insert(QStringLiteral("id"),       qint64(t.itemId));
    o.insert(QStringLiteral("name"),     t.itemName);
    if (t.loreName != t.itemName) {
        o.insert(QStringLiteral("lore"), t.loreName);
    }
    o.insert(QStringLiteral("slotMask"), qint64(t.slotBitmask));
    if (t.icon) o.insert(QStringLiteral("icon"), qint64(*t.icon));
    if (t.wireStackCount)
        o.insert(QStringLiteral("stackCount"), qint64(*t.wireStackCount));
    if (t.wireFlags) o.insert(QStringLiteral("flags"), qint64(*t.wireFlags));
    else if (t.flags) o.insert(QStringLiteral("flags"), qint64(t.flags));
    if (t.weightTenths)
        o.insert(QStringLiteral("weightTenths"), qint64(*t.weightTenths));
    else if (t.weight != 0.0f)
        o.insert(QStringLiteral("weight"), double(t.weight));
    if (t.hp)        o.insert(QStringLiteral("hp"),        t.hp);
    if (t.mana)      o.insert(QStringLiteral("mana"),      t.mana);
    if (t.endurance) o.insert(QStringLiteral("endurance"), t.endurance);
    if (t.ac)        o.insert(QStringLiteral("ac"),        t.ac);

    bool anyStat = false;
    for (int i = 0; i < ITEM_STAT_COUNT; i++) {
        if (t.stats[i]) { anyStat = true; break; }
    }
    if (anyStat) {
        QJsonArray stats;
        for (int i = 0; i < ITEM_STAT_COUNT; i++) stats.append(t.stats[i]);
        o.insert(QStringLiteral("stats"), stats);
    }

    bool anyResist = false;
    for (int i = 0; i < ITEM_RES_COUNT; i++) {
        if (t.resists[i]) { anyResist = true; break; }
    }
    if (anyResist) {
        QJsonArray resists;
        for (int i = 0; i < ITEM_RES_COUNT; i++) resists.append(t.resists[i]);
        o.insert(QStringLiteral("resists"), resists);
    }

    if (t.wireCorruption)
        o.insert(QStringLiteral("corruption"), *t.wireCorruption);
    else if (t.corruption)
        o.insert(QStringLiteral("corruption"), t.corruption);
    o.insert(QStringLiteral("containerId"), qint64(t.containerId));
    o.insert(QStringLiteral("containerSlot"), int(t.containerSlot));
    o.insert(QStringLiteral("parentSlot"), int(t.parentSlot));
    return o;
}

ItemTemplate fromJson(const QJsonObject& o)
{
    ItemTemplate t;
    t.serial      = o.value(QStringLiteral("serial")).toString();
    t.itemId      = uint32_t(o.value(QStringLiteral("id")).toVariant().toULongLong());
    t.itemName    = o.value(QStringLiteral("name")).toString();
    t.loreName    = o.value(QStringLiteral("lore")).toString();
    if (t.loreName.isEmpty()) t.loreName = t.itemName;
    t.slotBitmask = uint32_t(o.value(QStringLiteral("slotMask")).toVariant().toULongLong());
    if (o.contains(QStringLiteral("icon")))
        t.icon = uint32_t(o.value(QStringLiteral("icon")).toVariant().toULongLong());
    if (o.contains(QStringLiteral("stackCount"))) {
        t.wireStackCount = uint32_t(o.value(QStringLiteral("stackCount")).toVariant().toULongLong());
        t.stackCount = *t.wireStackCount;
    }
    if (o.contains(QStringLiteral("flags"))) {
        t.wireFlags = uint32_t(o.value(QStringLiteral("flags")).toVariant().toULongLong());
        t.flags = *t.wireFlags;
    }
    if (o.contains(QStringLiteral("weightTenths"))) {
        t.weightTenths = uint32_t(o.value(QStringLiteral("weightTenths")).toVariant().toULongLong());
        t.weight = float(*t.weightTenths) / 10.0f;
    } else {
        t.weight = float(o.value(QStringLiteral("weight")).toDouble());
    }
    t.hp          = o.value(QStringLiteral("hp")).toInt();
    t.mana        = o.value(QStringLiteral("mana")).toInt();
    t.endurance   = o.value(QStringLiteral("endurance")).toInt();
    t.ac          = o.value(QStringLiteral("ac")).toInt();

    auto stats = o.value(QStringLiteral("stats")).toArray();
    for (int i = 0; i < ITEM_STAT_COUNT && i < stats.size(); i++) {
        t.stats[i] = int8_t(stats.at(i).toInt());
    }
    auto resists = o.value(QStringLiteral("resists")).toArray();
    for (int i = 0; i < ITEM_RES_COUNT && i < resists.size(); i++) {
        t.resists[i] = int8_t(resists.at(i).toInt());
    }
    if (o.contains(QStringLiteral("corruption"))) {
        t.wireCorruption = o.value(QStringLiteral("corruption")).toInt();
        t.corruption = int8_t(*t.wireCorruption);
    }
    t.containerId = uint32_t(o.value(QStringLiteral("containerId")).toVariant().toULongLong());
    t.containerSlot = uint16_t(o.value(QStringLiteral("containerSlot")).toInt());
    t.parentSlot = uint16_t(o.value(QStringLiteral("parentSlot")).toInt());
    return t;
}

} // namespace

ItemCache::ItemCache(QObject* parent)
    : QObject(parent)
    , m_flushTimer(new QTimer(this))
{
    m_flushTimer->setInterval(kDefaultFlushMs);
    m_flushTimer->setSingleShot(false);
    connect(m_flushTimer, &QTimer::timeout, this, &ItemCache::onFlushTimer);
}

ItemCache::~ItemCache()
{
    if (m_dirty && !m_storePath.isEmpty()) {
        save();
    }
}

void ItemCache::setStorePath(const QString& path)
{
    m_storePath = path;
    if (!path.isEmpty()) {
        load();
        if (m_flushTimer->interval() > 0) m_flushTimer->start();
    } else {
        m_flushTimer->stop();
    }
}

void ItemCache::setFlushIntervalMs(int ms)
{
    if (ms <= 0) {
        m_flushTimer->stop();
        m_flushTimer->setInterval(0);
        return;
    }
    m_flushTimer->setInterval(ms);
    if (!m_storePath.isEmpty()) m_flushTimer->start();
}

bool ItemCache::lookup(uint32_t itemId, ItemTemplate* out) const
{
    auto it = m_cache.constFind(itemId);
    if (it == m_cache.constEnd()) return false;
    if (out) *out = it.value();
    return true;
}

void ItemCache::insert(const ItemTemplate& tpl)
{
    if (tpl.itemId == 0) return;
    auto it = m_cache.find(tpl.itemId);
    if (it == m_cache.end()) ++m_learnedCount;
    m_cache.insert(tpl.itemId, tpl);
    m_dirty = true;
    emit itemLearned(tpl.itemId);
}

void ItemCache::replaceInventory(const QList<ItemTemplate>& items)
{
    m_inventory.clear();
    m_cache.clear();
    for (const ItemTemplate& item : items) {
        const QString key = item.serial.isEmpty()
            ? QStringLiteral("%1:%2:%3:%4")
                  .arg(item.itemId).arg(item.containerId)
                  .arg(item.containerSlot).arg(item.parentSlot)
            : item.serial;
        m_inventory.insert(key, item);
        if (item.itemId != 0) m_cache.insert(item.itemId, item);
    }
    m_dirty = true;
    for (const ItemTemplate& item : items) {
        if (item.itemId != 0) emit itemLearned(item.itemId);
    }
}

void ItemCache::applyInventoryItem(
    const ItemTemplate& item,
    std::optional<uint32_t> previousContainerId,
    std::optional<uint16_t> previousContainerSlot,
    std::optional<uint16_t> previousParentSlot)
{
    const QString key = item.serial.isEmpty()
        ? QStringLiteral("%1:%2:%3:%4")
              .arg(item.itemId).arg(item.containerId)
              .arg(item.containerSlot).arg(item.parentSlot)
        : item.serial;
    if (item.serial.isEmpty() && previousContainerId &&
        previousContainerSlot && previousParentSlot) {
        const QString previousKey = QStringLiteral("%1:%2:%3:%4")
            .arg(item.itemId).arg(*previousContainerId)
            .arg(*previousContainerSlot).arg(*previousParentSlot);
        if (previousKey != key) m_inventory.remove(previousKey);
    }
    m_inventory.insert(key, item);
    insert(item);
}

void ItemCache::replaceEquipment(const QHash<int, uint32_t>& equipment)
{
    // Clear first. Consumers must never observe a snapshot with stale slots
    // from the previous authoritative inventory.
    m_wornSlots.clear();
    for (auto it = equipment.cbegin(); it != equipment.cend(); ++it) {
        if (it.key() >= 0 && it.key() <= kMaxWornSlot && it.value() != 0)
            m_wornSlots.insert(it.key(), it.value());
    }
    emit wornSlotsChanged();
}

void ItemCache::clearEquipmentSlot(int slot)
{
    if (m_wornSlots.remove(slot) > 0) emit wornSlotsChanged();
}

void ItemCache::setEquipmentSlot(int slot, uint32_t itemId)
{
    if (slot < 0 || slot > kMaxWornSlot || itemId == 0) return;
    if (m_wornSlots.value(slot) == itemId && m_wornSlots.contains(slot)) return;
    m_wornSlots.insert(slot, itemId);
    emit wornSlotsChanged();
}

ItemCache::Totals ItemCache::totals() const
{
    Totals t;
    for (auto it = m_wornSlots.constBegin(); it != m_wornSlots.constEnd(); ++it) {
        auto cacheIt = m_cache.constFind(it.value());
        if (cacheIt == m_cache.constEnd()) continue;
        const ItemTemplate& v = cacheIt.value();
        t.itemCount++;
        t.hp        += v.hp;
        t.mana      += v.mana;
        t.endurance += v.endurance;
        t.ac        += v.ac;
        for (int i = 0; i < ITEM_STAT_COUNT; i++) t.stats[i]   += v.stats[i];
        for (int i = 0; i < ITEM_RES_COUNT;  i++) t.resists[i] += v.resists[i];
        t.corruption += v.corruption;
    }
    return t;
}

QList<uint32_t> ItemCache::sortedIds() const
{
    auto ids = m_cache.keys();
    std::sort(ids.begin(), ids.end());
    return ids;
}

void ItemCache::onItemPacket(const uint8_t* data, size_t len,
                             uint8_t /*dir*/)
{
    ItemTemplate t;
    if (!parseItemPacket(data, len, &t)) return;
    applyInventoryItem(t);

    // Track worn-slot membership from the wrapper. Top-level items
    // (mainSlot==0) carry the worn/inv/cursor slot directly in subSlot.
    // Items inside bags (mainSlot!=0) never affect the worn map.
    if (t.mainSlot != 0 || t.itemId == 0) return;

    bool changed = false;

    // An item moving anywhere always vacates whichever worn slot it
    // previously occupied (slot-to-slot swaps + un-equip-to-cursor +
    // un-equip-to-inventory all share this property).
    for (auto it = m_wornSlots.begin(); it != m_wornSlots.end(); ) {
        if (it.value() == t.itemId) {
            it = m_wornSlots.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }

    if (t.subSlot <= kMaxWornSlot) {
        m_wornSlots.insert(int(t.subSlot), t.itemId);
        changed = true;
    }
    // subSlot == kCursorSlot or any inv slot (>=23) leaves the worn
    // map cleared above; nothing further to insert.

    if (changed) emit wornSlotsChanged();
}

void ItemCache::onFlushTimer()
{
    if (m_dirty && !m_storePath.isEmpty()) save();
}

bool ItemCache::load()
{
    QFile f(m_storePath);
    if (!f.exists()) {
        qInfo("ItemCache: no existing cache at %s (first run)",
              qUtf8Printable(m_storePath));
        return true;
    }
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning("ItemCache::load: cannot open %s for read",
                 qUtf8Printable(m_storePath));
        return false;
    }
    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray()) {
        qWarning("ItemCache::load: invalid JSON in %s: %s",
                 qUtf8Printable(m_storePath),
                 qUtf8Printable(err.errorString()));
        return false;
    }
    auto arr = doc.array();
    m_cache.reserve(arr.size());
    for (const auto& v : arr) {
        if (!v.isObject()) continue;
        auto t = fromJson(v.toObject());
        if (t.itemId == 0) continue;
        m_cache.insert(t.itemId, t);
        if (!t.serial.isEmpty()) {
            m_inventory.insert(t.serial, t);
            if (t.parentSlot == 0xFFFF && t.containerSlot <= kMaxWornSlot)
                m_wornSlots.insert(int(t.containerSlot), t.itemId);
        }
    }
    qInfo("ItemCache: loaded %d items from %s",
          int(m_cache.size()), qUtf8Printable(m_storePath));
    m_dirty = false;
    return true;
}

bool ItemCache::save()
{
    if (m_storePath.isEmpty()) return false;

    QJsonArray arr;
    if (!m_inventory.isEmpty()) {
        auto serials = m_inventory.keys();
        std::sort(serials.begin(), serials.end());
        for (const QString& serial : serials)
            arr.append(toJson(m_inventory.value(serial)));
    } else {
        auto ids = m_cache.keys();
        std::sort(ids.begin(), ids.end());
        for (uint32_t id : ids)
            arr.append(toJson(m_cache.value(id)));
    }
    QJsonDocument doc(arr);

    QSaveFile f(m_storePath);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning("ItemCache::save: cannot open %s for write",
                 qUtf8Printable(m_storePath));
        return false;
    }
    f.write(doc.toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        qWarning("ItemCache::save: commit failed for %s",
                 qUtf8Printable(m_storePath));
        return false;
    }
    qInfo("ItemCache: saved %d items (%d new this session) to %s",
          int(m_cache.size()), m_learnedCount,
          qUtf8Printable(m_storePath));
    m_dirty = false;
    return true;
}
