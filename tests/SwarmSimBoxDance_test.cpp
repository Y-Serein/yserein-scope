#include <QtTest/QTest>
#include <cmath>
#include "../src/plugins/yserein-swarm-sim/Frontend/MockVehicle/BoxDancePlan.h"

class SwarmSimBoxDanceTest : public QObject {
    Q_OBJECT
private slots:
    void completeSequence();
    void unevenColumns();
    void twentyThousandBoundary();
};

static bool near(double actual, double expected)
{
    return std::abs(actual - expected) < 0.001;
}

void SwarmSimBoxDanceTest::completeSequence()
{
    BoxDancePlan plan;
    plan.aircraftCount = 144;
    QCOMPARE(plan.boxCount(), 12);
    QCOMPARE(plan.minLayers(), 12);
    QCOMPARE(plan.maxLayers(), 12);
    QVERIFY(near(plan.position(0, 0).z(), 0));
    QVERIFY(near(plan.position(0, plan.waveSeconds()).z(), 2));
    QVERIFY(near(plan.position(12, plan.waveSeconds()).z(), 0));
    QVERIFY(near(plan.position(0, plan.launchSeconds()).z(), 24));
    QVERIFY(near(plan.position(132, plan.launchSeconds()).z(), 2));

    const auto home = plan.position(0, 0);
    QVERIFY(near(plan.position(0, plan.launchSeconds() + plan.travelSeconds()).x(),
                 home.x() + 50));
    QVERIFY(near(plan.position(0, plan.launchSeconds() + 2 * plan.travelSeconds()).x(),
                 home.x()));
    QVERIFY(near(plan.position(0, plan.durationSeconds()).z(), 0));
    QVERIFY(near(plan.position(132, plan.durationSeconds()).z(), 0));
}

void SwarmSimBoxDanceTest::unevenColumns()
{
    BoxDancePlan plan;
    plan.aircraftCount = 13;
    QCOMPARE(plan.minLayers(), 1);
    QCOMPARE(plan.maxLayers(), 2);
    QCOMPARE(plan.layersInBox(0), 2);
    QCOMPARE(plan.layersInBox(1), 1);
    QVERIFY(near(plan.position(0, plan.launchSeconds()).z(), 4));
    QVERIFY(near(plan.position(1, plan.launchSeconds()).z(), 2));
    QVERIFY(near(plan.position(12, plan.launchSeconds()).z(), 2));
    QVERIFY(near(plan.position(0, plan.durationSeconds()).z(), 0));
}

void SwarmSimBoxDanceTest::twentyThousandBoundary()
{
    BoxDancePlan plan;
    plan.aircraftCount = 20000;
    QCOMPARE(plan.minLayers(), 1666);
    QCOMPARE(plan.maxLayers(), 1667);
    QVERIFY(plan.durationSeconds() > 0);
    QVERIFY(near(plan.position(19999, plan.durationSeconds()).z(), 0));
    QVERIFY(near(plan.position(19999, plan.launchSeconds()).z(), 2));
}

QTEST_MAIN(SwarmSimBoxDanceTest)
#include "SwarmSimBoxDance_test.moc"
