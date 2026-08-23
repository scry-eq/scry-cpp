/*
 *  itemcache_test.cpp
 *  Tier-1 unit test for ItemCache (insert/lookup, JSON round-trip,
 *  dirty-flag semantics, and zero-suppressing JSON serialization).
 */

#include <QtTest/QtTest>

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <vector>

#include "itemcache.h"

class ItemCacheTest : public QObject
{
  Q_OBJECT

private slots:
  void emptyByDefault();
  void insertAndLookup();
  void insertIgnoresZeroId();
  void duplicateInsertOverwritesAndCountsOnce();
  void roundTripsThroughJson();
  void preservesAllStatFieldsThroughJson();
  void preservesPhaseSevenOptionalFieldsByteExactly();
  void semanticSnapshotsReplaceInventoryAndEquipment();
  void seriallessMoveConsumesPreviousLocation();
  void omitsZeroFieldsFromJson();
  void loadFromMissingFileSucceedsEmpty();
  void wornSlotsTrackTopLevelEquipFire();
  void wornSlotsClearOnCursorMove();
  void wornSlotsIgnoreInBagFires();
  void wornSlotsHandleSlotToSlotSwap();
  void totalsSumWornSetOnly();

private:
  static ItemTemplate sampleArmor();
  static ItemTemplate sampleRing();
  // Build a synthetic OP_ItemPacket wrapper + minimal payload that
  // parseItemPacket() accepts. Lets us drive ItemCache::onItemPacket
  // through its full slot-tracking path without recapitulating
  // itempacket_test's coverage.
  static std::vector<uint8_t> buildSyntheticWrapper(
      uint32_t packetType, uint32_t itemId, uint32_t mainSlot,
      uint16_t subSlot, uint32_t stack);
};

ItemTemplate ItemCacheTest::sampleArmor()
{
    ItemTemplate t;
    t.itemId       = 111843;
    t.itemName     = QStringLiteral("Outstanding Necklace of Distant Echoes");
    t.loreName     = t.itemName;
    t.slotBitmask  = 0x20;
    t.flags        = 0x01000001;
    t.weight       = 0.8f;
    t.hp           = 122;
    t.mana         = 124;
    t.endurance    = 90;
    t.ac           = 16;
    t.stats[ITEM_STAT_STR] = 10;
    t.stats[ITEM_STAT_STA] = 10;
    t.stats[ITEM_STAT_AGI] = 10;
    t.stats[ITEM_STAT_DEX] = 10;
    t.stats[ITEM_STAT_CHA] = 10;
    t.stats[ITEM_STAT_INT] = 10;
    t.stats[ITEM_STAT_WIS] = 10;
    t.resists[ITEM_RES_COLD]    = 7;
    t.resists[ITEM_RES_DISEASE] = 7;
    t.resists[ITEM_RES_POISON]  = 7;
    t.resists[ITEM_RES_MAGIC]   = 8;
    t.resists[ITEM_RES_FIRE]    = 7;
    t.corruption = 1;
    return t;
}

ItemTemplate ItemCacheTest::sampleRing()
{
    ItemTemplate t;
    t.itemId      = 14679;
    t.itemName    = QStringLiteral("Platinum Fire Wedding Ring");
    t.loreName    = t.itemName;
    t.slotBitmask = 0x18000;
    t.weight      = 0.1f;
    t.hp          = 55;
    t.ac          = 5;
    return t;
}

std::vector<uint8_t> ItemCacheTest::buildSyntheticWrapper(
    uint32_t packetType, uint32_t itemId, uint32_t mainSlot,
    uint16_t subSlot, uint32_t stack)
{
    std::vector<uint8_t> buf;
    auto pushU32 = [&](uint32_t v) {
        for (int i = 0; i < 4; i++) buf.push_back(uint8_t(v >> (8*i)));
    };
    pushU32(packetType);
    static const char kInstanceId[] = "TEST00B80003TST0";
    buf.insert(buf.end(), kInstanceId, kInstanceId + 16);
    buf.push_back(0);
    pushU32(stack);
    pushU32(mainSlot);
    buf.push_back(uint8_t(subSlot));
    buf.push_back(uint8_t(subSlot >> 8));
    while (buf.size() < 0x70) buf.push_back(0);
    for (int i = 0; i < 4; i++) buf.push_back(0xff);
    for (int i = 0; i < 4; i++) buf.push_back(0x00);
    static const char kName[] = "Test Worn Item";
    buf.insert(buf.end(), kName, kName + sizeof(kName));
    buf.insert(buf.end(), kName, kName + sizeof(kName));
    pushU32(63); pushU32(0); pushU32(itemId); pushU32(0);
    pushU32(0); pushU32(0); pushU32(0); pushU32(0);
    buf.push_back(0); buf.push_back(0);
    for (int i = 0; i < 5; i++) buf.push_back(0);
    buf.push_back(0);
    for (int i = 0; i < 7; i++) buf.push_back(0);
    for (int i = 0; i < 16; i++) buf.push_back(0);
    return buf;
}

