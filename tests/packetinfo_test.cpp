/*
 *  packetinfo_test.cpp
 *  Tier-1 unit test for EQPacketOPCodeDB::load — the opcode-XML parser.
 *
 *  Pinned by this test: the in-memory state produced by parsing a valid
 *  opcode-XML file. Used to validate the QXmlSimpleReader → QXmlStreamReader
 *  migration in MODERNIZATION_PLAN.md (Qt5Compat retirement).
 */

#include <QtTest/QtTest>
#include <QFile>
#include <QTemporaryDir>

#include "packetinfo.h"
#include "packetcommon.h"

namespace {

// Writes `xml` into a freshly-created file under `dir`. Returns the path.
QString writeFixture(const QTemporaryDir& dir, const char* basename,
                     const QByteArray& toml)
{
    const QString path = dir.filePath(QString::fromLatin1(basename));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        qFatal("could not open %s for writing", qUtf8Printable(path));
    }
    f.write(toml);
    f.close();
    return path;
}

} // namespace

class PacketInfoTest : public QObject
{
    Q_OBJECT

private slots:
    void load_basicHappyPath();
    void load_payloadDirAndSizeCheck();
    void load_multipleCommentsAndPayloads();
    void load_implicitLenAndUpdatedAttrs();
    void load_unknownTypenameStillLoadsOpcode();
    void load_missingIdAttribute();
    void load_malformedHexId();
    void load_missingNameAttribute();
    void load_emptyDocument();
};

// Happy path: three opcodes with single payloads. Validates count,
// id parsing (hex), name lookup, and the most common payload shape.
void PacketInfoTest::load_basicHappyPath()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    QByteArray toml =
        "[[zone]]\n"
        "id      = \"0001\"\n"
        "name    = \"OP_Foo\"\n"
        "\n"
        "  [[zone.payloads]]\n"
        "  dir           = \"server\"\n"
        "  typename      = \"opCodeStruct\"\n"
        "  sizechecktype = \"match\"\n"
        "\n"
        "[[zone]]\n"
        "id      = \"abcd\"\n"
        "name    = \"OP_Bar\"\n"
        "\n"
        "  [[zone.payloads]]\n"
        "  dir           = \"client\"\n"
        "  typename      = \"none\"\n"
        "  sizechecktype = \"none\"\n"
        "\n"
        "[[zone]]\n"
        "id      = \"FFFF\"\n"
        "name    = \"OP_Unresolved\"\n"
        "\n"
        "  [[zone.payloads]]\n"
        "  dir           = \"both\"\n"
        "  typename      = \"unknown\"\n"
        "  sizechecktype = \"modulus\"\n"
        "\n";

    const QString path = writeFixture(tmp, "happy.toml", toml);

    EQPacketTypeDB typeDB;
    EQPacketOPCodeDB db{QStringLiteral("zone")};
    QVERIFY(db.load(typeDB, path));

    QCOMPARE(db.opcodes().count(), 3);

    const EQPacketOPCode* foo = db.find(QStringLiteral("OP_Foo"));
    QVERIFY(foo != nullptr);
    QCOMPARE(foo->opcode(), uint16_t(0x0001));
    QCOMPARE(foo->name(), QStringLiteral("OP_Foo"));
    QCOMPARE(foo->count(), 1);  // one payload

    const EQPacketOPCode* bar = db.find(QStringLiteral("OP_Bar"));
    QVERIFY(bar != nullptr);
    QCOMPARE(bar->opcode(), uint16_t(0xabcd));

    // Hex case-insensitive — uppercase digits parse the same way.
    const EQPacketOPCode* unresolved = db.find(QStringLiteral("OP_Unresolved"));
    QVERIFY(unresolved != nullptr);
    QCOMPARE(unresolved->opcode(), uint16_t(0xffff));

    // Reverse lookup by id.
    QVERIFY(db.find(uint16_t(0x0001)) == foo);
    QVERIFY(db.find(uint16_t(0xabcd)) == bar);
}

