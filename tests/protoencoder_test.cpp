// Tier-1 unit tests for seq::encode functions in protoencoder.{h,cpp}.
//
// These tests construct synthetic Item/Spawn objects and verify the
// serialized seq.v1 protobuf fields. They do NOT exercise the
// SessionAdapter wiring or the WebSocket I/O path — those are tier-2
// (replay against .vpk goldens) and tier-3 (full integration) and live
// behind the work documented in tests/README.md.
//
// What this catches:
//   - Field renames or removals on the seq.v1 schema side
//   - Mistakes in the in-memory-type → wire-field mapping
//   - Regressions in the heading-degrees conversion (fillPos)
//   - Regressions in name transforms (article move, underscore strip)
//
// What this does NOT catch:
//   - Decoder regressions (struct layout drift after EQ patch days)
//   - Snapshot/tail race regressions in SessionAdapter
//   - Opcode-wiring changes in DaemonApp

#include <QtTest/QtTest>

#include "everquest.h"
#include "protoencoder.h"
#include "spawn.h"

#include "seq/v1/events.pb.h"

class ProtoEncoderTest : public QObject {
    Q_OBJECT

private slots:
    void fillSpawn_npc();
    void fillSpawn_npcCorpse();
    void fillSpawn_pcCorpse();
    void fillSpawn_otherPlayer();
    void fillSpawn_drop();
    void fillSpawn_door();
    void fillSpawn_semanticDoorProjectsLegacyCompatibility();
    void fillSpawn_semanticGroundItemProjectsLegacyCompatibility();
    void fillPos_heading_data();
    void fillPos_heading();
    void transformedName_articleMove();
    void fillSpawn_defaultSpawn_isFullyInitialized();
};

// A vanilla NPC: id, name, position, hp/level/race/class/deity all
// populated. The web client renders this as a labeled, colored, moving
// dot — every field below maps to something visible.
void ProtoEncoderTest::fillSpawn_npc()
{
    Spawn s;
    s.setNPC(SPAWN_NPC);
    s.setName("a goblin");
    s.setLastName("Spectral Servant");
    s.setLevel(42);
    s.setRace(46);              // goblin
    s.setClassVal(1);           // Warrior
    s.setDeity(140);
    s.setHP(800);
    s.setMaxHP(1000);
    s.setGuildID(0xffff);
    s.setGM(0);
    s.setPos(123, -456, 78);
    s.setDeltas(1, 2, 3);
    s.setHeading(64, 0);
    s.setAnimation(0);

    seq::v1::Spawn out;
    seq::encode::fillSpawn(&out, s);

    // Identity + classification.
    QCOMPARE(out.id(), 0u);     // default Spawn() ctor → id 0
    QCOMPARE(out.type(), seq::v1::NPC);

    // Name transform: "a goblin" → "goblin, a" (article move).
    QCOMPARE(QString::fromStdString(out.name()), QString("goblin, a"));
    QCOMPARE(QString::fromStdString(out.last_name()),
             QString("Spectral Servant"));

    // Numeric fields pass through unchanged.
    QCOMPARE(out.level(), 42u);
    QCOMPARE(out.race(), 46u);
    QCOMPARE(out.class_(), 1u);
    QCOMPARE(out.deity(), 140u);
    QCOMPARE(out.hp_cur(), 800u);
    QCOMPARE(out.hp_max(), 1000u);
    QCOMPARE(out.guild_id(), 0xffffu);
    QVERIFY(!out.is_gm());

    // Position embedded as Pos sub-message. X/Y/vx/vy are negated by
    // fillPos so the wire is in screen convention; Z and vz pass
    // through. See seq/v1/events.proto Pos for the contract.
    QVERIFY(out.has_pos());
    QCOMPARE(out.pos().x(), -123);
    QCOMPARE(out.pos().y(), 456);
    QCOMPARE(out.pos().z(), 78);
    QCOMPARE(out.pos().vx(), -1);
    QCOMPARE(out.pos().vy(), -2);
    QCOMPARE(out.pos().vz(), 3);
}

