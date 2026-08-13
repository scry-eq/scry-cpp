/*
 *  boxregistry_test.cpp
 *  Tier-1 unit test for BoxRegistry — the multibox identity layer.
 *
 *  Covers the wire-key observation path (observe / lookupByWorld) and the
 *  two-layer identity logic that drives the picker: promoteByName (placeholder
 *  -> name-hash id), re-handshake merge, currentBoxFor (latest box of a
 *  character), the adopt-active-from-stale-placeholder rule, the active-box
 *  switch signal, and the zone-binding lookups (lookupBoundZone +
 *  lookupByExpectedZone recency tiebreak / skip-bound). No network, no
 *  streams — Box fields the daemon's observers populate are set directly.
 */

#include <QtTest/QtTest>

#include <netinet/in.h>

#include "boxregistry.h"

// So QSignalSpy can capture boxAboutToBeRemoved(Box*)'s argument (Box is a
// plain struct, not a QObject, so it isn't auto-registered).
Q_DECLARE_METATYPE(Box*)

namespace {
// Distinct same-host 5-tuples: one client IP, varying client world ports,
// one world server port (the :9016 case that motivated multibox).
constexpr in_addr_t kClientIp = 0x0a0d0008;  // 10.13.0.8
constexpr in_port_t kSrvPort  = 0x789c;      // htons(9016)-ish; value only
constexpr in_addr_t kServerIp = 0x0a0d00cd;  // the world server this box handshook with

in_port_t cport(uint16_t p) { return htons(p); }
}

class BoxRegistryTest : public QObject
{
  Q_OBJECT

private slots:
  void observeCreatesPrimary();
  void observeSameTupleUpdates();
  void observeDistinctCreatesSecond();
  void lookupByWorldFindsAndMisses();
  void promoteByNameStampsHashId();
  void promoteByNameMergesRelog();
  void currentBoxForReturnsLatest();
  void adoptActiveFromStalePlaceholder();
  void setActiveBoxEmitsChange();
  void lookupBoundZoneMatches();
  void lookupBoundZoneFindsMergedLiveBox();
  void lookupByExpectedZoneRecencyAndSkipBound();
  void anyZoneServerAnnouncedTracksFirstAnnouncement();
  void evictStaleReapsSupersededAlias();
  void evictStaleReapsLoggedOffGroup();
  void evictStaleProtectsPrimaryActiveFresh();
  void evictStaleDisabledAndNoVictims();
  // SCAFFOLD (character refactor): the session state machine + name-keyed view.
  void stateStartsPendingUntilPromoted();
  void stateAttachedSupersededOnRelog();
  void charactersViewAndCurrentSessionFor();
};

void BoxRegistryTest::observeCreatesPrimary()
{
  BoxRegistry reg;
  Box* b = reg.observe(kClientIp, kServerIp, cport(1000), kSrvPort, 1);
  QVERIFY(b != nullptr);
  QVERIFY(b->is_primary);
  QCOMPARE(b->packet_count, quint64(1));
  QCOMPARE(reg.size(), size_t(1));
  QCOMPARE(reg.distinctCount(), size_t(1));
  QVERIFY(b->display_name.isEmpty());           // not promoted yet
  QVERIFY(b->box_id.startsWith(QStringLiteral("p-")));  // placeholder id
  QCOMPARE(reg.activeCharacterId(), b->box_id);        // first box is active
}

void BoxRegistryTest::observeSameTupleUpdates()
{
  BoxRegistry reg;
  Box* a = reg.observe(kClientIp, kServerIp, cport(1000), kSrvPort, 1);
  Box* b = reg.observe(kClientIp, kServerIp, cport(1000), kSrvPort, 2);
  QCOMPARE(a, b);                                // same Box, not a new one
  QCOMPARE(b->packet_count, quint64(2));
  QCOMPARE(reg.size(), size_t(1));
}

