/*
 *  namepromoter_test.cpp
 *  Tier-1 unit test for NamePromoter — promotes a Box to a character on
 *  OP_EnterWorld (world C>S, char name in the leading 64-byte slot of a
 *  72-byte payload).
 *
 *  NamePromoter listens on an EQPacketStream's 5-arg decodedPacket signal.
 *  The test drives it by constructing a real stream (its opcode DB is never
 *  exercised — NamePromoter reads the EQPacketOPCode passed with the emit)
 *  and emitting decodedPacket directly. Matching is by opcode NAME (the wire
 *  id drifts every patch), so the test stamps an arbitrary id with the
 *  "OP_EnterWorld" name.
 */

#include <QtTest/QtTest>

#include <cstring>
#include <netinet/in.h>

#include "boxregistry.h"
#include "namepromoter.h"
#include "packetcommon.h"
#include "packetinfo.h"
#include "packetstream.h"

namespace {
constexpr in_addr_t kClientIp = 0x0a0d0008;
constexpr in_port_t kSrvPort  = 0x789c;
constexpr in_addr_t kServerIp = 0x0a0d00cd;  // the world server this box handshook with
constexpr uint16_t  kAnyOp    = 0x9bdc;   // current OP_EnterWorld C>S id

in_port_t cport(uint16_t p) { return htons(p); }

// 72-byte world C>S payload with `name` zero-padded in the leading 64-byte
// slot. The remaining 8 bytes are the rest of the EnterWorld struct (zeroed).
QByteArray enterWorldPayload(const char* name)
{
  QByteArray p(72, '\0');
  std::strncpy(p.data(), name, 63);
  return p;
}
}

class NamePromoterTest : public QObject
{
  Q_OBJECT

private slots:
  void promotesFromEnterWorld();
  void ignoresWrongOpcodeName();
  void ignoresWrongLength();
  void ignoresServerDirection();
  void mergesRelogByName();

private:
  // Emit a decoded packet on `stream` tagged with opcode `name`.
  static void feed(EQPacketStream& stream, const QString& opName,
                   const QByteArray& payload, uint8_t dir);
};

void NamePromoterTest::feed(EQPacketStream& stream, const QString& opName,
                            const QByteArray& payload, uint8_t dir)
{
  EQPacketOPCode entry(kAnyOp, opName);
  // Qt signals are public; emit directly to drive the connected slot.
  emit stream.decodedPacket(
      reinterpret_cast<const uint8_t*>(payload.constData()),
      size_t(payload.size()), dir, kAnyOp, &entry);
}

void NamePromoterTest::promotesFromEnterWorld()
{
  EQPacketOPCodeDB db;
  EQPacketStream stream(client2world, DIR_Client, 0, db);
  BoxRegistry reg;
  Box* box = reg.observe(kClientIp, kServerIp, cport(1000), kSrvPort, 1);
  NamePromoter promoter(box, &reg, &stream);

  feed(stream, QStringLiteral("OP_EnterWorld"),
       enterWorldPayload("Alpha"), DIR_Client);

  QCOMPARE(box->display_name, QStringLiteral("Alpha"));
  QVERIFY(box->box_id.startsWith(QStringLiteral("b-")));
}

void NamePromoterTest::ignoresWrongOpcodeName()
{
  EQPacketOPCodeDB db;
  EQPacketStream stream(client2world, DIR_Client, 0, db);
  BoxRegistry reg;
  Box* box = reg.observe(kClientIp, kServerIp, cport(1000), kSrvPort, 1);
  NamePromoter promoter(box, &reg, &stream);

  feed(stream, QStringLiteral("OP_ZoneEntry"),
       enterWorldPayload("Alpha"), DIR_Client);

  QVERIFY(box->display_name.isEmpty());           // not OP_EnterWorld
}

void NamePromoterTest::ignoresWrongLength()
{
  EQPacketOPCodeDB db;
  EQPacketStream stream(client2world, DIR_Client, 0, db);
  BoxRegistry reg;
  Box* box = reg.observe(kClientIp, kServerIp, cport(1000), kSrvPort, 1);
  NamePromoter promoter(box, &reg, &stream);

  // Server echo variants share the name but are not 72 bytes C>S.
  feed(stream, QStringLiteral("OP_EnterWorld"),
       enterWorldPayload("Alpha").left(64), DIR_Client);

  QVERIFY(box->display_name.isEmpty());
}

void NamePromoterTest::ignoresServerDirection()
{
  EQPacketOPCodeDB db;
  EQPacketStream stream(world2client, DIR_Server, 0, db);
  BoxRegistry reg;
  Box* box = reg.observe(kClientIp, kServerIp, cport(1000), kSrvPort, 1);
  NamePromoter promoter(box, &reg, &stream);

  feed(stream, QStringLiteral("OP_EnterWorld"),
       enterWorldPayload("Alpha"), DIR_Server);

  QVERIFY(box->display_name.isEmpty());           // C>S only
}

void NamePromoterTest::mergesRelogByName()
{
  EQPacketOPCodeDB db;
  EQPacketStream stream(client2world, DIR_Client, 0, db);
  BoxRegistry reg;
  Box* first  = reg.observe(kClientIp, kServerIp, cport(1000), kSrvPort, 1);
  Box* second = reg.observe(kClientIp, kServerIp, cport(1001), kSrvPort, 2);
  NamePromoter p1(first,  &reg, &stream);
  NamePromoter p2(second, &reg, &stream);

  // first promotes; second re-handshakes the same character on a new socket.
  // (Both promoters see every emit on the shared stream, but each only
  // promotes its own still-unnamed box — display_name guards re-entry.)
  feed(stream, QStringLiteral("OP_EnterWorld"),
       enterWorldPayload("Alpha"), DIR_Client);
  QCOMPARE(first->display_name,  QStringLiteral("Alpha"));
  QCOMPARE(second->display_name, QStringLiteral("Alpha"));
  // Same character's sessions share box_id (the name hash) — that IS the group.
  QCOMPARE(second->box_id, first->box_id);
  QCOMPARE(reg.distinctCount(), size_t(1));
}

QTEST_GUILESS_MAIN(NamePromoterTest)
#include "namepromoter_test.moc"
