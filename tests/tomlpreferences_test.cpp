/*
 *  tomlpreferences_test.cpp
 *  Tier-1 unit test for the TOML preference store.
 *
 *  Weighted toward the two traps this port actually hit, both of which were
 *  invisible until a tier-2 golden diverged:
 *    - toml++'s value<T>() CONVERTS, so probing bool before int turned every
 *      integer into a boolean and getPrefInt returned 1.
 *    - the legacy XML does not use value= on every element (colors use name=
 *      or red/green/blue), so a naive migration emptied them.
 */

#include <QtTest/QtTest>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include "tomlpreferences.h"

class TomlPreferencesTest : public QObject
{
  Q_OBJECT

private slots:
  void readsScalarsWithTheirRealTypes();
  void intPrefIsNotCoercedToBool();
  void precedenceRuntimeBeatsUserBeatsDefaults();
  void persistenceMaskLimitsTheSearch();
  void missingPrefReturnsCallerDefault();
  void colorRoundTripsThroughItsName();
  void namedColorIsReadable();
  void uint64MaskSurvivesRoundTrip();
  void saveThenReloadKeepsUserValues();
  void defaultsAreNeverWritten();
  void migratesLegacyXmlOnce();
  void malformedTomlKeepsCallerDefaults();

private:
  static void write(const QString& path, const QString& text)
  {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream(&f) << text;
  }
};

// A defaults file exercising each supported scalar.
static const char* kDefaults = R"(
[Defaults]
DefaultDeity = 140
DefaultName = "You"
useAutoDetectedSettings = true

[Interface]
SpawnColor = "#00008b"
NamedColor = "gray"
)";

void TomlPreferencesTest::readsScalarsWithTheirRealTypes()
{
  QTemporaryDir dir;
  const QString def = dir.filePath("def.toml");
  write(def, kDefaults);
  TomlPreferences p(def, dir.filePath("user.toml"));

  QCOMPARE(p.getPrefInt("DefaultDeity", "Defaults", -1), 140);
  QCOMPARE(p.getPrefString("DefaultName", "Defaults", ""), QString("You"));
  QCOMPARE(p.getPrefBool("useAutoDetectedSettings", "Defaults", false), true);
}

// The regression that shipped and was caught by a golden: 140 came back as 1.
void TomlPreferencesTest::intPrefIsNotCoercedToBool()
{
  QTemporaryDir dir;
  const QString def = dir.filePath("def.toml");
  write(def, kDefaults);
  TomlPreferences p(def, dir.filePath("user.toml"));

  QCOMPARE(p.getPrefInt("DefaultDeity", "Defaults", -1), 140);
  // And the converse: a real bool must not read as an arbitrary int.
  QCOMPARE(p.getPrefInt("useAutoDetectedSettings", "Defaults", -1), 1);
  QCOMPARE(p.getPrefBool("DefaultDeity", "Defaults", false), true); // 140 != 0
}

void TomlPreferencesTest::precedenceRuntimeBeatsUserBeatsDefaults()
{
  QTemporaryDir dir;
  const QString def = dir.filePath("def.toml");
  write(def, kDefaults);
  TomlPreferences p(def, dir.filePath("user.toml"));

  QCOMPARE(p.getPrefInt("DefaultDeity", "Defaults", -1), 140); // defaults
  p.setPrefInt("DefaultDeity", "Defaults", 201, TomlPreferences::User);
  QCOMPARE(p.getPrefInt("DefaultDeity", "Defaults", -1), 201); // user wins
  p.setPrefInt("DefaultDeity", "Defaults", 303, TomlPreferences::Runtime);
  QCOMPARE(p.getPrefInt("DefaultDeity", "Defaults", -1), 303); // runtime wins
}

void TomlPreferencesTest::persistenceMaskLimitsTheSearch()
{
  QTemporaryDir dir;
  const QString def = dir.filePath("def.toml");
  write(def, kDefaults);
  TomlPreferences p(def, dir.filePath("user.toml"));
  p.setPrefInt("DefaultDeity", "Defaults", 201, TomlPreferences::User);

  // Asking only for Defaults must not see the user's override.
  QCOMPARE(p.getPrefInt("DefaultDeity", "Defaults", -1, TomlPreferences::Defaults), 140);
  QCOMPARE(p.getPrefInt("DefaultDeity", "Defaults", -1, TomlPreferences::User), 201);
  // A pool with no such value falls through to the caller's default.
  QCOMPARE(p.getPrefInt("DefaultDeity", "Defaults", -7, TomlPreferences::Runtime), -7);
}

void TomlPreferencesTest::missingPrefReturnsCallerDefault()
{
  QTemporaryDir dir;
  const QString def = dir.filePath("def.toml");
  write(def, kDefaults);
  TomlPreferences p(def, dir.filePath("user.toml"));

  QCOMPARE(p.getPrefInt("NoSuchKey", "Defaults", 42), 42);
  QCOMPARE(p.getPrefString("NoSuchKey", "NoSuchSection", "fallback"), QString("fallback"));
  QVERIFY(!p.isPreference("NoSuchKey", "Defaults"));
  QVERIFY(p.isPreference("DefaultDeity", "Defaults"));
  QVERIFY(p.isSection("Defaults"));
  QVERIFY(!p.isSection("NoSuchSection"));
}

