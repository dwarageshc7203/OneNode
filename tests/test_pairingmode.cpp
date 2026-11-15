#include "pairingmode.h"

#include <QtTest>
#include <QSignalSpy>

class TestPairingMode : public QObject {

    Q_OBJECT

private slots:

    void isClosedUntilTheUserOpensIt()
    {
        PairingMode mode;
        QVERIFY(!mode.active());
        QVERIFY(!mode.beginAttempt()); // nothing from the network can open it
    }

    void startingShowsAFreshPinEveryTime()
    {
        PairingMode mode;
        QSignalSpy started(&mode, &PairingMode :: started);
        QSet<int> pins;
        for (int i = 0; i < 20; ++i) {
            mode.start();
            QVERIFY(mode.active());
            QVERIFY(mode.pin() >= 0 && mode.pin() <= 999999);
            pins.insert(mode.pin());
        }
        QCOMPARE(started.count(), 20);
        QVERIFY(pins.size() > 15); // a new random PIN each time, never "the same one again"
    }

    void onlyOneSenderAtATime()
    {
        PairingMode mode;
        mode.start();
        QVERIFY(mode.beginAttempt());
        QVERIFY(!mode.beginAttempt());
        QVERIFY(mode.active());
    }

    void leavingBeforeTheExchangeFreesThePlaceAndKeepsThePin()
    {
        PairingMode mode;
        QSignalSpy ended(&mode, &PairingMode :: ended);
        mode.start();
        const int pin = mode.pin();
        QVERIFY(mode.beginAttempt());
        mode.attemptAbandoned();
        QVERIFY(mode.active());
        QCOMPARE(mode.pin(), pin);
        QCOMPARE(ended.count(), 0);
        QVERIFY(mode.beginAttempt()); // the next sender may come in
    }

    void leavingInTheMiddleBurnsThePin()
    {
        PairingMode mode;
        QSignalSpy ended(&mode, &PairingMode :: ended);
        mode.start();
        QVERIFY(mode.beginAttempt());
        mode.attemptStartedExchange();
        mode.attemptAbandoned();
        QVERIFY(!mode.active());
        QCOMPARE(mode.pin(), -1); // the PIN no longer exists anywhere
        QCOMPARE(ended.count(), 1);
        QCOMPARE(ended.first().first().toString(), QString("failed"));
        QVERIFY(!mode.beginAttempt());
    }

    void aFailedExchangeBurnsThePin()
    {
        PairingMode mode;
        QSignalSpy ended(&mode, &PairingMode :: ended);
        mode.start();
        QVERIFY(mode.beginAttempt());
        mode.attemptStartedExchange();
        mode.attemptFailed();
        QVERIFY(!mode.active());
        QCOMPARE(ended.first().first().toString(), QString("failed"));
    }

    void successClosesTheModeAndAnnouncesTheDevice()
    {
        PairingMode mode;
        QSignalSpy ended(&mode, &PairingMode :: ended);
        QSignalSpy paired(&mode, &PairingMode :: paired);
        mode.start();
        QVERIFY(mode.beginAttempt());
        mode.attemptStartedExchange();
        mode.attemptSucceeded(QString(64, 'a'), "Phone");
        QVERIFY(!mode.active());
        QCOMPARE(ended.first().first().toString(), QString("paired"));
        QCOMPARE(paired.count(), 1);
        QCOMPARE(paired.first().at(0).toString(), QString(64, 'a'));
        QCOMPARE(paired.first().at(1).toString(), QString("Phone"));
    }

    void cancellingClosesIt()
    {
        PairingMode mode;
        QSignalSpy ended(&mode, &PairingMode :: ended);
        mode.start();
        mode.cancel();
        QVERIFY(!mode.active());
        QCOMPARE(ended.first().first().toString(), QString("cancelled"));
        mode.cancel(); // closing a closed door is nothing
        QCOMPARE(ended.count(), 1);
    }

    void thePinExpires()
    {
        Protocol :: Timeouts timeouts;
        timeouts.pinLifetimeMs = 100;
        PairingMode mode(timeouts);
        QSignalSpy ended(&mode, &PairingMode :: ended);
        mode.start();
        QTRY_COMPARE_WITH_TIMEOUT(ended.count(), 1, 2000);
        QCOMPARE(ended.first().first().toString(), QString("expired"));
        QVERIFY(!mode.active());
        QCOMPARE(mode.pin(), -1);
    }

    void restartingGivesAFreshPinAndRestartsTheClock()
    {
        Protocol :: Timeouts timeouts;
        timeouts.pinLifetimeMs = 300;
        PairingMode mode(timeouts);
        QSignalSpy ended(&mode, &PairingMode :: ended);
        mode.start();
        QTest :: qWait(200);
        mode.start();          // the user pressed the button again
        QTest :: qWait(200);   // 400 ms after the first start, but only 200 after the second
        QVERIFY(mode.active());
        QCOMPARE(ended.count(), 0);
    }

    void callsInTheWrongStateDoNothing()
    {
        PairingMode mode;
        mode.attemptStartedExchange();
        mode.attemptFailed();
        mode.attemptSucceeded("x", "y");
        mode.attemptAbandoned();
        QVERIFY(!mode.active());
    }
};

QTEST_GUILESS_MAIN(TestPairingMode)
#include "test_pairingmode.moc"
