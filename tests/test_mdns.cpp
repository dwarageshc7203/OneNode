#include "fakemdns.h"

#include <QtTest>
#include <memory>

static const QString fpA = QString("a1").repeated(32);
static const QString fpB = QString("b2").repeated(32);
static const QString fpC = QString("c3").repeated(32);

class TestMdns : public QObject {
    Q_OBJECT

    qint64 now = 1000;
    FakeRoom room;
    std :: unique_ptr<FakeTransport> ta, tb, tc;
    std :: unique_ptr<MdnsService> a, b, c;

    std :: function<qint64()> clock() { return [this] { return now; }; }

    void advance (qint64 ms)   // time passes one second at a time, everybody ticks, packets fly
    {
        for (qint64 done = 0; done < ms; done += 1000) {
            now += 1000;
            for (MdnsService *s : { a.get(), b.get(), c.get() }) { if (s) { s -> tick(); } }
            room.pump();
        }
    }

    void makeA() { ta.reset(new FakeTransport(&room, QHostAddress("192.168.1.10"))); room.nodes << ta.get(); a.reset(new MdnsService(ta.get(), clock())); }
    void makeB() { tb.reset(new FakeTransport(&room, QHostAddress("192.168.1.20"))); room.nodes << tb.get(); b.reset(new MdnsService(tb.get(), clock())); }
    void makeC() { tc.reset(new FakeTransport(&room, QHostAddress("192.168.1.30"))); room.nodes << tc.get(); c.reset(new MdnsService(tc.get(), clock())); }

    bool startService (MdnsService *s, const QString &name, const QString &fp, quint16 port, bool pairing = false)
    {
        QString error;
        const bool ok = s -> start(name, fp, port, pairing, error);
        room.pump();
        return ok;
    }

    // a hand-made response, as a hostile or odd neighbour could send it
    static QByteArray response (const QString &instance, const QString &host, quint16 port, const QMap<QString, QString> &txt,
                                const QHostAddress &address, quint32 ttl = 120)
    {
        Dns :: Message m; m.response = true;
        Dns :: Record srv; srv.name = instance; srv.type = Dns :: TypeSRV; srv.ttl = ttl; srv.port = port; srv.target = host;
        Dns :: Record t; t.name = instance; t.type = Dns :: TypeTXT; t.ttl = ttl; t.txt = txt;
        Dns :: Record ad; ad.name = host; ad.type = Dns :: TypeA; ad.ttl = ttl; ad.address = address;
        m.answers << srv << t << ad;
        return Dns :: encode(m);
    }

private slots:

    void init()
    {
        now = 1000;
        room = FakeRoom();
        a.reset(); b.reset(); c.reset(); ta.reset(); tb.reset(); tc.reset();
    }

    void twoDevicesFindEachOther()
    {
        makeA(); makeB();
        QVERIFY(startService(a.get(), "Dwarag's laptop", fpA, 45454));
        QVERIFY(startService(b.get(), "Pixel 8", fpB, 45455));
        advance(2000);

        QCOMPARE(a -> peers().size(), 1);
        QCOMPARE(b -> peers().size(), 1);
        const MdnsPeer p = a -> peers().first();
        QCOMPARE(p.name, QString("Pixel 8"));
        QCOMPARE(p.fingerprint, fpB);
        QCOMPARE(p.port, quint16(45455));
        QCOMPARE(p.address(), QHostAddress("192.168.1.20"));
        QVERIFY(!p.pairing);
        QCOMPARE(b -> peers().first().name, QString("Dwarag's laptop"));
        QCOMPARE(b -> peers().first().address(), QHostAddress("192.168.1.10"));
    }

    void aDeviceNeverSeesItself()
    {
        makeA();
        QVERIFY(startService(a.get(), "Solo", fpA, 45454)); // the room echoes our own packets back
        advance(5000);
        QCOMPARE(a -> peers().size(), 0);
    }

    void aLateComerFindsDevicesAlreadyThere()
    {
        makeA();
        QVERIFY(startService(a.get(), "Early", fpA, 45454));
        advance(30000);
        makeB();
        QVERIFY(startService(b.get(), "Late", fpB, 45455));
        advance(2000);
        QCOMPARE(b -> peers().size(), 1);
        QCOMPARE(b -> peers().first().name, QString("Early"));
        QCOMPARE(a -> peers().size(), 1); // and the early one hears the late one's announcement
    }

    void peersChangedIsEmittedOnlyOnRealChanges()
    {
        makeA(); makeB();
        QSignalSpy spy(a.get(), &MdnsService :: peersChanged);
        startService(a.get(), "A", fpA, 1);
        startService(b.get(), "B", fpB, 2);
        advance(3000);
        QCOMPARE(spy.count(), 1);   // B appeared: once
        advance(60000);             // a minute of refreshes changes nothing visible
        QCOMPARE(spy.count(), 1);
    }

