#ifndef LOOTSTORE_H
#define LOOTSTORE_H

#include <QString>
#include <QVector>
#include <cstdint>

struct sqlite3;

// One recorded loot row. Mirrors seq.v1.LootRecord and the `loot` table; 0
// stands in for SQL NULL on the id/icon columns.
struct LootRowRec {
    int64_t ts = 0;
    QString source;        // "message" | "window" | "coin"
    QString itemName;
    uint32_t itemId = 0;
    uint32_t icon = 0;
    uint32_t qty = 1;
    QString mobName;
    QString mobNorm;
    uint32_t corpseId = 0;
    QString zoneShort;
    QString zoneBase;
    QString instance;
    bool sold = false;
    uint32_t moneyCopper = 0;
    QString disposition;
    QString looter;
    uint32_t sequence = 0;
};

// Durable loot history, one SQLite file per backend data namespace. Schema is
// the one showeq-web's recorder created, so an existing loot.db is opened and
// appended to rather than migrated.
//
// Writes are INSERT OR IGNORE against a natural key: showeq-daemon and scry can
// watch the same capture at once, and both will see every loot event. The key
// is the confirmation's monotonic request sequence for acquisitions, and
// corpse+item / corpse+ts for the rows that have none.
//
// Persistence is opt-in via setStorePath(); DaemonApp withholds it under
// --replay so a regression run cannot write fixture loot into the real DB.
class LootStore {
public:
    ~LootStore();

    // Open (creating if needed) and apply the schema. Without this the store
    // accepts rows and drops them, which is what replay wants.
    bool setStorePath(const QString& path);
    bool isOpen() const { return m_db != nullptr; }
    const QString& path() const { return m_path; }

    // Append rows. No-op when closed. Returns rows actually inserted —
    // duplicates a peer already recorded count as 0.
    int record(const QVector<LootRowRec>& rows);

    // Newest first. `limit` is clamped to [1, kMaxLimit]; 0 means kDefaultLimit.
    QVector<LootRowRec> recent(uint32_t limit) const;

    static constexpr uint32_t kDefaultLimit = 5000;
    static constexpr uint32_t kMaxLimit = 50000;

private:
    sqlite3* m_db = nullptr;
    QString m_path;
};

#endif // LOOTSTORE_H
