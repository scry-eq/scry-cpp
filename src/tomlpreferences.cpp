#include "tomlpreferences.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>
#include <QXmlStreamReader>

#include "diagnosticmessages.h"
#include "toml.hpp"

// A uint64 preference is stored as a TOML integer, which is SIGNED 64-bit. The
// only uint64 preference is MessageFilter's type mask (1 << MT_*), so this
// holds as long as the mask stays inside 63 bits. If a 64th message type is
// ever added, this fails the build instead of silently truncating the mask.
#include "message.h"
static_assert(MT_Max < 63,
              "message-type mask no longer fits a signed TOML integer — "
              "store MessageFilter types as a string before adding MT_Max 63");

QString TomlPreferences::Value::toString() const
{
  switch (kind) {
    case Str:  return s;
    case Int:  return QString::number(i);
    case UInt: return QString::number(u);
    case Bool: return b ? QStringLiteral("true") : QStringLiteral("false");
  }
  return QString();
}

TomlPreferences::TomlPreferences(const QString& defaultsFileName,
                                 const QString& userFileName)
  : m_defaultsFile(defaultsFileName), m_userFile(userFileName)
{
  QString err;
  if (!m_defaultsFile.isEmpty() && !loadToml(m_defaultsFile, m_defaults, &err))
  {
    // Not fatal: every getPref call carries its own default, so the daemon
    // still runs — but silently running on compiled-in defaults is exactly
    // the kind of thing that gets misdiagnosed later, so say it loudly.
    seqWarn("preferences: no defaults loaded from '%s' (%s) — "
            "callers fall back to their compiled-in defaults",
            qUtf8Printable(m_defaultsFile), qUtf8Printable(err));
  }
  loadUserWithMigration();
}

const TomlPreferences::Pool* TomlPreferences::poolFor(Persistence pers) const
{
  if (pers & Runtime)  return &m_runtime;
  if (pers & User)     return &m_user;
  if (pers & Defaults) return &m_defaults;
  return nullptr;
}

TomlPreferences::Pool* TomlPreferences::poolFor(Persistence pers)
{
  if (pers & Runtime)  return &m_runtime;
  if (pers & User)     return &m_user;
  if (pers & Defaults) return &m_defaults;
  return nullptr;
}

// Runtime beats User beats Defaults, each consulted only if its bit is set.
const TomlPreferences::Value* TomlPreferences::find(const QString& name,
                                                    const QString& section,
                                                    Persistence pers) const
{
  const Pool* pools[3] = {nullptr, nullptr, nullptr};
  int n = 0;
  if (pers & Runtime)  pools[n++] = &m_runtime;
  if (pers & User)     pools[n++] = &m_user;
  if (pers & Defaults) pools[n++] = &m_defaults;

  for (int k = 0; k < n; k++)
  {
    auto sect = pools[k]->constFind(section);
    if (sect == pools[k]->constEnd()) continue;
    auto val = sect->constFind(name);
    if (val != sect->constEnd()) return &val.value();
  }
  return nullptr;
}

void TomlPreferences::put(const QString& name, const QString& section,
                          const Value& v, Persistence pers)
{
  Pool* pool = poolFor(pers);
  if (!pool) return;
  (*pool)[section][name] = v;
  if (pool == &m_user) m_userDirty = true;
}

bool TomlPreferences::isPreference(const QString& name, const QString& section,
                                   Persistence pers) const
{
  return find(name, section, pers) != nullptr;
}

bool TomlPreferences::isSection(const QString& section, Persistence pers) const
{
  const Pool* pools[3] = {&m_runtime, &m_user, &m_defaults};
  const int bits[3] = {Runtime, User, Defaults};
  for (int k = 0; k < 3; k++)
    if ((pers & bits[k]) && pools[k]->contains(section)) return true;
  return false;
}

QString TomlPreferences::getPrefString(const QString& name, const QString& section,
                                       const QString& def, Persistence pers) const
{
  const Value* v = find(name, section, pers);
  // Any type renders as a string, matching the old QVariant-backed store —
  // reading an int pref as a string was legal there and still is.
  return v ? v->toString() : def;
}

int TomlPreferences::getPrefInt(const QString& name, const QString& section,
                                int def, Persistence pers) const
{
  const Value* v = find(name, section, pers);
  if (!v) return def;
  switch (v->kind) {
    case Value::Int:  return int(v->i);
    case Value::UInt: return int(v->u);
    case Value::Bool: return v->b ? 1 : 0;
    case Value::Str: {
      bool ok = false;
      const int n = v->s.toInt(&ok, 0);
      return ok ? n : def;
    }
  }
  return def;
}