void TomlPreferencesTest::colorRoundTripsThroughItsName()
{
  QTemporaryDir dir;
  const QString def = dir.filePath("def.toml");
  write(def, kDefaults);
  TomlPreferences p(def, dir.filePath("user.toml"));

  const SeqColor read = p.getPrefColor("SpawnColor", "Interface");
  QVERIFY(read.isValid());
  QCOMPARE(read.name(), QString("#00008b"));

  const SeqColor magenta(255, 0, 255);
  p.setPrefColor("SpawnColor", "Interface", magenta);
  QCOMPARE(p.getPrefColor("SpawnColor", "Interface").name(), magenta.name());
}

void TomlPreferencesTest::namedColorIsReadable()
{
  QTemporaryDir dir;
  const QString def = dir.filePath("def.toml");
  write(def, kDefaults);
  TomlPreferences p(def, dir.filePath("user.toml"));

  // The legacy XML stored many colors by NAME, so they must still parse.
  QVERIFY(p.getPrefColor("NamedColor", "Interface").isValid());
  // An unparseable color yields the caller's default, never a silent black.
  const SeqColor red(255, 0, 0);
  QCOMPARE(p.getPrefColor("DefaultName", "Defaults", red).name(), red.name());
}

void TomlPreferencesTest::uint64MaskSurvivesRoundTrip()
{
  QTemporaryDir dir;
  const QString def = dir.filePath("def.toml");
  const QString usr = dir.filePath("user.toml");
  write(def, kDefaults);

  // MessageFilter's type mask is the only uint64 preference.
  const uint64_t mask = (1ULL << 40) | (1ULL << 3) | 1ULL;
  {
    TomlPreferences p(def, usr);
    p.setPrefUInt64("Types", "Filters", mask);
    QVERIFY(p.save());
  }
  TomlPreferences reloaded(def, usr);
  QCOMPARE(reloaded.getPrefUInt64("Types", "Filters", 0), mask);
}

void TomlPreferencesTest::saveThenReloadKeepsUserValues()
{
  QTemporaryDir dir;
  const QString def = dir.filePath("def.toml");
  const QString usr = dir.filePath("user.toml");
  write(def, kDefaults);
  {
    TomlPreferences p(def, usr);
    p.setPrefString("Package", "Maps", "brewall");
    p.setPrefInt("Answer", "Maps", 42);
    p.setPrefBool("Enabled", "Maps", true);
    QVERIFY(p.save());
  }
  QVERIFY(QFile::exists(usr));

  TomlPreferences reloaded(def, usr);
  QCOMPARE(reloaded.getPrefString("Package", "Maps", ""), QString("brewall"));
  QCOMPARE(reloaded.getPrefInt("Answer", "Maps", -1), 42);
  QCOMPARE(reloaded.getPrefBool("Enabled", "Maps", false), true);
  // Defaults still resolve after a reload.
  QCOMPARE(reloaded.getPrefInt("DefaultDeity", "Defaults", -1), 140);
}

void TomlPreferencesTest::defaultsAreNeverWritten()
{
  QTemporaryDir dir;
  const QString def = dir.filePath("def.toml");
  const QString usr = dir.filePath("user.toml");
  write(def, kDefaults);
  const QByteArray before = [&] {
    QFile f(def); f.open(QIODevice::ReadOnly); return f.readAll();
  }();

  TomlPreferences p(def, usr);
  p.setPrefInt("DefaultDeity", "Defaults", 999, TomlPreferences::User);
  QVERIFY(p.save());

  QFile f(def);
  QVERIFY(f.open(QIODevice::ReadOnly));
  QCOMPARE(f.readAll(), before); // the shipped defaults file is read-only
}

void TomlPreferencesTest::migratesLegacyXmlOnce()
{
  QTemporaryDir dir;
  const QString def = dir.filePath("def.toml");
  const QString usr = dir.filePath("showeq-daemon.toml");
  const QString xml = dir.filePath("showeq-daemon.xml");
  write(def, kDefaults);
  write(xml,
        "<?xml version='1.0' encoding='UTF-8'?>\n"
        "<seqpreferences version=\"1.0\">\n"
        "  <section name=\"Maps\">\n"
        "    <property name=\"Package\"><string value=\"brewall\"/></property>\n"
        "    <property name=\"Zoom\"><int value=\"3\"/></property>\n"
        "    <property name=\"Grid\"><bool value=\"true\"/></property>\n"
        "  </section>\n"
        "</seqpreferences>\n");

  TomlPreferences p(def, usr);
  QCOMPARE(p.getPrefString("Package", "Maps", ""), QString("brewall"));
  QCOMPARE(p.getPrefInt("Zoom", "Maps", -1), 3);
  QCOMPARE(p.getPrefBool("Grid", "Maps", false), true);
  // Migration writes the TOML immediately, so the next start reads TOML.
  QVERIFY(QFile::exists(usr));

  TomlPreferences again(def, usr);
  QCOMPARE(again.getPrefString("Package", "Maps", ""), QString("brewall"));
}

void TomlPreferencesTest::malformedTomlKeepsCallerDefaults()
{
  QTemporaryDir dir;
  const QString def = dir.filePath("def.toml");
  write(def, "[Defaults]\nthis is = = not toml\n");

  // A broken defaults file must not take the daemon down; callers just get
  // their compiled-in defaults (and the constructor warns).
  TomlPreferences p(def, dir.filePath("user.toml"));
  QCOMPARE(p.getPrefInt("DefaultDeity", "Defaults", 396), 396);
}

QTEST_MAIN(TomlPreferencesTest)
#include "tomlpreferences_test.moc"