// Payload direction + sizecheck-type translation. Each <payload>
// attribute combination must produce the expected enum value.
void PacketInfoTest::load_payloadDirAndSizeCheck()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    QByteArray toml =
        "[[zone]]\n"
        "id      = \"0001\"\n"
        "name    = \"OP_Server\"\n"
        "\n"
        "  [[zone.payloads]]\n"
        "  dir           = \"server\"\n"
        "  typename      = \"opCodeStruct\"\n"
        "  sizechecktype = \"match\"\n"
        "\n"
        "[[zone]]\n"
        "id      = \"0002\"\n"
        "name    = \"OP_Client\"\n"
        "\n"
        "  [[zone.payloads]]\n"
        "  dir           = \"client\"\n"
        "  typename      = \"opCodeStruct\"\n"
        "  sizechecktype = \"none\"\n"
        "\n"
        "[[zone]]\n"
        "id      = \"0003\"\n"
        "name    = \"OP_Both\"\n"
        "\n"
        "  [[zone.payloads]]\n"
        "  dir           = \"both\"\n"
        "  typename      = \"opCodeStruct\"\n"
        "  sizechecktype = \"modulus\"\n"
        "\n";

    const QString path = writeFixture(tmp, "dir.toml", toml);

    EQPacketTypeDB typeDB;
    EQPacketOPCodeDB db{QStringLiteral("zone")};
    QVERIFY(db.load(typeDB, path));

    const EQPacketOPCode* server = db.find(QStringLiteral("OP_Server"));
    QVERIFY(server != nullptr);
    QCOMPARE(server->count(), 1);
    EQPacketPayload* sp = server->first();
    QCOMPARE(sp->dir(), uint8_t(DIR_Server));
    QCOMPARE(int(sp->sizeCheckType()), int(SZC_Match));
    QCOMPARE(sp->typeName(), QStringLiteral("opCodeStruct"));

    EQPacketPayload* cp = db.find(QStringLiteral("OP_Client"))->first();
    QCOMPARE(cp->dir(), uint8_t(DIR_Client));
    QCOMPARE(int(cp->sizeCheckType()), int(SZC_None));

    EQPacketPayload* bp = db.find(QStringLiteral("OP_Both"))->first();
    QCOMPARE(bp->dir(), uint8_t(DIR_Client | DIR_Server));
    QCOMPARE(int(bp->sizeCheckType()), int(SZC_Modulus));
}

// Comments + multi-payload opcodes — both lists round-trip through
// the parser in document order.
// The TOML schema carries ONE `comment` per opcode where the XML allowed a
// repeated <comment> element, so this now pins one comment plus the payload
// ordering (which is what the rest of the test was really about — `at(0)` /
// `at(1)` order is golden-sensitive, install order being fire order).
void PacketInfoTest::load_multipleCommentsAndPayloads()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    QByteArray toml =
        "[[zone]]\n"
        "id      = \"00aa\"\n"
        "name    = \"OP_Multi\"\n"
        "comment = \"first comment second comment\"\n"
        "\n"
        "  [[zone.payloads]]\n"
        "  dir           = \"server\"\n"
        "  typename      = \"opCodeStruct\"\n"
        "  sizechecktype = \"match\"\n"
        "\n"
        "  [[zone.payloads]]\n"
        "  dir           = \"client\"\n"
        "  typename      = \"opCodeStruct\"\n"
        "  sizechecktype = \"none\"\n"
        "\n";

    const QString path = writeFixture(tmp, "multi.toml", toml);

    EQPacketTypeDB typeDB;
    EQPacketOPCodeDB db{QStringLiteral("zone")};
    QVERIFY(db.load(typeDB, path));

    const EQPacketOPCode* op = db.find(QStringLiteral("OP_Multi"));
    QVERIFY(op != nullptr);
    QCOMPARE(op->comments().size(), 1);
    QCOMPARE(op->comments().at(0), QStringLiteral("first comment second comment"));

    QCOMPARE(op->count(), 2);
    QCOMPARE(op->at(0)->dir(), uint8_t(DIR_Server));
    QCOMPARE(op->at(1)->dir(), uint8_t(DIR_Client));
}

// `implicitlen` and `updated` are optional attributes; when present
// they end up on the EQPacketOPCode object verbatim.
void PacketInfoTest::load_implicitLenAndUpdatedAttrs()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    QByteArray toml =
        "[[zone]]\n"
        "id      = \"00aa\"\n"
        "name    = \"OP_WithExtras\"\n"
        "updated = \"2026-04-25\"\n"
        "implicitlen = \"42\"\n"
        "\n"
        "  [[zone.payloads]]\n"
        "  dir           = \"server\"\n"
        "  typename      = \"opCodeStruct\"\n"
        "  sizechecktype = \"match\"\n"
        "\n"
        "[[zone]]\n"
        "id      = \"00ab\"\n"
        "name    = \"OP_Plain\"\n"
        "\n"
        "  [[zone.payloads]]\n"
        "  dir           = \"server\"\n"
        "  typename      = \"opCodeStruct\"\n"
        "  sizechecktype = \"match\"\n"
        "\n";

    const QString path = writeFixture(tmp, "extras.toml", toml);

    EQPacketTypeDB typeDB;
    EQPacketOPCodeDB db{QStringLiteral("zone")};
    QVERIFY(db.load(typeDB, path));

    const EQPacketOPCode* extras = db.find(QStringLiteral("OP_WithExtras"));
    QVERIFY(extras != nullptr);
    QCOMPARE(extras->implicitLen(), uint16_t(42));
    QCOMPARE(extras->updated(), QStringLiteral("2026-04-25"));

    const EQPacketOPCode* plain = db.find(QStringLiteral("OP_Plain"));
    QVERIFY(plain != nullptr);
    QCOMPARE(plain->implicitLen(), uint16_t(0));
    QVERIFY(plain->updated().isEmpty());
}

