#include "lootstore.h"

#include <sqlite3.h>

#include <QDebug>

namespace {

// The table showeq-web's recorder created, verbatim, so an existing loot.db is
// appended to rather than migrated. `dedup_key` is ours: a natural key so a
// second recorder watching the same capture is a no-op instead of a duplicate.
// It is added to old tables too — SQLite backfills NULL, and NULLs do not
// collide in a UNIQUE index, so pre-existing rows neither collide nor dedup.
const char* kSchema = R"sql(
CREATE TABLE IF NOT EXISTS loot (
  id           INTEGER PRIMARY KEY,
  ts           INTEGER NOT NULL,
  source       TEXT    NOT NULL,
  item_name    TEXT    NOT NULL,
  item_id      INTEGER,
  icon         INTEGER,
  qty          INTEGER NOT NULL DEFAULT 1,
  mob_name     TEXT,
  mob_norm     TEXT,
  corpse_id    INTEGER,
  zone_short   TEXT,
  zone_base    TEXT,
  instance     TEXT,
  sold         INTEGER NOT NULL DEFAULT 0,
  money_copper INTEGER NOT NULL DEFAULT 0,
  disposition  TEXT,
  looter       TEXT
);
CREATE INDEX IF NOT EXISTS loot_item ON loot(item_name);
CREATE INDEX IF NOT EXISTS loot_mob  ON loot(mob_norm);
CREATE INDEX IF NOT EXISTS loot_zone ON loot(zone_base);
)sql";

const char* kInsert =
    "INSERT OR IGNORE INTO loot (ts, source, item_name, item_id, icon, qty, "
    "mob_name, mob_norm, corpse_id, zone_short, zone_base, instance, sold, "
    "money_copper, disposition, looter, dedup_key) "
    "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)";

const char* kSelect =
    "SELECT ts, source, item_name, item_id, icon, qty, mob_name, mob_norm, "
    "corpse_id, zone_short, zone_base, instance, sold, money_copper, "
    "disposition, looter FROM loot ORDER BY ts DESC, id DESC LIMIT ?";

// NULL for a row with nothing stable to key on, so it is always inserted.
// An acquisition is identified by its monotonic request sequence; a corpse pile
// by corpse+amount+ts; a window row by corpse+item, which is what the corpse
// re-sends when reopened.
QString dedupKey(const LootRowRec& r)
{
    if (r.source == QLatin1String("message"))
        return r.sequence ? QStringLiteral("m|%1").arg(r.sequence) : QString();
    if (r.source == QLatin1String("coin"))
        return QStringLiteral("c|%1|%2|%3").arg(r.corpseId).arg(r.moneyCopper).arg(r.ts);
    if (r.source == QLatin1String("window"))
        return QStringLiteral("w|%1|%2").arg(r.corpseId).arg(r.itemName);
    return QString();
}

void bindText(sqlite3_stmt* st, int i, const QString& s)
{
    const QByteArray b = s.toUtf8();
    sqlite3_bind_text(st, i, b.constData(), b.size(), SQLITE_TRANSIENT);
}

// 0 means "unknown" on the wire; the column is nullable, so keep NULL rather
// than storing a fake id.
void bindIdOrNull(sqlite3_stmt* st, int i, uint32_t v)
{
    if (v)
        sqlite3_bind_int64(st, i, static_cast<sqlite3_int64>(v));
    else
        sqlite3_bind_null(st, i);
}

QString columnText(sqlite3_stmt* st, int i)
{
    const unsigned char* p = sqlite3_column_text(st, i);
    return p ? QString::fromUtf8(reinterpret_cast<const char*>(p)) : QString();
}

} // namespace

LootStore::~LootStore()
{
    if (m_db)
        sqlite3_close(m_db);
}