void ItemCacheTest::emptyByDefault()
{
    ItemCache c;
    QCOMPARE(c.size(), 0);
    QCOMPARE(c.itemsLearnedThisSession(), 0);
    ItemTemplate dst;
    QVERIFY(!c.lookup(1234, &dst));
}

void ItemCacheTest::insertAndLookup()
{
    ItemCache c;
    c.insert(sampleArmor());
    QCOMPARE(c.size(), 1);
    QCOMPARE(c.itemsLearnedThisSession(), 1);

    ItemTemplate dst;
    QVERIFY(c.lookup(111843, &dst));
    QCOMPARE(dst.itemName, QStringLiteral("Outstanding Necklace of Distant Echoes"));
    QCOMPARE(dst.hp, 122);
    QCOMPARE(dst.mana, 124);

    QVERIFY(!c.lookup(99999, &dst));
}

void ItemCacheTest::insertIgnoresZeroId()
{
    ItemCache c;
    ItemTemplate t = sampleRing();
    t.itemId = 0;
    c.insert(t);
    QCOMPARE(c.size(), 0);
    QCOMPARE(c.itemsLearnedThisSession(), 0);
}

void ItemCacheTest::duplicateInsertOverwritesAndCountsOnce()
{
    ItemCache c;
    c.insert(sampleArmor());
    QCOMPARE(c.itemsLearnedThisSession(), 1);

    // Re-insert same id with mutated fields: should overwrite without
    // bumping the learned counter.
    ItemTemplate t = sampleArmor();
    t.hp = 999;
    c.insert(t);
    QCOMPARE(c.size(), 1);
    QCOMPARE(c.itemsLearnedThisSession(), 1);

    ItemTemplate dst;
    QVERIFY(c.lookup(111843, &dst));
    QCOMPARE(dst.hp, 999);
}

void ItemCacheTest::roundTripsThroughJson()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.path() + QLatin1String("/itemcache.json");

    {
        ItemCache c;
        c.setStorePath(path);
        c.insert(sampleArmor());
        c.insert(sampleRing());
        QVERIFY(c.save());
    }

    ItemCache c2;
    c2.setStorePath(path);
    QCOMPARE(c2.size(), 2);

    ItemTemplate ring;
    QVERIFY(c2.lookup(14679, &ring));
    QCOMPARE(ring.itemName, QStringLiteral("Platinum Fire Wedding Ring"));
    QCOMPARE(ring.hp, 55);
    QCOMPARE(ring.ac, 5);
    QCOMPARE(ring.slotBitmask, uint32_t(0x18000));
    QCOMPARE(ring.weight, 0.1f);

    ItemTemplate neck;
    QVERIFY(c2.lookup(111843, &neck));
    QCOMPARE(neck.hp, 122);
    QCOMPARE(neck.mana, 124);
    QCOMPARE(neck.stats[ITEM_STAT_STR], int8_t(10));
    QCOMPARE(neck.resists[ITEM_RES_MAGIC], int8_t(8));
    QCOMPARE(neck.corruption, int8_t(1));
}

void ItemCacheTest::preservesAllStatFieldsThroughJson()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.path() + QLatin1String("/itemcache.json");

    ItemTemplate src = sampleArmor();
    {
        ItemCache c;
        c.setStorePath(path);
        c.insert(src);
        QVERIFY(c.save());
    }

    ItemCache c2;
    c2.setStorePath(path);
    ItemTemplate got;
    QVERIFY(c2.lookup(src.itemId, &got));
    for (int i = 0; i < ITEM_STAT_COUNT; i++) {
        QCOMPARE(got.stats[i], src.stats[i]);
    }
    for (int i = 0; i < ITEM_RES_COUNT; i++) {
        QCOMPARE(got.resists[i], src.resists[i]);
    }
    QCOMPARE(got.corruption, src.corruption);
    QCOMPARE(got.slotBitmask, src.slotBitmask);
    QCOMPARE(got.flags, src.flags);
}

