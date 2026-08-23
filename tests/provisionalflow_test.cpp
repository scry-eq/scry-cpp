#include "provisionalflow.h"

#include <QtTest>

namespace {

seq::shadow::BufferedApplicationPacket packet(
    uint64_t order, int bytes = 1, int64_t timestamp = 0,
    bool sourceIsLow = false)
{
    seq::shadow::BufferedApplicationPacket result;
    result.order = order;
    result.stream = uint8_t(zone2client);
    result.direction = DIR_Server;
    result.sourceIsLow = sourceIsLow;
    result.opcode = uint16_t(order);
    result.payload = QByteArray(bytes, char(order));
    result.timestamp = timestamp;
    return result;
}

EQPacketFlowKey key(uint64_t value)
{
    return EQPacketFlowKey{value, value + 1000};
}

} // namespace

class ProvisionalFlowTest : public QObject {
    Q_OBJECT

private slots:
    void storesExactOrderedMetadata();
    void keepsFlowsIsolated();
    void takeReleasesExactByteCharge();
    void packetLimitInvalidatesWholeFlow();
    void byteLimitInvalidatesWholeFlow();
    void oversizedPacketNeverLeavesSuffix();
    void seventeenthFlowEvictsLeastRecentlyUsed();
    void globalBudgetEvictsOldestOtherFlow();
    void touchingAFlowProtectsItFromEviction();
    void takeAllDrainsEveryFlow();
};

void ProvisionalFlowTest::storesExactOrderedMetadata()
{
    seq::shadow::ProvisionalPacketStore store;
    const EQPacketFlowKey flow = key(1);
    QVERIFY(store.append(flow, packet(1, 3, 90, true)).stored);
    QVERIFY(store.append(flow, packet(2, 5, 80, false)).stored);

    auto taken = store.take(flow);
    QVERIFY(taken.has_value());
    QVERIFY(taken->complete);
    QCOMPARE(taken->packets.size(), size_t(2));
    QCOMPARE(taken->packets[0].order, uint64_t(1));
    QCOMPARE(taken->packets[0].timestamp, int64_t(90));
    QVERIFY(taken->packets[0].sourceIsLow);
    QCOMPARE(taken->packets[0].payload, QByteArray(3, char(1)));
    QCOMPARE(taken->packets[1].order, uint64_t(2));
    QCOMPARE(taken->packets[1].timestamp, int64_t(80));
    QVERIFY(!taken->packets[1].sourceIsLow);
}

void ProvisionalFlowTest::keepsFlowsIsolated()
{
    seq::shadow::ProvisionalPacketStore store;
    QVERIFY(store.append(key(1), packet(11)).stored);
    QVERIFY(store.append(key(2), packet(22)).stored);

    auto first = store.take(key(1));
    QVERIFY(first.has_value());
    QCOMPARE(first->packets.front().order, uint64_t(11));
    const auto* second = store.find(key(2));
    QVERIFY(second);
    QCOMPARE(second->packets.front().order, uint64_t(22));
}

void ProvisionalFlowTest::takeReleasesExactByteCharge()
{
    seq::shadow::ProvisionalPacketStore store;
    QVERIFY(store.append(key(1), packet(1, 17)).stored);
    QVERIFY(store.append(key(2), packet(2, 29)).stored);
    const size_t before = store.totalBytes();
    auto first = store.take(key(1));
    QVERIFY(first.has_value());
    QCOMPARE(store.totalBytes(), before - first->bytes);
    QCOMPARE(store.size(), size_t(1));
}

void ProvisionalFlowTest::packetLimitInvalidatesWholeFlow()
{
    seq::shadow::ProvisionalPacketStore store;
    const EQPacketFlowKey flow = key(1);
    for (size_t i = 0; i < seq::shadow::ProvisionalPacketStore::MaxPacketsPerFlow;
         ++i)
        QVERIFY(store.append(flow, packet(i + 1)).stored);

    const auto result = store.append(flow, packet(999));
    QVERIFY(result.flowInvalidated);
    const auto* invalid = store.find(flow);
    QVERIFY(invalid);
    QVERIFY(!invalid->complete);
    QVERIFY(invalid->packets.empty());
    QCOMPARE(invalid->bytes, size_t(0));
}

