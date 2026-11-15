#include "pairingengine.h"
#include "protocol.h"

#include <QtTest>

using Engine = PairingEngine;

// Runs a whole exchange between a sender engine and a receiver engine by handing each one's messages to the other.
// Returns the round in which it broke off (0..19), or -1 when both reached Done.
static int runExchange (Engine &sender, Engine &receiver)
{
    for (int guard = 0; guard < 200; ++guard) {
        // let whoever has something to send, send it
        for (Engine *from : { &sender, &receiver }) {
            Engine *to = (from == &sender) ? &receiver : &sender;
            const Engine :: Action a = from -> action();
            if (a == Engine :: Action :: SendCommit || a == Engine :: Action :: SendOpen) {
                const QJsonObject message = from -> makeMessage();
                const int round = message.value("round").toInt();
                if (!to -> accept(message)) {
                    return round;
                }
                break; // one message at a time, then look again (keeps the strict order)
            }
        }
        if (sender.action() == Engine :: Action :: Done && receiver.action() == Engine :: Action :: Done) {
            return -1;
        }
    }
    return 99; // stuck
}

static QByteArray fp (char c) { return QByteArray(32, c); }

class TestPairingEngine : public QObject {

    Q_OBJECT

private slots:

    // ---- the test vectors written in PROTOCOL.md section 5.5 ----
    void testVectors()
    {
        // fixed nonces: sender uses byte i (16 times), receiver uses byte 0x80+i (16 times)
        Engine sender(Engine :: Role :: Sender, 482913, fp(0x11), fp(0x22), [] (int i) { return QByteArray(16, static_cast<char>(i)); });
        Engine receiver(Engine :: Role :: Receiver, 482913, fp(0x11), fp(0x22), [] (int i) { return QByteArray(16, static_cast<char>(0x80 + i)); });

        const QMap<int, QPair<QString, QString>> expected = {
            { 0,  { "04c1fd647242b4d7369c627a7f2e1931432e9a8dcd1d4012f0b70aefed833bf0", "cdd47beb0cabc581177bae31264da90c001191aff0bf363b73e6fe5e38a9366a" } },
            { 1,  { "9e00bdc6f21e1212c88a9ef586e27c08e2e31985b9a7e5a2e71a8ee995fca376", "e0638040a1d70cb798fe3d2e6080e154d38f9a289483a3d7c4179554167e6895" } },
            { 19, { "4554b6c5592979e5e9ab1678ceb60064c945f2f2d9ade66b981a57f03861114c", "1d79bf59f8f49c6f264523393b0d7c10aa05776480970afdee29dc3b05d018cd" } },
        };

        for (int round = 0; round < Engine :: rounds; ++round) {
            QCOMPARE(sender.action(), Engine :: Action :: SendCommit);
            const QJsonObject commitS = sender.makeMessage();
            QVERIFY(receiver.accept(commitS));

            QCOMPARE(receiver.action(), Engine :: Action :: SendCommit);
            const QJsonObject commitR = receiver.makeMessage();
            QVERIFY(sender.accept(commitR));

            QCOMPARE(sender.action(), Engine :: Action :: SendOpen);
            const QJsonObject openS = sender.makeMessage();
            QVERIFY(receiver.accept(openS));

            QCOMPARE(receiver.action(), Engine :: Action :: SendOpen);
            const QJsonObject openR = receiver.makeMessage();
            QVERIFY(sender.accept(openR));

            if (expected.contains(round)) {
                QCOMPARE(commitS.value("commit").toString(), expected.value(round).first);
                QCOMPARE(commitR.value("commit").toString(), expected.value(round).second);
                QCOMPARE(openS.value("nonce").toString(), QString :: fromLatin1(QByteArray(16, static_cast<char>(round)).toHex()));
                QCOMPARE(openR.value("nonce").toString(), QString :: fromLatin1(QByteArray(16, static_cast<char>(0x80 + round)).toHex()));
            }
        }
        QCOMPARE(sender.action(), Engine :: Action :: Done);
        QCOMPARE(receiver.action(), Engine :: Action :: Done);
    }

    // ---- the happy path and the failures ----
    void samePinPairs()
    {
        Engine sender(Engine :: Role :: Sender, 123456, fp(1), fp(2));
        Engine receiver(Engine :: Role :: Receiver, 123456, fp(1), fp(2));
        QCOMPARE(runExchange(sender, receiver), -1);
    }

