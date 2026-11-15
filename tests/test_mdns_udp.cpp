#include "mdnsservice.h"

#include <QtTest>

// The real multicast socket. It needs a real network connection that allows multicast, so it SKIPS (not fails)
// on machines without one (some build servers, containers).
class TestMdnsUdp : public QObject {
    Q_OBJECT
private slots:
    void twoServicesInOneProcessFindEachOtherOverRealMulticast()
    {
        if (UdpMulticastTransport :: usableInterfaces().isEmpty()) {
            QSKIP("no network connection with multicast on this machine");
        }
        const QString fpA = QString("a1").repeated(32), fpB = QString("b2").repeated(32);
        UdpMulticastTransport ta, tb;
        MdnsService a(&ta), b(&tb);
        QString error;
        if (!a.start("Real A", fpA, 45454, false, error)) {
            QSKIP(qPrintable("cannot use multicast here: " + error));
        }
        QVERIFY2(b.start("Real B", fpB, 45455, true, error), qPrintable(error));
        // Some build servers (virtual networks) accept multicast packets but never deliver them back.
        // There the workflows set ONENODE_MULTICAST_TEST=optional, so the test skips instead of failing.
        // On a developer machine the variable is not set, and the test must pass.
        const bool optional = qgetenv("ONENODE_MULTICAST_TEST") == "optional";
        if (optional) {
            for (int waited = 0; waited < 6000 && !(a.peers().size() == 1 && b.peers().size() == 1); waited += 100) {
                QTest :: qWait(100);
            }
            if (!(a.peers().size() == 1 && b.peers().size() == 1)) {
                QSKIP("multicast is not delivered on this machine (optional here)");
            }
        }
        QTRY_VERIFY_WITH_TIMEOUT(a.peers().size() == 1 && b.peers().size() == 1, 8000);
        QCOMPARE(a.peers().first().name, QString("Real B"));
        QCOMPARE(a.peers().first().port, quint16(45455));
        QVERIFY(a.peers().first().pairing);
        QCOMPARE(b.peers().first().fingerprint, fpA);
        b.stop();
        QTRY_VERIFY_WITH_TIMEOUT(a.peers().isEmpty(), 3000); // the goodbye arrives
    }
};

QTEST_GUILESS_MAIN(TestMdnsUdp)
#include "test_mdns_udp.moc"