bool LootStore::setStorePath(const QString& path)
{
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
    const QByteArray p = path.toUtf8();
    if (sqlite3_open(p.constData(), &m_db) != SQLITE_OK) {
        qWarning("LootStore: cannot open %s: %s", p.constData(),
                 m_db ? sqlite3_errmsg(m_db) : "out of memory");
        sqlite3_close(m_db);
        m_db = nullptr;
        return false;
    }
    // WAL so the reader side never blocks the writer, and vice versa — a second
    // recorder or an external sqlite3 session can read while we append.
    sqlite3_exec(m_db, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);

    char* err = nullptr;
    if (sqlite3_exec(m_db, kSchema, nullptr, nullptr, &err) != SQLITE_OK) {
        qWarning("LootStore: schema failed: %s", err ? err : "?");
        sqlite3_free(err);
        sqlite3_close(m_db);
        m_db = nullptr;
        return false;
    }
    // Additive on an existing table; the error when it is already there is
    // expected and ignored.
    sqlite3_exec(m_db, "ALTER TABLE loot ADD COLUMN dedup_key TEXT", nullptr,
                 nullptr, nullptr);
    sqlite3_exec(m_db,
                 "CREATE UNIQUE INDEX IF NOT EXISTS loot_dedup ON loot(dedup_key)",
                 nullptr, nullptr, nullptr);

    m_path = path;
    qInfo("LootStore: recording to %s", p.constData());
    return true;
}

int LootStore::record(const QVector<LootRowRec>& rows)
{
    if (!m_db || rows.isEmpty())
        return 0;

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(m_db, kInsert, -1, &st, nullptr) != SQLITE_OK) {
        qWarning("LootStore: prepare failed: %s", sqlite3_errmsg(m_db));
        return 0;
    }
    sqlite3_exec(m_db, "BEGIN", nullptr, nullptr, nullptr);
    int written = 0;
    for (const LootRowRec& r : rows) {
        sqlite3_bind_int64(st, 1, r.ts);
        bindText(st, 2, r.source);
        bindText(st, 3, r.itemName);
        bindIdOrNull(st, 4, r.itemId);
        bindIdOrNull(st, 5, r.icon);
        sqlite3_bind_int(st, 6, static_cast<int>(r.qty));
        bindText(st, 7, r.mobName);
        bindText(st, 8, r.mobNorm);
        bindIdOrNull(st, 9, r.corpseId);
        bindText(st, 10, r.zoneShort);
        bindText(st, 11, r.zoneBase);
        bindText(st, 12, r.instance);
        sqlite3_bind_int(st, 13, r.sold ? 1 : 0);
        sqlite3_bind_int64(st, 14, static_cast<sqlite3_int64>(r.moneyCopper));
        bindText(st, 15, r.disposition);
        bindText(st, 16, r.looter);
        const QString key = dedupKey(r);
        if (key.isEmpty())
            sqlite3_bind_null(st, 17);
        else
            bindText(st, 17, key);

        if (sqlite3_step(st) != SQLITE_DONE)
            qWarning("LootStore: insert failed: %s", sqlite3_errmsg(m_db));
        else
            written += sqlite3_changes(m_db);
        sqlite3_reset(st);
        sqlite3_clear_bindings(st);
    }
    sqlite3_exec(m_db, "COMMIT", nullptr, nullptr, nullptr);
    sqlite3_finalize(st);
    return written;
}

QVector<LootRowRec> LootStore::recent(uint32_t limit) const
{
    QVector<LootRowRec> out;
    if (!m_db)
        return out;
    if (limit == 0)
        limit = kDefaultLimit;
    if (limit > kMaxLimit)
        limit = kMaxLimit;

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(m_db, kSelect, -1, &st, nullptr) != SQLITE_OK) {
        qWarning("LootStore: query failed: %s", sqlite3_errmsg(m_db));
        return out;
    }
    sqlite3_bind_int(st, 1, static_cast<int>(limit));
    while (sqlite3_step(st) == SQLITE_ROW) {
        LootRowRec r;
        r.ts = sqlite3_column_int64(st, 0);
        r.source = columnText(st, 1);
        r.itemName = columnText(st, 2);
        r.itemId = static_cast<uint32_t>(sqlite3_column_int64(st, 3));
        r.icon = static_cast<uint32_t>(sqlite3_column_int64(st, 4));
        r.qty = static_cast<uint32_t>(sqlite3_column_int(st, 5));
        r.mobName = columnText(st, 6);
        r.mobNorm = columnText(st, 7);
        r.corpseId = static_cast<uint32_t>(sqlite3_column_int64(st, 8));
        r.zoneShort = columnText(st, 9);
        r.zoneBase = columnText(st, 10);
        r.instance = columnText(st, 11);
        r.sold = sqlite3_column_int(st, 12) != 0;
        r.moneyCopper = static_cast<uint32_t>(sqlite3_column_int64(st, 13));
        r.disposition = columnText(st, 14);
        r.looter = columnText(st, 15);
        out.push_back(r);
    }
    sqlite3_finalize(st);
    return out;
}