void ProvisionalFlowTest::byteLimitInvalidatesWholeFlow()
{
    seq::shadow::ProvisionalPacketStore store;
    const EQPacketFlowKey flow = key(1);
    const int half = int(seq::shadow::ProvisionalPacketStore::MaxBytesPerFlow / 2);
    QVERIFY(store.append(flow, packet(1, half - 1024)).stored);
    const auto result = store.append(flow, packet(2, half + 1024));
    QVERIFY(result.flowInvalidated);
    QVERIFY(!store.find(flow)->complete);
    QCOMPARE(store.totalBytes(), size_t(0));
}

void ProvisionalFlowTest::oversizedPacketNeverLeavesSuffix()
{
    seq::shadow::ProvisionalPacketStore store;
    const EQPacketFlowKey flow = key(1);
    QVERIFY(store.append(flow, packet(1)).stored);
    const auto result = store.append(
        flow, packet(2, int(seq::shadow::ProvisionalPacketStore::MaxBytesPerFlow)));
    QVERIFY(result.flowInvalidated);
    QVERIFY(store.find(flow)->packets.empty());
    QVERIFY(!store.append(flow, packet(3)).stored);
    QVERIFY(store.find(flow)->packets.empty());
}

void ProvisionalFlowTest::seventeenthFlowEvictsLeastRecentlyUsed()
{
    seq::shadow::ProvisionalPacketStore store;
    for (size_t i = 1; i <= seq::shadow::ProvisionalPacketStore::MaxFlows; ++i)
        QVERIFY(store.append(key(i), packet(i)).stored);

    const auto result = store.append(key(17), packet(17));
    QVERIFY(result.stored);
    QCOMPARE(result.evicted.size(), size_t(1));
    QVERIFY(result.evicted.front().key == key(1));
    QVERIFY(!store.find(key(1)));
    QVERIFY(store.find(key(17)));
}

void ProvisionalFlowTest::globalBudgetEvictsOldestOtherFlow()
{
    seq::shadow::ProvisionalPacketStore store;
    const int chunk = 3 * 1024 * 1024;
    for (uint64_t i = 1; i <= 5; ++i)
        QVERIFY(store.append(key(i), packet(i, chunk)).stored);

    const auto result = store.append(key(6), packet(6, chunk));
    QVERIFY(result.stored);
    QVERIFY(!result.evicted.empty());
    QVERIFY(result.evicted.front().key == key(1));
    QVERIFY(store.totalBytes() <=
            seq::shadow::ProvisionalPacketStore::MaxTotalBytes);
}

void ProvisionalFlowTest::touchingAFlowProtectsItFromEviction()
{
    seq::shadow::ProvisionalPacketStore store;
    for (size_t i = 1; i <= seq::shadow::ProvisionalPacketStore::MaxFlows; ++i)
        QVERIFY(store.append(key(i), packet(i)).stored);
    QVERIFY(store.append(key(1), packet(100)).stored);

    const auto result = store.append(key(17), packet(17));
    QCOMPARE(result.evicted.size(), size_t(1));
    QVERIFY(result.evicted.front().key == key(2));
    QVERIFY(store.find(key(1)));
}

void ProvisionalFlowTest::takeAllDrainsEveryFlow()
{
    seq::shadow::ProvisionalPacketStore store;
    QVERIFY(store.append(key(1), packet(1)).stored);
    QVERIFY(store.append(key(2), packet(2)).stored);
    const auto all = store.takeAll();
    QCOMPARE(all.size(), size_t(2));
    QCOMPARE(store.size(), size_t(0));
    QCOMPARE(store.totalBytes(), size_t(0));
}

QTEST_APPLESS_MAIN(ProvisionalFlowTest)
#include "provisionalflow_test.moc"