uint64_t TomlPreferences::getPrefUInt64(const QString& name, const QString& section,
                                        uint64_t def, Persistence pers) const
{
  const Value* v = find(name, section, pers);
  if (!v) return def;
  switch (v->kind) {
    case Value::UInt: return v->u;
    case Value::Int:  return uint64_t(v->i);
    case Value::Bool: return v->b ? 1 : 0;
    case Value::Str: {
      bool ok = false;
      const uint64_t n = v->s.toULongLong(&ok, 0);
      return ok ? n : def;
    }
  }
  return def;
}

bool TomlPreferences::getPrefBool(const QString& name, const QString& section,
                                  bool def, Persistence pers) const
{
  const Value* v = find(name, section, pers);
  if (!v) return def;
  switch (v->kind) {
    case Value::Bool: return v->b;
    case Value::Int:  return v->i != 0;
    case Value::UInt: return v->u != 0;
    case Value::Str: {
      const QString s = v->s.trimmed().toLower();
      if (s == QLatin1String("true") || s == QLatin1String("1")) return true;
      if (s == QLatin1String("false") || s == QLatin1String("0")) return false;
      return def;
    }
  }
  return def;
}

SeqColor TomlPreferences::getPrefColor(const QString& name, const QString& section,
                                       const SeqColor& def, Persistence pers) const
{
  const Value* v = find(name, section, pers);
  if (!v) return def;
  const SeqColor c(v->toString());
  return c.isValid() ? c : def;
}

void TomlPreferences::setPrefString(const QString& name, const QString& section,
                                    const QString& value, Persistence pers)
{
  Value v; v.kind = Value::Str; v.s = value;
  put(name, section, v, pers);
}

void TomlPreferences::setPrefInt(const QString& name, const QString& section,
                                 int value, Persistence pers)
{
  Value v; v.kind = Value::Int; v.i = value;
  put(name, section, v, pers);
}

void TomlPreferences::setPrefUInt64(const QString& name, const QString& section,
                                    uint64_t value, Persistence pers)
{
  Value v; v.kind = Value::UInt; v.u = value;
  put(name, section, v, pers);
}

void TomlPreferences::setPrefBool(const QString& name, const QString& section,
                                  bool value, Persistence pers)
{
  Value v; v.kind = Value::Bool; v.b = value;
  put(name, section, v, pers);
}

void TomlPreferences::setPrefColor(const QString& name, const QString& section,
                                   const SeqColor& value, Persistence pers)
{
  // SeqColor round-trips through its own name(), so no encoding is invented.
  Value v; v.kind = Value::Str; v.s = value.name();
  put(name, section, v, pers);
}

bool TomlPreferences::loadToml(const QString& path, Pool& into, QString* error) const
{
  if (!QFile::exists(path))
  {
    if (error) *error = QStringLiteral("file not found");
    return false;
  }
  toml::table doc;
  try {
    doc = toml::parse_file(path.toStdString());
  } catch (const toml::parse_error& e) {
    // Keep whatever was already loaded rather than half-applying a bad file.
    if (error) *error = QString::fromStdString(std::string(e.description()));
    return false;
  }

  for (auto&& [sectionKey, sectionNode] : doc)
  {
    const toml::table* sect = sectionNode.as_table();
    if (!sect) continue; // a top-level scalar has no section; skip it
    const QString section = QString::fromStdString(std::string(sectionKey.str()));
    for (auto&& [key, node] : *sect)
    {
      // Test the node's ACTUAL type. `value<T>()` CONVERTS — value<bool>() on
      // an integer node returns true, so probing bool first silently turned
      // every int into a bool and getPrefInt then returned 1. The tier-2
      // goldens caught it via DefaultDeity (140 read back as 1); level, race
      // and class are all 1 and hid it.
      Value v;
      if (node.is_string()) {
        v.kind = Value::Str;
        v.s = QString::fromStdString(node.as_string()->get());
      } else if (node.is_boolean()) {
        v.kind = Value::Bool;
        v.b = node.as_boolean()->get();
      } else if (node.is_integer()) {
        v.kind = Value::Int;
        v.i = node.as_integer()->get();
      } else {
        continue; // floats/arrays/tables/dates: no accessor reads them
      }
      into[section][QString::fromStdString(std::string(key.str()))] = v;
    }
  }
  return true;
}