void BoxRegistryTest::observeDistinctCreatesSecond()
{
  BoxRegistry reg;
  Box* a = reg.observe(kClientIp, kServerIp, cport(1000), kSrvPort, 1);
  Box* b = reg.observe(kClientIp, kServerIp, cport(1001), kSrvPort, 2);
  QVERIFY(a != b);
  QVERIFY(a->is_primary);
  QVERIFY(!b->is_primary);                       // only the first is primary
  QCOMPARE(reg.size(), size_t(2));
  QCOMPARE(reg.distinctCount(), size_t(2));
}

void BoxRegistryTest::lookupByWorldFindsAndMisses()
{
  BoxRegistry reg;
  Box* a = reg.observe(kClientIp, kServerIp, cport(1000), kSrvPort, 1);
  QCOMPARE(reg.lookupByWorld(kClientIp, cport(1000), kSrvPort), a);
  QVERIFY(reg.lookupByWorld(kClientIp, cport(9999), kSrvPort) == nullptr);
}

void BoxRegistryTest::promoteByNameStampsHashId()
{
  BoxRegistry reg;
  Box* b = reg.observe(kClientIp, kServerIp, cport(1000), kSrvPort, 1);
  const QString placeholder = b->box_id;

  Box* parent = reg.promoteByName(b, QStringLiteral("Alpha"));
  QVERIFY(parent == nullptr);                    // first sighting: no merge
  QCOMPARE(b->display_name, QStringLiteral("Alpha"));
  QVERIFY(b->box_id.startsWith(QStringLiteral("b-")));  // stable name hash
  QVERIFY(b->box_id != placeholder);
  QCOMPARE(reg.distinctCount(), size_t(1));
  // findById tracks the new id; the old placeholder is gone.
  QCOMPARE(reg.findById(b->box_id), b);
  QVERIFY(reg.findById(placeholder) == nullptr);
}

void BoxRegistryTest::promoteByNameMergesRelog()
{
  BoxRegistry reg;
  Box* first  = reg.observe(kClientIp, kServerIp, cport(1000), kSrvPort, 1);
  Box* second = reg.observe(kClientIp, kServerIp, cport(1001), kSrvPort, 2);
  reg.promoteByName(first,  QStringLiteral("Alpha"));

  // Same character re-handshakes on a new world socket (zone change).
  Box* parent = reg.promoteByName(second, QStringLiteral("Alpha"));
  QCOMPARE(parent, first);                        // re-handshake of the first
  // A character's sessions share box_id (the name hash) — that IS the grouping.
  QCOMPARE(second->box_id, first->box_id);
  QCOMPARE(reg.distinctCount(), size_t(1));       // one character, two boxes
  QCOMPARE(reg.size(), size_t(2));
}

void BoxRegistryTest::currentBoxForReturnsLatest()
{
  BoxRegistry reg;
  Box* first  = reg.observe(kClientIp, kServerIp, cport(1000), kSrvPort, 10);
  Box* second = reg.observe(kClientIp, kServerIp, cport(1001), kSrvPort, 20);  // later
  reg.promoteByName(first,  QStringLiteral("Alpha"));
  reg.promoteByName(second, QStringLiteral("Alpha"));

  // The character's live box is the most recently seen one (the new zone
  // session), reachable via any of the character's box_ids.
  QCOMPARE(reg.currentBoxFor(first->box_id), second);
}

void BoxRegistryTest::adoptActiveFromStalePlaceholder()
{
  BoxRegistry reg;
  // First box is a partial/dead session that never resolves a character
  // (daemon started mid-session) — it's active but stays a placeholder.
  Box* stale = reg.observe(kClientIp, kServerIp, cport(1000), kSrvPort, 1);
  QCOMPARE(reg.activeCharacterId(), stale->box_id);

  // A real distinct character then decodes on another box. Because the
  // active box is an unpromoted placeholder, the registry adopts the newly
  // decoded character as active (else SessionAdapter streams an empty box).
  Box* real = reg.observe(kClientIp, kServerIp, cport(1001), kSrvPort, 2);
  reg.promoteByName(real, QStringLiteral("Beta"));
  QCOMPARE(reg.activeCharacterId(), real->box_id);
}