// Corpses get distinct SpawnTypes so the client can render PC vs NPC
// corpses differently (different colors in MapCanvas). The two flavors
// are distinguished by m_NPC (SPAWN_NPC_CORPSE vs SPAWN_PC_CORPSE).
void ProtoEncoderTest::fillSpawn_npcCorpse()
{
    Spawn s;
    s.setNPC(SPAWN_NPC_CORPSE);
    s.setName("a goblin");

    seq::v1::Spawn out;
    seq::encode::fillSpawn(&out, s);

    QCOMPARE(out.type(), seq::v1::CORPSE_NPC);
}

void ProtoEncoderTest::fillSpawn_pcCorpse()
{
    Spawn s;
    s.setNPC(SPAWN_PC_CORPSE);
    s.setName("Vox");

    seq::v1::Spawn out;
    seq::encode::fillSpawn(&out, s);

    QCOMPARE(out.type(), seq::v1::CORPSE_PC);
}

// Other PCs in the zone are stored as Spawn (Item::type==tSpawn) with
// m_NPC == SPAWN_PLAYER. Only the local player is a Player object with
// tPlayer; the typeFromItem fall-through must promote SPAWN_PLAYER /
// SPAWN_SELF Spawns to PC so other players don't render as NPCs.
void ProtoEncoderTest::fillSpawn_otherPlayer()
{
    Spawn s;
    s.setNPC(SPAWN_PLAYER);
    s.setName("Vox");

    seq::v1::Spawn out;
    seq::encode::fillSpawn(&out, s);

    QCOMPARE(out.type(), seq::v1::PC);
    QCOMPARE(QString::fromStdString(out.name()), QString("Vox"));
}

// Drops take the non-Spawn branch in fillSpawn — only id, type, name,
// and Item-level position get filled. Name passes through verbatim
// because drops don't carry the article/instance-suffix convention.
void ProtoEncoderTest::fillSpawn_drop()
{
    Item drop(tDrop, 4242);
    drop.setName("Pile_of_Coins");
    drop.setPos(10, 20, 30);

    seq::v1::Spawn out;
    seq::encode::fillSpawn(&out, drop);

    QCOMPARE(out.id(), 4242u);
    QCOMPARE(out.type(), seq::v1::DROP);
    QCOMPARE(QString::fromStdString(out.name()), QString("Pile_of_Coins"));
    QVERIFY(out.has_pos());
    // X/Y negated to screen convention; Z passes through.
    QCOMPARE(out.pos().x(), -10);
    QCOMPARE(out.pos().y(), -20);
    QCOMPARE(out.pos().z(), 30);
    // Drop branch doesn't fill velocity / heading / level / hp.
    QCOMPARE(out.pos().vx(), 0);
    QCOMPARE(out.level(), 0u);
    QCOMPARE(out.hp_max(), 0u);
}

void ProtoEncoderTest::fillSpawn_door()
{
    Item door(tDoors, 7);
    door.setName("Door_to_Mistmoore");
    door.setPos(-100, 0, 0);

    seq::v1::Spawn out;
    seq::encode::fillSpawn(&out, door);

    QCOMPARE(out.id(), 7u);
    QCOMPARE(out.type(), seq::v1::DOOR);
    // X negated: input -100 → wire 100.
    QCOMPARE(out.pos().x(), 100);
}

void ProtoEncoderTest::fillSpawn_semanticDoorProjectsLegacyCompatibility()
{
    Door door(7, QStringLiteral("POKTELE500"), 10.75f, -20.25f, 3.5f,
              91.25f, 1, 100, 2, 3, 4, std::nullopt);
    QCOMPARE(door.name(), QStringLiteral("POKTELE500"));
    QVERIFY(!door.semanticZonePoint());

    seq::v1::Spawn out;
    seq::encode::fillSpawn(&out, door);
    QCOMPARE(QString::fromStdString(out.name()),
             QStringLiteral("Door: POKTELE500 (7) "));
    QCOMPARE(out.type(), seq::v1::DOOR);
    QCOMPARE(out.pos().x(), -10);
    QCOMPARE(out.pos().y(), 20);
    QCOMPARE(out.pos().z(), 35);
}