    void goodbyeRemovesADeviceAtOnce()
    {
        makeA(); makeB();
        startService(a.get(), "A", fpA, 1);
        startService(b.get(), "B", fpB, 2);
        advance(2000);
        QCOMPARE(a -> peers().size(), 1);
        b -> stop();
        room.pump();
        QCOMPARE(a -> peers().size(), 0);
    }

    void aDeviceThatVanishesExpiresAfterItsTtl()
    {
        makeA(); makeB();
        startService(a.get(), "A", fpA, 1);
        startService(b.get(), "B", fpB, 2);
        advance(2000);
        QCOMPARE(a -> peers().size(), 1);
        tb -> reachable = false;     // the battery died, no goodbye
        advance(100000);
        QCOMPARE(a -> peers().size(), 1);   // 100 s: still inside the 120 s lifetime
        advance(30000);
        QCOMPARE(a -> peers().size(), 0);
    }

    void pairingModeIsAnnounced()
    {
        makeA(); makeB();
        startService(a.get(), "A", fpA, 1);
        startService(b.get(), "B", fpB, 2);
        advance(2000);
        QVERIFY(!a -> peers().first().pairing);
        b -> setPairing(true);
        room.pump();
        QVERIFY(a -> peers().first().pairing);
        b -> setPairing(false);
        room.pump();
        QVERIFY(!a -> peers().first().pairing);
    }

    void renamingIsAnnounced()
    {
        makeA(); makeB();
        startService(a.get(), "A", fpA, 1);
        startService(b.get(), "Old name", fpB, 2);
        advance(2000);
        b -> setDeviceName("New name");
        room.pump();
        QCOMPARE(a -> peers().first().name, QString("New name"));
    }

    void threeDevicesAreSortedByName()
    {
        makeA(); makeB(); makeC();
        startService(a.get(), "A", fpA, 1);
        startService(b.get(), "zeta", fpB, 2);
        startService(c.get(), "Alpha", fpC, 3);
        advance(3000);
        QCOMPARE(a -> peers().size(), 2);
        QCOMPARE(a -> peers().at(0).name, QString("Alpha"));
        QCOMPARE(a -> peers().at(1).name, QString("zeta"));
    }

