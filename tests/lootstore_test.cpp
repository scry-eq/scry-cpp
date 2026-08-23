/*
 *  lootstore_test.cpp
 *  Tier-1 unit test for LootStore: schema creation, append, the dedup key that
 *  lets a second recorder watch the same capture, newest-first reads, and the
 *  closed-store no-op that --replay relies on.
 */

#include <QtTest/QtTest>

#include <QTemporaryDir>

#include "lootstore.h"
#include "messageshell.h"

class LootStoreTest : public QObject
{
  Q_OBJECT

private slots:
  void closedStoreDropsRows();
  void recordsAndReadsBack();
  void readsNewestFirst();
  void clampsTheLimit();
  void dedupsAnAcquisitionBySequence();
  void dedupsAWindowRowByCorpseAndItem();
  void keepsRowsThatHaveNoStableKey();
  void reopensAnExistingDatabaseAndAppends();
  void readOnlyServesButNeverWrites();
  void readOnlyOnAMissingDatabaseStaysClosed();
  void semanticLootWritesExistingSchemaAndProjectsOnce();

private:
  static LootRowRec sale(uint32_t sequence, const QString& item, uint32_t copper,
                         int64_t ts = 1000)
  {
    LootRowRec r;
    r.ts = ts;
    r.source = "message";
    r.itemName = item;
    r.itemId = 7012;
    r.qty = 1;
    r.mobName = "a goblin diviner";
    r.mobNorm = "goblin diviner";
    r.corpseId = 18632;
    r.zoneShort = "greatdivide";
    r.zoneBase = "greatdivide";
    r.sold = true;
    r.moneyCopper = copper;
    r.disposition = "sold";
    r.sequence = sequence;
    return r;
  }

  static LootRowRec window(uint32_t corpseId, const QString& item, int64_t ts = 1000)
  {
    LootRowRec r;
    r.ts = ts;
    r.source = "window";
    r.itemName = item;
    r.itemId = 16884;
    r.icon = 1075;
    r.qty = 1;
    r.mobName = "an ice giant";
    r.mobNorm = "ice giant";
    r.corpseId = corpseId;
    return r;
  }

  static LootRowRec coin(uint32_t corpseId, uint32_t copper, int64_t ts)
  {
    LootRowRec r;
    r.ts = ts;
    r.source = "coin";
    r.itemName = "Coin";
    r.qty = 1;
    r.corpseId = corpseId;
    r.moneyCopper = copper;
    r.disposition = "corpse_coin";
    return r;
  }
};

// What --replay depends on: no store path, no writes, no crash.
void LootStoreTest::closedStoreDropsRows()
{
  LootStore s;
  QVERIFY(!s.isOpen());
  QCOMPARE(s.record({sale(1, "Bronze Dagger +1", 200)}), 0);
  QVERIFY(s.recent(10).isEmpty());
}

void LootStoreTest::recordsAndReadsBack()
{
  QTemporaryDir dir;
  LootStore s;
  QVERIFY(s.setStorePath(dir.filePath("loot.db")));
  QCOMPARE(s.record({sale(1, "Bronze Dagger +1", 200)}), 1);

  const QVector<LootRowRec> got = s.recent(10);
  QCOMPARE(got.size(), 1);
  QCOMPARE(got[0].itemName, QString("Bronze Dagger +1"));
  QCOMPARE(got[0].moneyCopper, 200u);
  QCOMPARE(got[0].itemId, 7012u);
  QCOMPARE(got[0].corpseId, 18632u);
  QCOMPARE(got[0].mobNorm, QString("goblin diviner"));
  QCOMPARE(got[0].disposition, QString("sold"));
  QVERIFY(got[0].sold);
}

void LootStoreTest::readsNewestFirst()
{
  QTemporaryDir dir;
  LootStore s;
  QVERIFY(s.setStorePath(dir.filePath("loot.db")));
  s.record({sale(1, "First", 10, 1000), sale(2, "Second", 20, 2000)});

  const QVector<LootRowRec> got = s.recent(10);
  QCOMPARE(got.size(), 2);
  QCOMPARE(got[0].itemName, QString("Second"));
  QCOMPARE(got[1].itemName, QString("First"));
}

