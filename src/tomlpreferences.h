/*
 * tomlpreferences.h — the daemon's preference store.
 *
 * Replaces the XML store (xmlpreferences.{h,cpp}, 1104 lines) with TOML,
 * matching the move the opcode tables already made. Same three-pool model and
 * the same lookup precedence, so callers are unchanged; only the on-disk
 * format and the type surface differ.
 *
 * TYPE SURFACE. The XML store exposed 14 typed accessors. The daemon only ever
 * called five value types — string, int, uint64, bool, color — so that is what
 * this exposes. QPoint/QRect/QSize/QStringList/QVariant/double had ZERO callers
 * and are deliberately not carried over: they were the widget-era API of a
 * headless daemon, and each would have needed an invented TOML encoding to
 * serve nobody. Add one back only with a caller in the same commit.
 *
 * ON-DISK FORMAT. Section per table, preference per key, native TOML scalars:
 *
 *     [Network]
 *     Device = "eth0"
 *     RealTimeThread = false
 *     ArqSeqGiveUp = 512
 *
 * Colors are their SeqColor::name() string ("#rrggbb"), which round-trips
 * through SeqColor's string constructor — no invented encoding.
 */

#ifndef TOMLPREFERENCES_H
#define TOMLPREFERENCES_H

#include <cstdint>

#include <QHash>
#include <QString>

#include "seqcolor.h"

class TomlPreferences
{
 public:
  // Which pool a value is read from / written to. A get with Any walks
  // Runtime -> User -> Defaults and returns the first hit, so a runtime
  // override beats a user setting, which beats a shipped default.
  enum Persistence
  {
    Runtime  = 0x01, // not persisted (command-line overrides, etc.)
    User     = 0x02, // the user's own preference file
    Defaults = 0x04, // the shipped defaults file, never written
    Any      = Runtime | User | Defaults,
    All      = Any
  };

  // `defaultsFileName` is read-only; `userFileName` is what save() writes.
  // If the user file is absent but a same-named .xml sits beside it, that XML
  // is imported once — see loadUserWithMigration().
  TomlPreferences(const QString& defaultsFileName, const QString& userFileName);

  bool isPreference(const QString& name, const QString& section,
                    Persistence pers = Any) const;
  bool isSection(const QString& section, Persistence pers = Any) const;

  QString  getPrefString(const QString& name, const QString& section,
                         const QString& def = QString(), Persistence pers = Any) const;
  int      getPrefInt(const QString& name, const QString& section,
                      int def = -1, Persistence pers = Any) const;
  uint64_t getPrefUInt64(const QString& name, const QString& section,
                         uint64_t def = 0, Persistence pers = Any) const;
  bool     getPrefBool(const QString& name, const QString& section,
                       bool def = false, Persistence pers = Any) const;
  SeqColor getPrefColor(const QString& name, const QString& section,
                        const SeqColor& def = SeqColor(), Persistence pers = Any) const;

  void setPrefString(const QString& name, const QString& section,
                     const QString& value, Persistence pers = User);
  void setPrefInt(const QString& name, const QString& section,
                  int value, Persistence pers = User);
  void setPrefUInt64(const QString& name, const QString& section,
                     uint64_t value, Persistence pers = User);
  void setPrefBool(const QString& name, const QString& section,
                   bool value, Persistence pers = User);
  void setPrefColor(const QString& name, const QString& section,
                    const SeqColor& value, Persistence pers = User);

  // Writes the User pool if anything changed. Atomic (temp + rename): a
  // crash mid-write must not leave a truncated preferences file.
  bool save();
  // Re-reads both files, discarding unsaved User changes. Runtime survives,
  // since it holds command-line overrides that outlive a reload.
  void revert();

  const QString& userFileName() const { return m_userFile; }

 private:
  // One value, tagged. Small on purpose — five types, no QVariant.
  struct Value
  {
    enum Kind { Str, Int, UInt, Bool } kind = Str;
    QString  s;
    int64_t  i = 0;
    uint64_t u = 0;
    bool     b = false;

    QString toString() const;
  };

  using Section = QHash<QString, Value>;
  using Pool    = QHash<QString, Section>;

  const Value* find(const QString& name, const QString& section,
                    Persistence pers) const;
  void put(const QString& name, const QString& section, const Value& v,
           Persistence pers);
  Pool* poolFor(Persistence pers);
  const Pool* poolFor(Persistence pers) const;

  bool loadToml(const QString& path, Pool& into, QString* error) const;
  bool loadUserWithMigration();
  bool importXml(const QString& path, Pool& into) const;

  QString m_defaultsFile;
  QString m_userFile;
  Pool    m_runtime;
  Pool    m_user;
  Pool    m_defaults;
  bool    m_userDirty = false;
};

#endif // TOMLPREFERENCES_H