void ItemCacheTest::preservesPhaseSevenOptionalFieldsByteExactly()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.path() + QLatin1String("/itemcache.json");
    ItemTemplate source = sampleArmor();
    source.serial = QStringLiteral("instance-7");
    source.icon = 0;
    source.wireStackCount = 0;
    source.weightTenths = 8;
    source.wireFlags = 0;
    source.wireCorruption = 0;
    source.containerId = 4;
    source.containerSlot = 2;
    source.parentSlot = 0xFFFF;

    QByteArray first;
    {
        ItemCache cache;
        cache.setStorePath(path);
        cache.applyInventoryItem(source);
        QVERIFY(cache.save());
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        first = file.readAll();
        QVERIFY(first.contains("\"icon\": 0"));
        QVERIFY(first.contains("\"stackCount\": 0"));
        QVERIFY(first.contains("\"flags\": 0"));
        QVERIFY(first.contains("\"corruption\": 0"));
    }

    ItemCache restored;
    restored.setStorePath(path);
    ItemTemplate actual;
    QVERIFY(restored.lookup(source.itemId, &actual));
    QCOMPARE(actual.serial, source.serial);
    QVERIFY(actual.icon && *actual.icon == 0);
    QVERIFY(actual.wireStackCount && *actual.wireStackCount == 0);
    QVERIFY(actual.weightTenths && *actual.weightTenths == 8);
    QVERIFY(actual.wireFlags && *actual.wireFlags == 0);
    QVERIFY(actual.wireCorruption && *actual.wireCorruption == 0);
    QCOMPARE(actual.containerId, uint32_t(4));
    QCOMPARE(actual.containerSlot, uint16_t(2));
    QCOMPARE(actual.parentSlot, uint16_t(0xFFFF));
    QCOMPARE(restored.wornSlots().value(2), source.itemId);
    QVERIFY(restored.save());
    QFile secondFile(path);
    QVERIFY(secondFile.open(QIODevice::ReadOnly));
    QCOMPARE(secondFile.readAll(), first);
}

void ItemCacheTest::semanticSnapshotsReplaceInventoryAndEquipment()
{
    ItemCache cache;
    cache.insert(sampleRing());
    ItemTemplate armor = sampleArmor();
    armor.serial = QStringLiteral("armor-instance");
    armor.containerSlot = 5;
    armor.parentSlot = 0xFFFF;
    cache.replaceInventory(QList<ItemTemplate>{armor});
    QCOMPARE(cache.size(), 1);
    QVERIFY(!cache.lookup(sampleRing().itemId, nullptr));
    QVERIFY(cache.lookup(armor.itemId, nullptr));

    QSignalSpy wornChanged(&cache, &ItemCache::wornSlotsChanged);
    cache.replaceEquipment(QHash<int, uint32_t>{{5, armor.itemId}});
    QCOMPARE(cache.wornSlots().value(5), armor.itemId);
    QCOMPARE(wornChanged.count(), 1);
    cache.clearEquipmentSlot(5);
    QVERIFY(cache.wornSlots().isEmpty());
    cache.setEquipmentSlot(15, armor.itemId);
    QCOMPARE(cache.wornSlots().value(15), armor.itemId);
    QCOMPARE(wornChanged.count(), 3);
}

void ItemCacheTest::seriallessMoveConsumesPreviousLocation()
{
    ItemCache cache;
    ItemTemplate item = sampleRing();
    item.containerId = 0;
    item.containerSlot = 15;
    item.parentSlot = 0xFFFF;
    cache.applyInventoryItem(item);
    QCOMPARE(cache.inventoryInstanceCount(), 1);
    item.containerSlot = 24;
    cache.applyInventoryItem(item, 0, 15, uint16_t(0xFFFF));
    QCOMPARE(cache.inventoryInstanceCount(), 1);
}

void ItemCacheTest::omitsZeroFieldsFromJson()
{
    // A weapon with no HP/mana/AC/stats/resists should serialize compactly.
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.path() + QLatin1String("/itemcache.json");

    ItemTemplate weapon;
    weapon.itemId      = 9999;
    weapon.itemName    = QStringLiteral("Plain Weapon");
    weapon.loreName    = weapon.itemName;
    weapon.slotBitmask = 0x6000;
    weapon.weight      = 3.0f;

    {
        ItemCache c;
        c.setStorePath(path);
        c.insert(weapon);
        QVERIFY(c.save());
    }

    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QByteArray json = f.readAll();
    QVERIFY(!json.contains("\"hp\""));
    QVERIFY(!json.contains("\"mana\""));
    QVERIFY(!json.contains("\"ac\""));
    QVERIFY(!json.contains("\"stats\""));
    QVERIFY(!json.contains("\"resists\""));
    QVERIFY(json.contains("\"name\": \"Plain Weapon\""));
    QVERIFY(json.contains("\"slotMask\""));
}