    void edgePinsPair()
    {
        for (int pin : { 0, 1, 999999, 524288 /* only the top bit set among 20 */ }) {
            Engine sender(Engine :: Role :: Sender, pin, fp(1), fp(2));
            Engine receiver(Engine :: Role :: Receiver, pin, fp(1), fp(2));
            QCOMPARE(runExchange(sender, receiver), -1);
        }
    }

    void aWrongPinNeverGetsThrough()
    {
        // 300 different wrong PINs: none may complete all 20 rounds, and each must break at the first differing bit
        for (int i = 1; i <= 300; ++i) {
            const int wrong = (482913 + i * 3331) % 1000000;
            Engine sender(Engine :: Role :: Sender, wrong, fp(1), fp(2));
            Engine receiver(Engine :: Role :: Receiver, 482913, fp(1), fp(2));
            const int round = runExchange(sender, receiver);
            QVERIFY2(round >= 0 && round < 20, qPrintable(QString("pin %1 got through").arg(wrong)));

            // the first round whose bit differs
            int firstDifferent = 0;
            while (((wrong >> (19 - firstDifferent)) & 1) == ((482913 >> (19 - firstDifferent)) & 1)) {
                ++firstDifferent;
            }
            QCOMPARE(round, firstDifferent);
            QCOMPARE(sender.action() == Engine :: Action :: Failed || receiver.action() == Engine :: Action :: Failed, true);
        }
    }

    void aDeviceInTheMiddleWithItsOwnCertificateFailsInRoundZeroEvenWithTheRightPin()
    {
        // the sender thinks it talks to fp(9) (the attacker's cert), the receiver thinks it is talked to by fp(8) (the attacker's other cert)
        Engine sender(Engine :: Role :: Sender, 482913, fp(1), fp(9));
        Engine receiver(Engine :: Role :: Receiver, 482913, fp(8), fp(2));
        QCOMPARE(runExchange(sender, receiver), 0);
    }

    void differentViewOfJustOneFingerprintFails()
    {
        Engine sender(Engine :: Role :: Sender, 777, fp(1), fp(2));
        Engine receiver(Engine :: Role :: Receiver, 777, fp(1), fp(3));
        QCOMPARE(runExchange(sender, receiver), 0);
    }

    // ---- rules about the messages themselves ----
    void wrongOrderIsRefused()
    {
        Engine receiver(Engine :: Role :: Receiver, 5, fp(1), fp(2));
        QCOMPARE(receiver.action(), Engine :: Action :: ExpectCommit);

        // an "open" while a "commit" is expected
        QJsonObject open;
        open["v"] = Protocol :: version; open["type"] = "pair_open"; open["round"] = 0; open["nonce"] = QString(32, 'a');
        QVERIFY(!receiver.accept(open));
        QCOMPARE(receiver.action(), Engine :: Action :: Failed);
    }

    void wrongRoundIsRefused()
    {
        Engine sender(Engine :: Role :: Sender, 5, fp(1), fp(2));
        Engine receiver(Engine :: Role :: Receiver, 5, fp(1), fp(2));
        QJsonObject commit = sender.makeMessage();
        commit["round"] = 1; // claims to be round 1 in round 0
        QVERIFY(!receiver.accept(commit));
    }

    void aReplayedMessageIsRefused()
    {
        Engine sender(Engine :: Role :: Sender, 5, fp(1), fp(2));
        Engine receiver(Engine :: Role :: Receiver, 5, fp(1), fp(2));
        const QJsonObject commit = sender.makeMessage();
        QVERIFY(receiver.accept(commit));
        // the same commit again, while the receiver now has to SEND
        QVERIFY(!receiver.accept(commit));
    }

    void badFormatsAreRefused_data()
    {
        QTest :: addColumn<QString>("field");
        QTest :: addColumn<QJsonValue>("value");
        QTest :: newRow("commit too short")  << "commit" << QJsonValue(QString(63, 'a'));
        QTest :: newRow("commit uppercase")  << "commit" << QJsonValue(QString(64, 'A'));
        QTest :: newRow("commit not hex")    << "commit" << QJsonValue(QString(64, 'g'));
        QTest :: newRow("commit a number")   << "commit" << QJsonValue(5);
        QTest :: newRow("type wrong")        << "type" << QJsonValue("pair_open");
        QTest :: newRow("version wrong")     << "v" << QJsonValue(1);
        QTest :: newRow("round a string")    << "round" << QJsonValue("0");
        QTest :: newRow("round fraction")    << "round" << QJsonValue(0.5);
    }