void LootStoreTest::clampsTheLimit()
{
  QTemporaryDir dir;
  LootStore s;
  QVERIFY(s.setStorePath(dir.filePath("loot.db")));
  QVector<LootRowRec> rows;
  for (uint32_t i = 1; i <= 5; ++i)
    rows.push_back(sale(i, QString("Item%1").arg(i), i, i));
  s.record(rows);

  QCOMPARE(s.recent(2).size(), 2);
  // 0 means the default, not "none".
  QCOMPARE(s.recent(0).size(), 5);
}

// The reason the key exists: scry-cpp and scry both watch the same
// capture, so both see every event.
void LootStoreTest::dedupsAnAcquisitionBySequence()
{
  QTemporaryDir dir;
  LootStore s;
  QVERIFY(s.setStorePath(dir.filePath("loot.db")));

  QCOMPARE(s.record({sale(238, "Bronze Dagger +1", 200)}), 1);
  // Same acquisition seen by a peer recorder: different ts, same sequence.
  QCOMPARE(s.record({sale(238, "Bronze Dagger +1", 200, 9999)}), 0);
  QCOMPARE(s.recent(10).size(), 1);

  // A different acquisition still lands.
  QCOMPARE(s.record({sale(239, "Cloth Veil +1", 114)}), 1);
  QCOMPARE(s.recent(10).size(), 2);
}

void LootStoreTest::dedupsAWindowRowByCorpseAndItem()
{
  QTemporaryDir dir;
  LootStore s;
  QVERIFY(s.setStorePath(dir.filePath("loot.db")));

  QCOMPARE(s.record({window(11613, "Diamond Dust")}), 1);
  // Reopening a corpse re-sends its whole list.
  QCOMPARE(s.record({window(11613, "Diamond Dust", 5000)}), 0);
  // The same item on a different corpse is a distinct drop.
  QCOMPARE(s.record({window(11614, "Diamond Dust")}), 1);
  QCOMPARE(s.recent(10).size(), 2);
}

// A confirmation with no sequence has nothing stable to key on, so it must be
// kept rather than silently collapsed onto a peer's row.
void LootStoreTest::keepsRowsThatHaveNoStableKey()
{
  QTemporaryDir dir;
  LootStore s;
  QVERIFY(s.setStorePath(dir.filePath("loot.db")));

  QCOMPARE(s.record({sale(0, "Unkeyed", 1)}), 1);
  QCOMPARE(s.record({sale(0, "Unkeyed", 1)}), 1);
  QCOMPARE(s.recent(10).size(), 2);

  // Coin piles key on corpse+amount+ts, so a repeat of the same pile collapses
  // but two genuinely different piles do not.
  QCOMPARE(s.record({coin(100, 2881, 7000)}), 1);
  QCOMPARE(s.record({coin(100, 2881, 7000)}), 0);
  QCOMPARE(s.record({coin(100, 2881, 7001)}), 1);
}

// The existing loot.db was written by scry-web's recorder, which had no
// dedup_key column. Opening it must add the column rather than fail, and the
// pre-existing rows (NULL key) must neither collide nor be lost.
void LootStoreTest::reopensAnExistingDatabaseAndAppends()
{
  QTemporaryDir dir;
  const QString path = dir.filePath("loot.db");
  {
    LootStore s;
    QVERIFY(s.setStorePath(path));
    QCOMPARE(s.record({sale(1, "Old Row", 5)}), 1);
  }
  LootStore reopened;
  QVERIFY(reopened.setStorePath(path));
  QCOMPARE(reopened.recent(10).size(), 1);
  QCOMPARE(reopened.record({sale(2, "New Row", 6)}), 1);
  QCOMPARE(reopened.recent(10).size(), 2);
}

// --replay opens read-only, so the guard is enforced by SQLite rather than by
// remembering not to call record().
void LootStoreTest::readOnlyServesButNeverWrites()
{
  QTemporaryDir dir;
  const QString path = dir.filePath("loot.db");
  {
    LootStore w;
    QVERIFY(w.setStorePath(path));
    QCOMPARE(w.record({sale(1, "Existing", 42)}), 1);
  }

  LootStore ro;
  QVERIFY(ro.setStorePath(path, /*readOnly=*/true));
  QVERIFY(ro.isReadOnly());
  QCOMPARE(ro.recent(10).size(), 1);          // history still served
  QCOMPARE(ro.record({sale(2, "Nope", 1)}), 0);
  QCOMPARE(ro.recent(10).size(), 1);          // and nothing was added

  // Confirm on a fresh handle, not just this one's view.
  LootStore check;
  QVERIFY(check.setStorePath(path));
  QCOMPARE(check.recent(10).size(), 1);
}