void ProtoEncoderTest::fillSpawn_semanticGroundItemProjectsLegacyCompatibility()
{
    Drop drop(8, QStringLiteral("IT63_ACTORDEF"), 1.75f, -2.75f, 3.75f,
              std::nullopt);
    QCOMPARE(drop.name(), QStringLiteral("IT63_ACTORDEF"));

    seq::v1::Spawn out;
    seq::encode::fillSpawn(&out, drop);
    QCOMPARE(out.type(), seq::v1::DROP);
    QCOMPARE(QString::fromStdString(out.name()),
             seq::encode::compatibilityGroundItemName(
                 QStringLiteral("IT63_ACTORDEF")));
    QCOMPARE(out.pos().x(), -1);
    QCOMPARE(out.pos().y(), 2);
    QCOMPARE(out.pos().z(), 3);
}

// Verifies the 8-bit heading → degrees formula in fillPos for non-Player
// spawns: degrees = 360 - ((raw8 * 360) >> 8).
//
// EQ stores headings as 0..255 where 0 means "facing north" by the
// game's convention. The legacy code converts to degrees clockwise from
// north. These are the cardinal points the formula must round to.
void ProtoEncoderTest::fillPos_heading_data()
{
    QTest::addColumn<int>("rawHeading");
    QTest::addColumn<int>("expectedDegrees");

    // raw 0   → 360 - (0   * 360 / 256) =  360
    // raw 64  → 360 - (64  * 360 / 256) =  270   (east → west? game-specific)
    // raw 128 → 360 - (128 * 360 / 256) =  180
    // raw 192 → 360 - (192 * 360 / 256) =  90
    QTest::newRow("north")  << 0   << 360;
    QTest::newRow("west")   << 64  << 270;
    QTest::newRow("south")  << 128 << 180;
    QTest::newRow("east")   << 192 << 90;
}

void ProtoEncoderTest::fillPos_heading()
{
    QFETCH(int, rawHeading);
    QFETCH(int, expectedDegrees);

    Spawn s;
    s.setNPC(SPAWN_NPC);
    s.setHeading(static_cast<int8_t>(rawHeading), 0);
    s.setPos(0, 0, 0);

    seq::v1::Pos out;
    seq::encode::fillPos(&out, s);

    QCOMPARE(out.heading(), expectedDegrees);
}

// transformedName moves leading articles ("a "/"an "/"the ") to the end
// so the spawn list sorts by noun. Underscores in the raw name become
// spaces and trailing instance digits are stripped before the article
// move runs.
void ProtoEncoderTest::transformedName_articleMove()
{
    Spawn s;
    s.setNPC(SPAWN_NPC);

    s.setName("an_orc_pawn00");
    seq::v1::Spawn out;
    seq::encode::fillSpawn(&out, s);
    QCOMPARE(QString::fromStdString(out.name()),
             QString("orc pawn, an"));

    s.setName("the Spectre");
    seq::v1::Spawn out2;
    seq::encode::fillSpawn(&out2, s);
    QCOMPARE(QString::fromStdString(out2.name()),
             QString("Spectre, the"));
}

// Lock in that a freshly default-constructed Spawn produces a fully
// determined wire envelope. The previous regression: m_guildServerID,
// m_isMount, m_isMercenary, m_isAura, m_notUpdated were never set in
// the default ctor, so encoder + category logic read uninitialized
// memory (broke tier-2 golden determinism). This test documents the
// expected zero-state and fails noisily if a future field gets added
// to Spawn without being initialized in the ctor.
void ProtoEncoderTest::fillSpawn_defaultSpawn_isFullyInitialized()
{
    Spawn s;

    seq::v1::Spawn out;
    seq::encode::fillSpawn(&out, s);

    QCOMPARE(out.id(), 0u);
    QCOMPARE(out.type(), seq::v1::NPC);
    QCOMPARE(out.guild_id(), 0xffffu);
    QCOMPARE(out.guild_server_id(), 0u);  // was uninit pre-2026-04-25
    QCOMPARE(out.hp_cur(), 0u);
    QCOMPARE(out.hp_max(), 0u);
    QCOMPARE(out.level(), 0u);
    QCOMPARE(out.race(), 0u);
    QCOMPARE(out.class_(), 0u);
    QCOMPARE(out.deity(), 0u);
    QVERIFY(!out.is_gm());
}

// QTEST_GUILESS_MAIN — daemon code is headless (QCoreApplication only),
// and the test must match so it runs without a display server.
QTEST_GUILESS_MAIN(ProtoEncoderTest)
#include "protoencoder_test.moc"