void BoxRegistryTest::setActiveBoxEmitsChange()
{
  BoxRegistry reg;
  Box* a = reg.observe(kClientIp, kServerIp, cport(1000), kSrvPort, 1);
  Box* b = reg.observe(kClientIp, kServerIp, cport(1001), kSrvPort, 2);
  reg.promoteByName(a, QStringLiteral("Alpha"));
  reg.promoteByName(b, QStringLiteral("Beta"));

  Box* spyOld = nullptr;
  Box* spyNew = nullptr;
  int  changes = 0;
  QObject::connect(&reg, &BoxRegistry::activeCharacterChanged,
                   [&](Box* o, Box* n) { spyOld = o; spyNew = n; ++changes; });

  QVERIFY(reg.setActiveCharacterId(b->box_id));
  QCOMPARE(reg.activeCharacterId(), b->box_id);
  QCOMPARE(changes, 1);
  QCOMPARE(spyOld, a);
  QCOMPARE(spyNew, b);

  // Re-selecting the same box is a no-op (returns true, no new signal).
  QVERIFY(reg.setActiveCharacterId(b->box_id));
  QCOMPARE(changes, 1);

  // Unknown id fails.
  QVERIFY(!reg.setActiveCharacterId(QStringLiteral("b-nope")));
}

void BoxRegistryTest::lookupBoundZoneMatches()
{
  BoxRegistry reg;
  Box* a = reg.observe(kClientIp, kServerIp, cport(1000), kSrvPort, 1);
  a->zone_client_port       = cport(5555);
  a->zone_server_port_bound = cport(7000);

  QCOMPARE(reg.lookupBoundZone(kClientIp, cport(5555), cport(7000)), a);
  QVERIFY(reg.lookupBoundZone(kClientIp, cport(9999), cport(7000)) == nullptr);
}

void BoxRegistryTest::lookupBoundZoneFindsMergedLiveBox()
{
  // Regression: a zone-change/re-handshake box gets promoted+merged at
  // OP_PlayerProfile, which lands right after its zone session binds. The
  // routing lookups USED to skip merged boxes — so the live session became
  // unroutable the instant the character was identified, dropping the spawn
  // burst and ongoing position updates that follow the profile. (Symptom on
  // the primary box: name+zone decode once, then no spawns / frozen position,
  // never recovers; the freed session got grabbed by another same-host box.)
  // Identity grouping is by shared box_id; routing keys on the physical 5-tuple.
  BoxRegistry reg;
  Box* first = reg.observe(kClientIp, kServerIp, cport(1000), kSrvPort, 1);
  first->first_seen_ms = 1;
  reg.promoteByName(first, QStringLiteral("Alpha"));   // Alpha's parent box

  // Alpha zones: fresh world socket -> new box; its zone session binds.
  Box* zoned = reg.observe(kClientIp, kServerIp, cport(1001), kSrvPort, 2);
  zoned->first_seen_ms          = 2;
  zoned->zone_client_port       = cport(5555);
  zoned->zone_server_port_bound = cport(7000);
  QCOMPARE(reg.lookupBoundZone(kClientIp, cport(5555), cport(7000)), zoned);

  // OP_PlayerProfile -> promote folds the new box into Alpha (shared box_id).
  reg.promoteByName(zoned, QStringLiteral("Alpha"));
  QCOMPARE(zoned->box_id, first->box_id);              // same character
  QCOMPARE(reg.currentBoxFor(first->box_id), zoned);   // it IS the live box

  // The merged-but-live box must STILL route by its zone 5-tuple.
  QCOMPARE(reg.lookupBoundZone(kClientIp, cport(5555), cport(7000)), zoned);

  // And after a reused-socket re-zone (ZoneServerObserver clears the binding),
  // a merged box must remain re-bindable via the expected-zone path.
  zoned->zone_client_port          = 0;
  zoned->zone_server_port_bound    = 0;
  zoned->expected_zone_server_port = cport(8000);
  zoned->zone_await_ms             = 300;
  QCOMPARE(reg.lookupByExpectedZone(kClientIp, 0x01020304, cport(8000)), zoned);
}