// A replay on a machine that has never recorded: nothing to serve, no error.
void LootStoreTest::readOnlyOnAMissingDatabaseStaysClosed()
{
  QTemporaryDir dir;
  LootStore ro;
  QVERIFY(!ro.setStorePath(dir.filePath("absent.db"), /*readOnly=*/true));
  QVERIFY(!ro.isOpen());
  QVERIFY(ro.recent(10).isEmpty());
}

void LootStoreTest::semanticLootWritesExistingSchemaAndProjectsOnce()
{
  QTemporaryDir dir;
  LootStore store;
  QVERIFY(store.setStorePath(dir.filePath("loot.db")));
  MessageShell shell(nullptr, nullptr, nullptr, nullptr, nullptr,
                     nullptr, nullptr, nullptr);
  shell.setLootStore(&store);
  QSignalSpy acquiredSpy(&shell, &MessageShell::lootTransactionReceived);
  QSignalSpy snapshotSpy(&shell, &MessageShell::lootDropsReceived);

  seq::rust::EventCorpseLootSnapshot snapshot;
  snapshot.timestamp = 7000;
  snapshot.corpse_id = 100;
  snapshot.corpse_name = ::rust::String("an ice giant");
  snapshot.corpse_name_normalized = ::rust::String("ice giant");
  snapshot.zone_short = ::rust::String("permafrost");
  snapshot.zone_base = ::rust::String("permafrost");
  snapshot.instance = ::rust::String("solo");
  snapshot.looter = ::rust::String("Alice");
  seq::rust::EventLootItemInfo item;
  item.name = ::rust::String("Diamond Dust");
  item.icon = 1075; item.item_id = 16884;
  snapshot.items.push_back(std::move(item));
  shell.applyCorpseLootSnapshot(snapshot);
  QCOMPARE(snapshotSpy.size(), 1);

  seq::rust::EventLootAcquisition acquired;
  acquired.timestamp = 7001;
  acquired.item_name = ::rust::String("Fine Steel Sword");
  acquired.has_item_id = true; acquired.item_id = 1007;
  acquired.quantity = 2;
  acquired.corpse_name = ::rust::String("an ice giant");
  acquired.corpse_name_normalized = ::rust::String("ice giant");
  acquired.has_corpse_id = true; acquired.corpse_id = 100;
  acquired.zone_short = ::rust::String("permafrost");
  acquired.zone_base = ::rust::String("permafrost");
  acquired.instance = ::rust::String("solo");
  acquired.sold = true; acquired.coin_copper = 123;
  acquired.disposition = ::rust::String("sold");
  acquired.looter = ::rust::String("Alice");
  acquired.has_sequence = true; acquired.sequence = 44;
  acquired.complete = true;
  shell.applyLootAcquired(acquired);
  QCOMPARE(acquiredSpy.size(), 1);

  const auto rows = store.recent(10);
  QCOMPARE(rows.size(), 2);
  QCOMPARE(rows[0].source, QString("message"));
  QCOMPARE(rows[0].itemName, QString("Fine Steel Sword"));
  QCOMPARE(rows[0].qty, uint32_t(2));
  QCOMPARE(rows[0].mobNorm, QString("ice giant"));
  QCOMPARE(rows[0].instance, QString("solo"));
  QCOMPARE(rows[0].sequence, uint32_t(0));
  QCOMPARE(rows[1].source, QString("window"));
  QCOMPARE(rows[1].icon, uint32_t(1075));

  seq::rust::EventLootAcquisition abandoned;
  abandoned.timestamp = 7002;
  abandoned.item_name = ::rust::String("Destroyed Thing");
  abandoned.quantity = 1;
  abandoned.disposition = ::rust::String("destroyed");
  abandoned.complete = false;
  shell.applyLootAcquired(abandoned);
  QCOMPARE(acquiredSpy.size(), 1);
  QCOMPARE(store.recent(10).size(), 3);

  // Replayed semantic duplicates stay one row through the existing natural
  // keys. The projection is live, so callers must apply each Rust event once.
  shell.applyCorpseLootSnapshot(snapshot);
  shell.applyLootAcquired(acquired);
  QCOMPARE(store.recent(10).size(), 3);
}

QTEST_MAIN(LootStoreTest)
#include "lootstore_test.moc"