    void startFailsCleanlyWithoutANetwork()
    {
        ta.reset(new FakeTransport(&room, QHostAddress("192.168.1.10"), true));
        room.nodes << ta.get();
        a.reset(new MdnsService(ta.get(), clock()));
        QString error;
        QVERIFY(!a -> start("A", fpA, 45454, false, error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!a -> running());
    }

    void startRefusesBadInput()
    {
        makeA();
        QString error;
        QVERIFY(!a -> start("A", fpA, 0, false, error));
        QVERIFY(!a -> start("A", "not a fingerprint", 45454, false, error));
    }

    void longNamesAreShortenedToFitOneTxtEntry()
    {
        makeA(); makeB();
        startService(a.get(), "A", fpA, 1);
        startService(b.get(), QString(64, QChar(0x20AC)), fpB, 2);  // 64 euro signs = 192 bytes
        advance(2000);
        QCOMPARE(a -> peers().size(), 1);
        QVERIFY(!a -> peers().first().name.isEmpty());
        // and a name made of 4-byte characters (64 of them = 256 bytes) must not break the packet either
        b -> setDeviceName(QString("\U0001F600").repeated(64));
        room.pump();
        QCOMPARE(a -> peers().size(), 1);
    }

    // ---------- the neighbours cannot be trusted ----------

    void hostileOrOddRecordsAreIgnored_data()
    {
        QTest::addColumn<QByteArray>("packet");
        const QString inst = "OneNode-deadbeef._onenode._tcp.local";
        const QString host = "onenode-deadbeef.local";
        const QMap<QString, QString> good { { "v", "2" }, { "fp", fpC }, { "name", "Evil" } };
        auto with = [&] (const QString &k, const QString &v) { auto m = good; m.insert(k, v); return m; };

        QTest::newRow("wrong protocol version") << response(inst, host, 1, with("v", "9"), QHostAddress("192.168.1.99"));
        QTest::newRow("invalid fingerprint") << response(inst, host, 1, with("fp", "xyz"), QHostAddress("192.168.1.99"));
        QTest::newRow("uppercase fingerprint") << response(inst, host, 1, with("fp", fpC.toUpper()), QHostAddress("192.168.1.99"));
        QTest::newRow("port zero") << response(inst, host, 0, good, QHostAddress("192.168.1.99"));
        QTest::newRow("not our service") << response("x._other._tcp.local", host, 1, good, QHostAddress("192.168.1.99"));
        QTest::newRow("claims to be us") << response(inst, host, 1, with("fp", fpA), QHostAddress("192.168.1.99"));
    }

    void hostileOrOddRecordsAreIgnored()
    {
        QFETCH(QByteArray, packet);
        makeA();
        startService(a.get(), "A", fpA, 45454);
        room.inject(packet, QHostAddress("192.168.1.99"));
        room.pump();
        QCOMPARE(a -> peers().size(), 0);
    }

    // A record that points at 127.0.0.1 or a multicast address is dropped. The device may still be listed,
    // but only at the address its packet really came from, never at the address it claimed.
    void unusableAddressesAreNeverUsed_data()
    {
        QTest::addColumn<QString>("claimed");
        QTest::newRow("loopback") << "127.0.0.1";
        QTest::newRow("multicast") << "224.0.0.251";
        QTest::newRow("any") << "0.0.0.0";
        QTest::newRow("broadcast") << "255.255.255.255";
    }

    void unusableAddressesAreNeverUsed()
    {
        QFETCH(QString, claimed);
        makeA();
        startService(a.get(), "A", fpA, 45454);
        room.inject(response("OneNode-deadbeef._onenode._tcp.local", "onenode-deadbeef.local", 1,
                             { { "v", "2" }, { "fp", fpC }, { "name", "Evil" } }, QHostAddress(claimed)),
                    QHostAddress("192.168.1.99"));
        room.pump();
        for (const MdnsPeer &peer : a -> peers()) {
            QVERIFY(!peer.addresses.contains(QHostAddress(claimed)));
            QCOMPARE(peer.address(), QHostAddress("192.168.1.99"));
        }
    }

    void aGoodHandMadeRecordIsAccepted_soTheTestsAboveProveSomething()
    {
        makeA();
        startService(a.get(), "A", fpA, 45454);
        room.inject(response("OneNode-deadbeef._onenode._tcp.local", "onenode-deadbeef.local", 4242,
                             { { "v", "2" }, { "fp", fpC }, { "name", "  Hand\tmade\n" } }, QHostAddress("192.168.1.99")),
                    QHostAddress("192.168.1.99"));
        room.pump();
        QCOMPARE(a -> peers().size(), 1);
        QCOMPARE(a -> peers().first().name, QString("Hand made")); // control characters are cleaned
        QCOMPARE(a -> peers().first().port, quint16(4242));
    }

    void aSenderWithoutAnAddressRecordIsReachedAtItsPacketSource()
    {
        makeA();
        startService(a.get(), "A", fpA, 45454);
        Dns :: Message m; m.response = true;
        Dns :: Record srv; srv.name = "OneNode-deadbeef._onenode._tcp.local"; srv.type = Dns :: TypeSRV; srv.ttl = 120; srv.port = 7; srv.target = "nohost.local";
        Dns :: Record t; t.name = srv.name; t.type = Dns :: TypeTXT; t.ttl = 120; t.txt = { { "v", "2" }, { "fp", fpC }, { "name", "NoA" } };
        m.answers << srv << t;
        room.inject(Dns :: encode(m), QHostAddress("192.168.1.77"));
        room.pump();
        QCOMPARE(a -> peers().size(), 1);
        QCOMPARE(a -> peers().first().address(), QHostAddress("192.168.1.77"));
    }

    void garbageIsDroppedWithoutHarm()
    {
        makeA();
        startService(a.get(), "A", fpA, 45454);
        for (int i = 0; i < 300; ++i) {
            room.inject(QByteArray(i % 50 + 1, char(i)), QHostAddress("192.168.1.99"));
        }
        room.pump();
        QVERIFY(a -> running());
        QCOMPARE(a -> peers().size(), 0);
    }

    void aFloodOfFakeDevicesIsCapped()
    {
        makeA();
        startService(a.get(), "A", fpA, 45454);
        for (int i = 0; i < 400; ++i) {
            const QString id = QString("%1").arg(i, 8, 16, QChar('0'));
            const QString fp = QString("%1").arg(i + 1, 64, 16, QChar('0'));
            room.inject(response("OneNode-" + id + "._onenode._tcp.local", "onenode-" + id + ".local", 1,
                                 { { "v", "2" }, { "fp", fp }, { "name", "F" + id } }, QHostAddress(QString("192.168.2.%1").arg(i % 250 + 1))),
                        QHostAddress("192.168.1.99"));
            if (i % 100 == 99) { advance(1000); } // a new second: the per-second packet cap starts over
            else { room.pump(); }
        }
        QVERIFY(a -> peers().size() <= MdnsService :: maxPeers);
        QVERIFY(a -> peers().size() > 0);
    }

    void aPacketFloodInOneSecondIsLimited()
    {
        makeA();
        startService(a.get(), "A", fpA, 45454);
        for (int i = 0; i < MdnsService :: maxPacketsPerSecond + 200; ++i) {
            const QString id = QString("%1").arg(i, 8, 16, QChar('0'));
            const QString fp = QString("%1").arg(i + 1, 64, 16, QChar('0'));
            room.inject(response("OneNode-" + id + "._onenode._tcp.local", "onenode-" + id + ".local", 1,
                                 { { "v", "2" }, { "fp", fp }, { "name", "F" } }, QHostAddress("192.168.2.1")),
                        QHostAddress("192.168.1.99"));
        }
        room.pump();
        QVERIFY(a -> peers().size() <= MdnsService :: maxPacketsPerSecond);
        QVERIFY(a -> peers().size() <= MdnsService :: maxPeers);
    }

    void repeatedQueriesGetAtMostOneAnswerPerSecond()
    {
        makeA();
        startService(a.get(), "A", fpA, 1);
        advance(5000);
        const int before = ta -> sent.size();
        Dns :: Message q; Dns :: Question qu; qu.name = MdnsService :: serviceName; qu.type = Dns :: TypePTR; q.questions << qu;
        const QByteArray packet = Dns :: encode(q);
        for (int i = 0; i < 10; ++i) { room.inject(packet, QHostAddress("192.168.1.99")); }
        room.pump();
        QCOMPARE(ta -> sent.size() - before, 1);
        now += 1500;
        room.inject(packet, QHostAddress("192.168.1.99"));
        room.pump();
        QCOMPARE(ta -> sent.size() - before, 2);
    }

    void queriesForOtherThingsGetNoAnswer()
    {
        makeA();
        startService(a.get(), "A", fpA, 1);
        advance(3000);
        const int before = ta -> sent.size();
        Dns :: Message q; Dns :: Question qu; qu.name = "_printer._tcp.local"; qu.type = Dns :: TypePTR; q.questions << qu;
        room.inject(Dns :: encode(q), QHostAddress("192.168.1.99"));
        room.pump();
        QCOMPARE(ta -> sent.size(), before);
    }

    void ourOwnPacketsAreWellFormedDnsSd()
    {
        makeA();
        startService(a.get(), "My PC", fpA, 45454, true);
        Dns :: Message m;
        QVERIFY(Dns :: decode(ta -> sent.first(), m));       // the first thing sent is the announcement
        QVERIFY(m.response);
        QCOMPARE(m.answers.size(), 4);
        QCOMPARE(m.answers.at(0).type, Dns :: TypePTR);
        QCOMPARE(m.answers.at(0).name, QString("_onenode._tcp.local"));
        QCOMPARE(m.answers.at(0).target, QString("OneNode-%1._onenode._tcp.local").arg(fpA.left(8)));
        QCOMPARE(m.answers.at(1).type, Dns :: TypeSRV);
        QCOMPARE(m.answers.at(1).port, quint16(45454));
        QCOMPARE(m.answers.at(2).txt.value("v"), QString("2"));
        QCOMPARE(m.answers.at(2).txt.value("fp"), fpA);
        QCOMPARE(m.answers.at(2).txt.value("name"), QString("My PC"));
        QCOMPARE(m.answers.at(2).txt.value("pair"), QString("1"));
        QCOMPARE(m.answers.at(3).address, QHostAddress("192.168.1.10"));
        QCOMPARE(m.answers.at(0).ttl, MdnsService :: recordTtl);
        // and the second packet is the query that looks for others
        QVERIFY(Dns :: decode(ta -> sent.at(1), m));
        QVERIFY(!m.response);
        QCOMPARE(m.questions.first().name, QString("_onenode._tcp.local"));
        QCOMPARE(m.questions.first().type, Dns :: TypePTR);
    }

    void queriesBackOffOverTime()
    {
        makeA();
        startService(a.get(), "A", fpA, 1);
        advance(120000);
        int queries = 0;
        for (const QByteArray &p : ta -> sent) {
            Dns :: Message m;
            if (Dns :: decode(p, m) && !m.response) { ++queries; }
        }
        // 1, 2, 4, 8, 16, 32, 60 ... : about 8 in two minutes, nowhere near 120
        QVERIFY2(queries >= 6 && queries <= 12, qPrintable(QString :: number(queries)));
    }

    void stopThenStartAgainWorks()
    {
        makeA(); makeB();
        startService(a.get(), "A", fpA, 1);
        startService(b.get(), "B", fpB, 2);
        advance(2000);
        a -> stop();
        room.pump();
        QCOMPARE(a -> peers().size(), 0);
        QVERIFY(startService(a.get(), "A", fpA, 1));
        advance(2000);
        QCOMPARE(a -> peers().size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestMdns)
#include "test_mdns.moc"