void BoxRegistryTest::lookupByExpectedZoneRecencyAndSkipBound()
{
  BoxRegistry reg;
  Box* a = reg.observe(kClientIp, kServerIp, cport(1000), kSrvPort, 1);
  Box* b = reg.observe(kClientIp, kServerIp, cport(1001), kSrvPort, 2);

  // Both await a zone on the same server port (ZoneServerObserver defers IP
  // resolution, so expected_zone_server_ip stays 0 -> port-only match).
  a->expected_zone_server_port = cport(7000);
  a->zone_await_ms             = 100;
  b->expected_zone_server_port = cport(7000);
  b->zone_await_ms             = 200;            // notified more recently

  // The most recently notified box owns the SessionRequest arriving now.
  QCOMPARE(reg.lookupByExpectedZone(kClientIp, 0x01020304, cport(7000)), b);

  // Once b binds a live zone session it's skipped, so a wins.
  b->zone_client_port = cport(6666);
  QCOMPARE(reg.lookupByExpectedZone(kClientIp, 0x01020304, cport(7000)), a);
}

void BoxRegistryTest::anyZoneServerAnnouncedTracksFirstAnnouncement()
{
  BoxRegistry reg;
  QVERIFY(!reg.anyZoneServerAnnounced());          // empty registry

  Box* a = reg.observe(kClientIp, kServerIp, cport(1000), kSrvPort, 1);
  Box* b = reg.observe(kClientIp, kServerIp, cport(1001), kSrvPort, 2);
  QVERIFY(!reg.anyZoneServerAnnounced());          // boxes, no announcement

  // One announcement anywhere flips it: this is what separates "the opcode
  // has never decoded" from "it decodes, but missed this port".
  b->expected_zone_server_port = cport(7000);
  QVERIFY(reg.anyZoneServerAnnounced());

  // Binding a zone session must NOT clear it — ZoneServerObserver resets
  // zone_client_port on a re-zone but leaves expected_zone_server_port set,
  // which is what makes this derivable from box state instead of a counter.
  b->zone_client_port = cport(6666);
  QVERIFY(reg.anyZoneServerAnnounced());

  a->expected_zone_server_port = cport(7001);
  QVERIFY(reg.anyZoneServerAnnounced());
}

// --- Eviction (evictStale) -------------------------------------------------
//
// Helper: observe a world session for `name`, stamp its first_seen so
// currentBoxFor orders the group, and promote it. The 2nd+ session for a
// name merges into the first as a re-handshake alias — the way a client
// zoning produces one Box per zone for the same character.
namespace {
Box* addSession(BoxRegistry& reg, uint16_t port, const QString& name,
                qint64 firstSeen)
{
  Box* b = reg.observe(kClientIp, kServerIp, cport(port), kSrvPort, firstSeen);
  b->first_seen_ms = firstSeen;
  reg.promoteByName(b, name);
  return b;
}
}  // namespace