void ItemCacheTest::loadFromMissingFileSucceedsEmpty()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.path() + QLatin1String("/does-not-exist.json");

    ItemCache c;
    c.setStorePath(path);
    QCOMPARE(c.size(), 0);
}

void ItemCacheTest::wornSlotsTrackTopLevelEquipFire()
{
    ItemCache c;
    QSignalSpy spy(&c, &ItemCache::wornSlotsChanged);

    auto pkt = buildSyntheticWrapper(0x78, 1111, /*main*/ 0, /*sub*/ 5, 1);
    c.onItemPacket(pkt.data(), pkt.size(), 0);
    QCOMPARE(c.wornSlots().value(5), uint32_t(1111));
    QCOMPARE(spy.count(), 1);
}

void ItemCacheTest::wornSlotsClearOnCursorMove()
{
    ItemCache c;
    QSignalSpy spy(&c, &ItemCache::wornSlotsChanged);

    auto equip = buildSyntheticWrapper(0x78, 1111, 0, /*Neck*/ 5,    1);
    c.onItemPacket(equip.data(), equip.size(), 0);
    QCOMPARE(c.wornSlots().value(5), uint32_t(1111));

    auto cursor = buildSyntheticWrapper(0x78, 1111, 0, /*Cursor*/ 0x23, 1);
    c.onItemPacket(cursor.data(), cursor.size(), 0);
    QVERIFY(c.wornSlots().isEmpty());
    QCOMPARE(spy.count(), 2);
}

void ItemCacheTest::wornSlotsIgnoreInBagFires()
{
    ItemCache c;
    QSignalSpy spy(&c, &ItemCache::wornSlotsChanged);

    auto inBag = buildSyntheticWrapper(0x74, 9999, /*main*/ 21, /*sub*/ 3, 10);
    c.onItemPacket(inBag.data(), inBag.size(), 0);
    QVERIFY(c.wornSlots().isEmpty());
    QCOMPARE(spy.count(), 0);
    QCOMPARE(c.size(), 1);   // still cached as a known item template
}

void ItemCacheTest::wornSlotsHandleSlotToSlotSwap()
{
    // Item moves directly from one worn slot to another (e.g. finger
    // swap response) — must vacate the old slot without a cursor stop.
    ItemCache c;
    auto a = buildSyntheticWrapper(0x78, 5555, 0, /*Finger_L*/ 15, 1);
    c.onItemPacket(a.data(), a.size(), 0);
    auto b = buildSyntheticWrapper(0x78, 5555, 0, /*Finger_R*/ 16, 1);
    c.onItemPacket(b.data(), b.size(), 0);

    QVERIFY(!c.wornSlots().contains(15));
    QCOMPARE(c.wornSlots().value(16), uint32_t(5555));
}

void ItemCacheTest::totalsSumWornSetOnly()
{
    // Cached items alone don't contribute to totals — only the worn set.
    ItemCache c;
    c.insert(sampleArmor());            // hp=122, mana=124
    c.insert(sampleRing());             // hp=55,  ac=5

    auto t0 = c.totals();
    QCOMPARE(t0.itemCount, 0);
    QCOMPARE(t0.hp,        0);

    // Drive worn-slot tracking against the same itemIds. Note the
    // synthetic wrapper overwrites the rich cache entry with its empty
    // stat block, so we re-insert the rich templates after to verify
    // totals routes through the cache and reads the fresh stats.
    auto neckWrap = buildSyntheticWrapper(0x78, 111843, 0, /*Neck*/ 5,      1);
    c.onItemPacket(neckWrap.data(), neckWrap.size(), 0);
    auto ringWrap = buildSyntheticWrapper(0x78, 14679,  0, /*Finger_L*/ 15, 1);
    c.onItemPacket(ringWrap.data(), ringWrap.size(), 0);
    c.insert(sampleArmor());            // restore rich stats post-wrapper
    c.insert(sampleRing());

    auto t = c.totals();
    QCOMPARE(t.itemCount, 2);
    QCOMPARE(t.hp,        122 + 55);
    QCOMPARE(t.mana,      124 + 0);
    QCOMPARE(t.ac,        16 + 5);
}

QTEST_GUILESS_MAIN(ItemCacheTest)
#include "itemcache_test.moc"