// The user file is the only one that migrates: defaults ship as TOML already.
bool TomlPreferences::loadUserWithMigration()
{
  if (m_userFile.isEmpty()) return false;

  QString err;
  if (loadToml(m_userFile, m_user, &err)) return true;

  // No TOML yet. If the old XML is beside it, import once and write TOML, so
  // an upgrade keeps the user's settings instead of silently resetting them.
  QString xmlPath = m_userFile;
  if (xmlPath.endsWith(QLatin1String(".toml"), Qt::CaseInsensitive))
    xmlPath.chop(5);
  xmlPath += QLatin1String(".xml");

  if (QFile::exists(xmlPath) && importXml(xmlPath, m_user))
  {
    seqInfo("preferences: migrated '%s' -> '%s'",
            qUtf8Printable(xmlPath), qUtf8Printable(m_userFile));
    m_userDirty = true;
    save();
    return true;
  }
  return false; // first run: no user prefs yet, which is normal
}

// Minimal reader for the legacy <section><property><type value=""/> form.
// Only needs to handle what the daemon itself wrote.
bool TomlPreferences::importXml(const QString& path, Pool& into) const
{
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;

  QXmlStreamReader xml(&f);
  QString section, property;
  int imported = 0;

  while (!xml.atEnd())
  {
    xml.readNext();
    if (xml.isStartElement())
    {
      const QStringView name = xml.name();
      if (name == QLatin1String("section"))
        section = xml.attributes().value(QLatin1String("name")).toString();
      else if (name == QLatin1String("property"))
        property = xml.attributes().value(QLatin1String("name")).toString();
      else if (!section.isEmpty() && !property.isEmpty() &&
               xml.attributes().hasAttribute(QLatin1String("value")))
      {
        const QString raw =
            xml.attributes().value(QLatin1String("value")).toString();
        Value v;
        if (name == QLatin1String("bool")) {
          v.kind = Value::Bool;
          v.b = (raw.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0);
        } else if (name == QLatin1String("int")) {
          v.kind = Value::Int;
          v.i = raw.toLongLong();
        } else {
          v.kind = Value::Str; // string / color / font / key
          v.s = raw;
        }
        into[section][property] = v;
        imported++;
      }
    }
    else if (xml.isEndElement())
    {
      if (xml.name() == QLatin1String("property")) property.clear();
      else if (xml.name() == QLatin1String("section")) section.clear();
    }
  }
  if (xml.hasError())
  {
    seqWarn("preferences: '%s' is not readable XML (%s) — not migrated",
            qUtf8Printable(path), qUtf8Printable(xml.errorString()));
    return false;
  }
  return imported > 0;
}

bool TomlPreferences::save()
{
  if (!m_userDirty || m_userFile.isEmpty()) return true;

  toml::table doc;
  // Sorted so the file is stable across runs — a preferences file that
  // reorders itself on every save is unreadable in a diff.
  QList<QString> sections = m_user.keys();
  std::sort(sections.begin(), sections.end());
  for (const QString& section : sections)
  {
    toml::table sect;
    QList<QString> keys = m_user[section].keys();
    std::sort(keys.begin(), keys.end());
    for (const QString& key : keys)
    {
      const Value& v = m_user[section][key];
      const std::string k = key.toStdString();
      switch (v.kind) {
        case Value::Str:  sect.insert_or_assign(k, v.s.toStdString()); break;
        case Value::Int:  sect.insert_or_assign(k, v.i); break;
        case Value::UInt: sect.insert_or_assign(k, int64_t(v.u)); break;
        case Value::Bool: sect.insert_or_assign(k, v.b); break;
      }
    }
    doc.insert_or_assign(section.toStdString(), std::move(sect));
  }

  // QSaveFile is temp-write + atomic rename, so an interrupted save leaves the
  // previous file intact rather than a truncated one.
  QSaveFile out(m_userFile);
  if (!out.open(QIODevice::WriteOnly | QIODevice::Text))
  {
    seqWarn("preferences: cannot write '%s'", qUtf8Printable(m_userFile));
    return false;
  }
  std::ostringstream ss;
  ss << "# showeq-daemon preferences — written by the daemon.\n\n" << doc << "\n";
  const std::string text = ss.str();
  out.write(text.data(), qint64(text.size()));
  if (!out.commit())
  {
    seqWarn("preferences: failed to commit '%s'", qUtf8Printable(m_userFile));
    return false;
  }
  m_userDirty = false;
  return true;
}

void TomlPreferences::revert()
{
  m_user.clear();
  m_defaults.clear();
  m_userDirty = false;
  QString err;
  loadToml(m_defaultsFile, m_defaults, &err);
  loadUserWithMigration();
}