void BoxRegistryTest::evictStaleReapsSupersededAlias()
{
  qRegisterMetaType<Box*>("Box*");
  BoxRegistry reg;
  QSignalSpy spy(&reg, &BoxRegistry::boxAboutToBeRemoved);

  // Primary character (active by default) — must be protected.
  Box* prim = addSession(reg, 1000, QStringLiteral("Primary"), 1000);
  QVERIFY(prim->is_primary);

  // A second character with three world sessions: parent (zone 1), an old
  // superseded alias (zone 2), and the live box (zone 3).
  Box* p   = addSession(reg, 2000, QStringLiteral("Foo"), 2000);  // first zone
  Box* old = addSession(reg, 3000, QStringLiteral("Foo"), 3000);  // superseded
  Box* cur = addSession(reg, 4000, QStringLiteral("Foo"), 4000);  // live
  QCOMPARE(old->box_id, p->box_id);                // Foo's sessions share the id
  QCOMPARE(cur->box_id, p->box_id);
  QCOMPARE(reg.currentBoxFor(p->box_id), cur);
  QCOMPARE(reg.size(), size_t(4));
  const QString fooId = p->box_id;                 // capture before p is freed

  // Live box still talking; the two older zone sessions went silent at zone-out.
  const qint64 now = 1'000'000, ttl = 10'000;
  prim->last_seen_ms = now;            // active char fresh
  cur->last_seen_ms  = now;            // live box fresh
  p->last_seen_ms    = 2000;           // stale old-zone session -> reap
  old->last_seen_ms  = 3000;           // stale old-zone session -> reap

  const int n = reg.evictStale(now, ttl);

  // Both silent old-zone sessions are reclaimed; only the live box survives.
  // Identity lives in the Character store (keyed by the shared id), so the
  // character still resolves to its live session with no orphaned anchor box.
  QCOMPARE(n, 2);
  QCOMPARE(spy.count(), 2);
  QCOMPARE(qvariant_cast<Box*>(spy.at(0).at(0)), p);    // pointer-value compare
  QCOMPARE(qvariant_cast<Box*>(spy.at(1).at(0)), old);  // (no deref of freed Box)
  QCOMPARE(reg.size(), size_t(2));                 // Primary + Foo's live box
  QCOMPARE(reg.distinctCount(), size_t(2));        // Primary + Foo
  QCOMPARE(reg.currentBoxFor(fooId), cur);         // no orphan, live intact
}

void BoxRegistryTest::evictStaleReapsLoggedOffGroup()
{
  qRegisterMetaType<Box*>("Box*");
  BoxRegistry reg;
  QSignalSpy spy(&reg, &BoxRegistry::boxAboutToBeRemoved);

  Box* prim = addSession(reg, 1000, QStringLiteral("Primary"), 1000);
  Box* p    = addSession(reg, 2000, QStringLiteral("Gone"), 2000);  // parent
  Box* cur  = addSession(reg, 3000, QStringLiteral("Gone"), 3000);  // live box

  const qint64 now = 1'000'000, ttl = 10'000;
  prim->last_seen_ms = now;            // active char still here
  p->last_seen_ms    = 2000;           // whole "Gone" character went silent
  cur->last_seen_ms  = 3000;

  const int n = reg.evictStale(now, ttl);

  // The live box itself is stale => the character logged off => the whole
  // group (parent + alias) is reclaimed.
  QCOMPARE(n, 2);
  QCOMPARE(spy.count(), 2);
  QCOMPARE(reg.size(), size_t(1));                 // only Primary left
  QCOMPARE(reg.distinctCount(), size_t(1));
  QVERIFY(reg.findById(p->box_id) == nullptr);     // Gone is gone
}

void BoxRegistryTest::evictStaleProtectsPrimaryActiveFresh()
{
  BoxRegistry reg;

  Box* prim = addSession(reg, 1000, QStringLiteral("Primary"), 1000);
  Box* act  = addSession(reg, 2000, QStringLiteral("Active"), 2000);
  Box* fresh= addSession(reg, 3000, QStringLiteral("Fresh"), 3000);
  QVERIFY(reg.setActiveCharacterId(act->box_id));

  const qint64 now = 1'000'000, ttl = 10'000;
  prim->last_seen_ms  = 1000;          // primary stale...
  act->last_seen_ms   = 2000;          // active char stale...
  fresh->last_seen_ms = now;           // other char still talking

  // Primary is never reaped; the actively-viewed character is never reaped
  // even when idle; a still-fresh character is never reaped.
  QCOMPARE(reg.evictStale(now, ttl), 0);
  QCOMPARE(reg.size(), size_t(3));
}