    void badFormatsAreRefused()
    {
        QFETCH(QString, field);
        QFETCH(QJsonValue, value);

        Engine sender(Engine :: Role :: Sender, 5, fp(1), fp(2));
        Engine receiver(Engine :: Role :: Receiver, 5, fp(1), fp(2));
        QJsonObject commit = sender.makeMessage();
        commit[field] = value;
        QVERIFY(!receiver.accept(commit));
        QCOMPARE(receiver.action(), Engine :: Action :: Failed);
    }

    void aFailedEngineStaysFailed()
    {
        Engine receiver(Engine :: Role :: Receiver, 5, fp(1), fp(2));
        QVERIFY(!receiver.accept(QJsonObject()));
        QCOMPARE(receiver.action(), Engine :: Action :: Failed);
        QVERIFY(receiver.makeMessage().isEmpty());
        QVERIFY(!receiver.accept(QJsonObject()));
    }

    void makeMessageAtTheWrongMomentGivesNothing()
    {
        Engine receiver(Engine :: Role :: Receiver, 5, fp(1), fp(2)); // must receive first
        QVERIFY(receiver.makeMessage().isEmpty());
        QCOMPARE(receiver.action(), Engine :: Action :: ExpectCommit); // and nothing moved
    }

    void impossibleSetupStartsFailed()
    {
        QCOMPARE(Engine(Engine :: Role :: Sender, -1, fp(1), fp(2)).action(), Engine :: Action :: Failed);
        QCOMPARE(Engine(Engine :: Role :: Sender, 1000000, fp(1), fp(2)).action(), Engine :: Action :: Failed);
        QCOMPARE(Engine(Engine :: Role :: Sender, 5, QByteArray(31, 1), fp(2)).action(), Engine :: Action :: Failed);
    }

    void nonceIsFreshEveryRoundAndEveryExchange()
    {
        Engine a(Engine :: Role :: Sender, 5, fp(1), fp(2));
        Engine b(Engine :: Role :: Sender, 5, fp(1), fp(2));
        // same PIN, same fingerprints, same round: still different commits, because the nonce is random
        QVERIFY(a.makeMessage().value("commit").toString() != b.makeMessage().value("commit").toString());
    }

    // ---- PIN helpers ----
    void pinFormattingAndParsing()
    {
        QCOMPARE(Engine :: formatPin(482913), QString("482 913"));
        QCOMPARE(Engine :: formatPin(7), QString("000 007"));

        int pin = -1;
        QVERIFY(Engine :: parsePin("482913", pin));    QCOMPARE(pin, 482913);
        QVERIFY(Engine :: parsePin("482 913", pin));   QCOMPARE(pin, 482913);
        QVERIFY(Engine :: parsePin("000007", pin));    QCOMPARE(pin, 7);
        QVERIFY(!Engine :: parsePin("48291", pin));
        QVERIFY(!Engine :: parsePin("4829133", pin));
        QVERIFY(!Engine :: parsePin("48291a", pin));
        QVERIFY(!Engine :: parsePin("", pin));
        QVERIFY(!Engine :: parsePin("-12345", pin));
    }

    void generatedPinsAreInRangeAndVary()
    {
        QSet<int> seen;
        for (int i = 0; i < 2000; ++i) {
            const int pin = Engine :: generatePin();
            QVERIFY(pin >= 0 && pin <= Engine :: maxPin);
            seen.insert(pin);
        }
        QVERIFY(seen.size() > 1900); // 2000 draws from 1 000 000: almost no repeats
    }

    void fingerprintBytesFromHex()
    {
        QCOMPARE(Engine :: fingerprintBytes(QString(64, 'a')), QByteArray(32, '\xaa'));
        QCOMPARE(Engine :: fingerprintBytes(QString(64, 'A')), QByteArray(32, '\xaa')); // case does not matter
        QVERIFY(Engine :: fingerprintBytes(QString(63, 'a')).isEmpty());
        QVERIFY(Engine :: fingerprintBytes(QString(64, 'x')).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestPairingEngine)
#include "test_pairingengine.moc"