// An unknown payload typename emits a warning but does NOT abort load —
// the opcode itself is still added to the DB. This matches the legacy
// SAX behavior used in production: bad typename in conf XMLs surfaces
// as a startup warning, not a hard failure.
void PacketInfoTest::load_unknownTypenameStillLoadsOpcode()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    QByteArray toml =
        "[[zone]]\n"
        "id      = \"0001\"\n"
        "name    = \"OP_BadType\"\n"
        "\n"
        "  [[zone.payloads]]\n"
        "  dir           = \"server\"\n"
        "  typename      = \"NoSuchStruct\"\n"
        "  sizechecktype = \"match\"\n"
        "\n";

    const QString path = writeFixture(tmp, "badtype.toml", toml);

    EQPacketTypeDB typeDB;
    EQPacketOPCodeDB db{QStringLiteral("zone")};
    QVERIFY(db.load(typeDB, path));

    const EQPacketOPCode* op = db.find(QStringLiteral("OP_BadType"));
    QVERIFY(op != nullptr);
    QCOMPARE(op->count(), 1);
    // typeName is left empty when setType rejected the unknown type.
    QVERIFY(op->first()->typeName().isEmpty());
}

// `<opcode>` element without an `id` attribute aborts the parse. The
// legacy SAX impl returned false from startElement, which makes
// QXmlSimpleReader::parse return false. Pinned here so the
// QXmlStreamReader port preserves the abort semantics.
void PacketInfoTest::load_missingIdAttribute()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    QByteArray toml =
        "[[zone]]\n"
        "name    = \"OP_NoId\"\n"
        "\n"
        "  [[zone.payloads]]\n"
        "  dir           = \"server\"\n"
        "  typename      = \"opCodeStruct\"\n"
        "  sizechecktype = \"match\"\n"
        "\n";

    const QString path = writeFixture(tmp, "no-id.xml", toml);

    EQPacketTypeDB typeDB;
    EQPacketOPCodeDB db{QStringLiteral("zone")};
    QVERIFY(!db.load(typeDB, path));
    QCOMPARE(db.opcodes().count(), 0);
}

// Hex parse failure on the id attribute also aborts the parse.
void PacketInfoTest::load_malformedHexId()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    QByteArray toml =
        "[[zone]]\n"
        "id      = \"zzzz\"\n"
        "name    = \"OP_BadId\"\n"
        "\n"
        "  [[zone.payloads]]\n"
        "  dir           = \"server\"\n"
        "  typename      = \"opCodeStruct\"\n"
        "  sizechecktype = \"match\"\n"
        "\n";

    const QString path = writeFixture(tmp, "bad-id.xml", toml);

    EQPacketTypeDB typeDB;
    EQPacketOPCodeDB db{QStringLiteral("zone")};
    QVERIFY(!db.load(typeDB, path));
    QCOMPARE(db.opcodes().count(), 0);
}

// Missing name attribute also aborts the parse.
void PacketInfoTest::load_missingNameAttribute()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    QByteArray toml =
        "[[zone]]\n"
        "id      = \"0001\"\n"
        "\n"
        "  [[zone.payloads]]\n"
        "  dir           = \"server\"\n"
        "  typename      = \"opCodeStruct\"\n"
        "  sizechecktype = \"match\"\n"
        "\n";

    const QString path = writeFixture(tmp, "no-name.xml", toml);

    EQPacketTypeDB typeDB;
    EQPacketOPCodeDB db{QStringLiteral("zone")};
    QVERIFY(!db.load(typeDB, path));
    QCOMPARE(db.opcodes().count(), 0);
}

// An empty (but well-formed) document produces an empty DB.
void PacketInfoTest::load_emptyDocument()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    QByteArray toml =
        "\n";

    const QString path = writeFixture(tmp, "empty.toml", toml);

    EQPacketTypeDB typeDB;
    EQPacketOPCodeDB db{QStringLiteral("zone")};
    QVERIFY(db.load(typeDB, path));
    QCOMPARE(db.opcodes().count(), 0);
}

QTEST_APPLESS_MAIN(PacketInfoTest)
#include "packetinfo_test.moc"