void BoxRegistryTest::evictStaleDisabledAndNoVictims()
{
  BoxRegistry reg;
  Box* prim = addSession(reg, 1000, QStringLiteral("Primary"), 1000);
  Box* p    = addSession(reg, 2000, QStringLiteral("Foo"), 2000);
  prim->last_seen_ms = 1000;
  p->last_seen_ms    = 1000;
  const qint64 now = 1'000'000;

  // ttl <= 0 disables eviction outright.
  QCOMPARE(reg.evictStale(now, 0), 0);
  QCOMPARE(reg.size(), size_t(2));

  // Nothing stale enough yet (huge ttl) -> no-op, no changed() churn.
  QCOMPARE(reg.evictStale(now, 100'000'000), 0);
  QCOMPARE(reg.size(), size_t(2));
}

// --- Session state machine (character-refactor scaffold) -------------------

void BoxRegistryTest::stateStartsPendingUntilPromoted()
{
  BoxRegistry reg;
  Box* b = reg.observe(kClientIp, kServerIp, cport(1000), kSrvPort, 1);
  QCOMPARE(b->state, SessionState::Pending);     // observed, still anonymous
  reg.promoteByName(b, QStringLiteral("Alpha"));
  QCOMPARE(b->state, SessionState::Attached);     // name resolved -> live session
}

void BoxRegistryTest::stateAttachedSupersededOnRelog()
{
  BoxRegistry reg;
  Box* first = reg.observe(kClientIp, kServerIp, cport(1000), kSrvPort, 10);
  first->first_seen_ms = 10;
  reg.promoteByName(first, QStringLiteral("Alpha"));
  QCOMPARE(first->state, SessionState::Attached);

  // Alpha zones: a newer session promotes and takes over as the live one.
  Box* second = reg.observe(kClientIp, kServerIp, cport(1001), kSrvPort, 20);
  second->first_seen_ms = 20;
  reg.promoteByName(second, QStringLiteral("Alpha"));

  QCOMPARE(second->state, SessionState::Attached);    // newest is the live session
  QCOMPARE(first->state,  SessionState::Superseded);  // prior one handed off
  // The state annotation agrees with today's routing authority.
  QCOMPARE(reg.currentBoxFor(first->box_id), second);
}

void BoxRegistryTest::charactersViewAndCurrentSessionFor()
{
  BoxRegistry reg;
  // Alpha with a re-handshake (two sessions), plus a distinct Beta.
  Box* aP = reg.observe(kClientIp, kServerIp, cport(1000), kSrvPort, 10);
  aP->first_seen_ms = 10;
  reg.promoteByName(aP, QStringLiteral("Alpha"));
  Box* aZ = reg.observe(kClientIp, kServerIp, cport(1001), kSrvPort, 20);
  aZ->first_seen_ms = 20;
  reg.promoteByName(aZ, QStringLiteral("Alpha"));   // Alpha's live session now
  Box* bP = reg.observe(kClientIp, kServerIp, cport(1002), kSrvPort, 30);
  bP->first_seen_ms = 30;
  reg.promoteByName(bP, QStringLiteral("Beta"));

  std::vector<Character> chars = reg.characters();
  QCOMPARE(chars.size(), size_t(2));                 // two distinct identities

  const Character* alpha = nullptr;
  for (const auto& c : chars)
    if (c.name == QStringLiteral("Alpha")) alpha = &c;
  QVERIFY(alpha != nullptr);
  QCOMPARE(alpha->session, aZ);                       // live = newest zone session
  QCOMPARE(alpha->id, aP->box_id);                   // identity anchored to first
  QCOMPARE(alpha->aliasCount, 1);                    // one superseded re-handshake

  // The name-keyed primitive: pin to the character, not the last-zoned session.
  QCOMPARE(reg.currentSessionFor(QStringLiteral("Alpha")), aZ);
  QCOMPARE(reg.currentSessionFor(QStringLiteral("Beta")), bP);
  QVERIFY(reg.currentSessionFor(QStringLiteral("Nobody")) == nullptr);
  QVERIFY(reg.currentSessionFor(QString()) == nullptr);
}

QTEST_GUILESS_MAIN(BoxRegistryTest)
#include "boxregistry_test.moc"
