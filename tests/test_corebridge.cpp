#include <QtTest>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSslSocket>
#include "identity.h"
#include "tlsconfig.h"
#include "truststore.h"
#include <QTemporaryDir>

#include "corebridge.h"
#include "fakemdns.h"
#include "dnsmessage.h"

// One thing the bridge told us, written down so a test can check the order afterwards
struct Event
{
    QString kind;      // "started", "progress", "finished" or "failed"
    quint64 id = 0;
    QString name;
    qint64 total = 0;  // only filled in for "started"
    CoreBridge :: Direction direction = CoreBridge :: Direction :: Send; // only filled in for "started"
};

// Listens to every signal of one CoreBridge and keeps a list of what happened.
// Its parent is the bridge, so it is deleted together with it and can never be called after the bridge is gone.
class Recorder : public QObject
{
public:
    explicit Recorder(CoreBridge *bridge) : QObject(bridge)
    {
        connect(bridge, &CoreBridge :: transferStarted, this, [this] (quint64 id, const QString &name, CoreBridge :: Direction direction, qint64 total) {
            events.append({"started", id, name, total, direction});
        });
        connect(bridge, &CoreBridge :: transferProgress, this, [this] (quint64 id, const QString &name, qint64, qint64) {
            events.append({"progress", id, name, 0, CoreBridge :: Direction :: Send});
        });
        connect(bridge, &CoreBridge :: transferFinished, this, [this] (quint64 id, const QString &name) {
            events.append({"finished", id, name, 0, CoreBridge :: Direction :: Send});
        });
        connect(bridge, &CoreBridge :: transferFailed, this, [this] (quint64 id, const QString &name, const QString &) {
            events.append({"failed", id, name, 0, CoreBridge :: Direction :: Send});
        });
    }

    int count(const QString &kind) const
    {
        int n = 0;
        for (const Event &e : events) {
            if (e.kind == kind) {
                ++n;
            }
        }
        return n;
    }

    // the events of one transfer, in the order they happened
    QList<Event> forId(quint64 id) const
    {
        QList<Event> result;
        for (const Event &e : events) {
            if (e.id == id) {
                result.append(e);
            }
        }
        return result;
    }

    QSet<quint64> ids() const
    {
        QSet<quint64> result;
        for (const Event &e : events) {
            result.insert(e.id);
        }
        return result;
    }

private:
    QList<Event> events;
};

class TestCoreBridge : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir tempDir; // a throwaway folder: tests never touch the real Downloads
    QTemporaryDir settingsDir; // where the remembered settings go during the tests, not in the real ~/.config
    QList<QSharedPointer<QTemporaryDir>> identityDirs; // one throwaway identity folder per bridge that needs one

    // Makes two bridges know each other, as if they had paired before
    void makeFriends(CoreBridge &a, CoreBridge &b)
    {
        QVERIFY(a.trustStore() -> add(b.myFingerprint(), "B").isEmpty());
        QVERIFY(b.trustStore() -> add(a.myFingerprint(), "A").isEmpty());
    }

    // Gives a bridge its own device identity (certificate + key), like main.cpp does at startup
    void giveIdentity(CoreBridge &bridge)
    {
        auto dir = QSharedPointer<QTemporaryDir>::create();
        identityDirs.append(dir);
        const QString error = bridge.loadIdentity(dir -> path());
        QVERIFY2(error.isEmpty(), qPrintable(error));
    }

    QString makeFile(const QString &name, int size)
    {
        QByteArray data(size, 0);
        for (int i = 0; i < size; ++i) {
            data[i] = static_cast<char>(i * 31 + 5);
        }

        const QString path = tempDir.path() + "/" + name;
        QFile file(path);
        if (!file.open(QIODevice :: WriteOnly)) {
            return QString();
        }
        file.write(data);
        file.close();
        return path;
    }

    // a receiver that is listening, and a sender that has never met it
    struct Pair {
        CoreBridge receiver;
        CoreBridge sender;
        QString pin;           // what the receiver shows, e.g. "482 913"
        quint16 port = 0;
        int pinRequests = 0;
        quint64 lastPinId = 0;
    };

    void setUpStrangers(Pair &p, const QString &inbox)
    {
        giveIdentity(p.receiver);
        giveIdentity(p.sender);
        p.receiver.setReceiveDir(tempDir.path() + "/" + inbox);
        QVERIFY(p.receiver.startListening(0));
        p.port = p.receiver.listeningPort();
        connect(&p.receiver, &CoreBridge :: pairingStarted, this, [&p] (const QString &pin) { p.pin = pin; });
        connect(&p.sender, &CoreBridge :: pinNeeded, this, [&p] (quint64 id, const QString &) { ++p.pinRequests; p.lastPinId = id; });
    }

private slots:
    void initTestCase()
    {
        QSettings :: setPath(QSettings :: NativeFormat, QSettings :: UserScope, settingsDir.path());
        const QString problem = TlsConfig :: selectBackend();
        QVERIFY2(problem.isEmpty(), qPrintable(problem));
    }

    // ---- the receive folder ----
    void changeReceiveFolderSwitchesAndRemembers()
    {
        CoreBridge bridge;
        const QString folder = tempDir.path() + "/newhome";
        QDir().mkpath(folder);

        QVERIFY(bridge.changeReceiveFolder(QUrl :: fromLocalFile(folder)).isEmpty());
        QCOMPARE(bridge.receiveFolder(), folder);
        QCOMPARE(bridge.receiveFolderUrl(), QUrl :: fromLocalFile(folder));
        QCOMPARE(QSettings("OneNode", "OneNode").value("receiveDir").toString(), folder); // remembered for the next start
    }

    void changeReceiveFolderRefusesBadFolders()
    {
        CoreBridge bridge;
        const QString before = bridge.receiveFolder();

        QVERIFY(!bridge.changeReceiveFolder(QUrl()).isEmpty());                                        // nothing chosen
        QVERIFY(!bridge.changeReceiveFolder(QUrl :: fromLocalFile(tempDir.path() + "/nope")).isEmpty()); // does not exist
        QVERIFY(!bridge.changeReceiveFolder(QUrl("https://example.com/x")).isEmpty());                 // not a local folder

        QCOMPARE(bridge.receiveFolder(), before); // nothing changed
    }

    // ---- the address box ----
    void parseAddress_data()
    {
        QTest :: addColumn<QString>("text");
        QTest :: addColumn<QString>("host");
        QTest :: addColumn<int>("port");
        QTest :: addColumn<bool>("ok");

        QTest :: newRow("ip only")          << "192.168.1.23"       << "192.168.1.23" << 45454 << true;
        QTest :: newRow("ip and port")      << "192.168.1.23:9000"  << "192.168.1.23" << 9000  << true;
        QTest :: newRow("spaces around")    << "  10.0.0.5:1234  "  << "10.0.0.5"     << 1234  << true;
        QTest :: newRow("a name")           << "my-pc.local"        << "my-pc.local"  << 45454 << true;
        QTest :: newRow("empty")            << ""                   << "" << 0 << false;
        QTest :: newRow("only spaces")      << "   "                << "" << 0 << false;
        QTest :: newRow("port zero")        << "10.0.0.5:0"         << "" << 0 << false;
        QTest :: newRow("port too big")     << "10.0.0.5:65536"     << "" << 0 << false;
        QTest :: newRow("port not a number")<< "10.0.0.5:abc"       << "" << 0 << false;
        QTest :: newRow("empty port")       << "10.0.0.5:"          << "" << 0 << false;
        QTest :: newRow("no host")          << ":9000"              << "" << 0 << false;
        QTest :: newRow("inner space")      << "10.0.0 .5"          << "" << 0 << false;
        QTest :: newRow("ipv6")             << "fe80::1"            << "" << 0 << false;
        QTest :: newRow("slash")            << "10.0.0.5/evil"      << "" << 0 << false;
    }

    void parseAddress()
    {
        QFETCH(QString, text);
        QFETCH(QString, host);
        QFETCH(int, port);
        QFETCH(bool, ok);

        QString gotHost;
        quint16 gotPort = 0;
        const QString problem = CoreBridge :: parseAddress(text, gotHost, gotPort);

        QCOMPARE(problem.isEmpty(), ok);
        if (ok) {
            QCOMPARE(gotHost, host);
            QCOMPARE(int(gotPort), port);
        } else {
            QVERIFY(gotHost.isEmpty()); // nothing is filled in on failure
        }
    }

    // ---- sendTo: several files from urls, with checks before anything starts ----
    void sendToStartsOneTransferPerFile()
    {
        CoreBridge receiver;
        giveIdentity(receiver);
        receiver.setReceiveDir(tempDir.path() + "/inbox");
        QVERIFY(receiver.startListening(0));
        auto *receiverEvents = new Recorder(&receiver);

        CoreBridge sender;
        giveIdentity(sender);
        makeFriends(receiver, sender);
        const QString address = "127.0.0.1:" + QString :: number(receiver.listeningPort());
        const QString problem = sender.sendTo(address, { QUrl :: fromLocalFile(makeFile("one.bin", 1000)),
                                                         QUrl :: fromLocalFile(makeFile("two.bin", 2000)) });
        QVERIFY2(problem.isEmpty(), qPrintable(problem));

        QTRY_COMPARE_WITH_TIMEOUT(receiverEvents -> count("finished"), 2, 5000);
        QVERIFY(QFile :: exists(tempDir.path() + "/inbox/one.bin"));
        QVERIFY(QFile :: exists(tempDir.path() + "/inbox/two.bin"));
    }

    void sendToStartsNothingWhenSomethingIsWrong()
    {
        CoreBridge sender;
        giveIdentity(sender);
        QSignalSpy started(&sender, &CoreBridge :: transferStarted);
        const QUrl good = QUrl :: fromLocalFile(makeFile("ok.bin", 10));

        QVERIFY(!sender.sendTo("", { good }).isEmpty());                         // no address
        QVERIFY(!sender.sendTo("10.0.0.5", {}).isEmpty());                       // no files
        QVERIFY(!sender.sendTo("10.0.0.5", { good, QUrl("https://x.y/a") }).isEmpty()); // one file is not local

        QCOMPARE(started.count(), 0); // the good file was NOT started either: all or nothing
    }

    // the started signal tells the screen WHO the other device is
    void startedCarriesThePeerAddress()
    {
        CoreBridge receiver;
        giveIdentity(receiver);
        receiver.setReceiveDir(tempDir.path() + "/inbox");
        QVERIFY(receiver.startListening(0));
        QSignalSpy receiverSpy(&receiver, &CoreBridge :: transferStarted);

        CoreBridge sender;
        giveIdentity(sender);
        makeFriends(receiver, sender);
        QSignalSpy senderSpy(&sender, &CoreBridge :: transferStarted);
        sender.sendFile("127.0.0.1", makeFile("peer.bin", 1000), receiver.listeningPort());

        QTRY_COMPARE_WITH_TIMEOUT(receiverSpy.count(), 1, 5000);
        QCOMPARE(senderSpy.first().at(4).toString(), QString("127.0.0.1"));   // we sent TO this address
        QCOMPARE(receiverSpy.first().at(4).toString(), QString("127.0.0.1")); // we received FROM this address (not "::ffff:127.0.0.1")
    }

    void idsAreUniqueAndStartedComesFirst()
    {
        CoreBridge receiver;
        giveIdentity(receiver);
        receiver.setReceiveDir(tempDir.path() + "/inbox");
        QVERIFY(receiver.startListening(0)); // port 0: the OS picks a free port
        auto *receiverEvents = new Recorder(&receiver);

        CoreBridge sender;
        giveIdentity(sender);
        makeFriends(receiver, sender);
        auto *senderEvents = new Recorder(&sender);

        const quint16 port = receiver.listeningPort();
        sender.sendFile("127.0.0.1", makeFile("first.bin", 200 * 1000), port);
        sender.sendFile("127.0.0.1", makeFile("second.bin", 5), port);

        QTRY_COMPARE_WITH_TIMEOUT(senderEvents -> count("finished"), 2, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(receiverEvents -> count("finished"), 2, 5000);

        // sender side: two different ids, handed out in the order of the calls
        QCOMPARE(senderEvents -> ids(), (QSet<quint64>{1, 2}));
        for (const quint64 id : {quint64(1), quint64(2)}) {
            const QList<Event> events = senderEvents -> forId(id);
            QCOMPARE(events.first().kind, QString("started"));
            QVERIFY(events.first().direction == CoreBridge :: Direction :: Send);
            QCOMPARE(events.last().kind, QString("finished"));
            int started = 0;
            for (const Event &e : events) {
                started += (e.kind == "started") ? 1 : 0;
            }
            QCOMPARE(started, 1); // announced exactly once
        }
        QCOMPARE(senderEvents -> forId(1).first().name, QString("first.bin"));
        QCOMPARE(senderEvents -> forId(1).first().total, qint64(200 * 1000));
        QCOMPARE(senderEvents -> forId(2).first().name, QString("second.bin"));
        QCOMPARE(senderEvents -> forId(2).first().total, qint64(5));

        // receiver side: it learns name and size from the header, so the announcement is made just before the first progress
        QCOMPARE(receiverEvents -> ids().size(), 2);
        QMap<QString, qint64> receivedSizes;
        for (const quint64 id : receiverEvents -> ids()) {
            const QList<Event> events = receiverEvents -> forId(id);
            QCOMPARE(events.first().kind, QString("started"));
            QVERIFY(events.first().direction == CoreBridge :: Direction :: Receive);
            QCOMPARE(events.last().kind, QString("finished"));
            receivedSizes[events.first().name] = events.first().total;
        }
        QCOMPARE(receivedSizes.value("first.bin"), qint64(200 * 1000));
        QCOMPARE(receivedSizes.value("second.bin"), qint64(5));

        QCOMPARE(QFileInfo(tempDir.path() + "/inbox/first.bin").size(), qint64(200 * 1000));
    }

    void failedSendIsStillAnnouncedFirst()
    {
        quint16 closedPort = 0;
        {
            QTcpServer probe;
            QVERIFY(probe.listen(QHostAddress :: LocalHost, 0));
            closedPort = probe.serverPort();
        } // the probe is gone, so nobody listens on that port any more

        CoreBridge sender;
        giveIdentity(sender);
        auto *events = new Recorder(&sender);
        sender.sendFile("127.0.0.1", makeFile("x.bin", 10), closedPort);

        QTRY_COMPARE_WITH_TIMEOUT(events -> count("failed"), 1, 5000);

        const QList<Event> list = events -> forId(1);
        QCOMPARE(list.size(), 2); // exactly started, then failed
        QCOMPARE(list.first().kind, QString("started"));
        QCOMPARE(list.first().name, QString("x.bin"));
        QCOMPARE(list.last().kind, QString("failed"));
    }

    void withoutAnIdentityNothingStartsAndNothingListens()
    {
        CoreBridge bridge; // loadIdentity was never called
        QVERIFY(!bridge.startListening(0));
        QCOMPARE(bridge.listeningPort(), quint64(0));

        auto *events = new Recorder(&bridge);
        bridge.sendFile("127.0.0.1", makeFile("noid.bin", 10), 45454);
        QCOMPARE(events -> count("failed"), 1); // reported at once, nothing was connected
    }

    void badHeaderIsAnnouncedBeforeItFails()
    {
        CoreBridge receiver;
        giveIdentity(receiver);
        receiver.setReceiveDir(tempDir.path() + "/inbox2");
        QVERIFY(receiver.startListening(0));
        auto *events = new Recorder(&receiver);

        // a hand-made sender: a proper TLS connection with its own identity, but a broken first frame
        QTemporaryDir rawIdentityDir;
        QSslCertificate rawCert;
        QSslKey rawKey;
        QString identityError;
        QVERIFY2(Identity :: loadOrCreate(rawIdentityDir.path(), rawCert, rawKey, identityError), qPrintable(identityError));
        QVERIFY(receiver.trustStore() -> add(Identity :: fingerprint(rawCert), "Raw").isEmpty()); // an old friend
        QSslSocket raw;
        raw.setSslConfiguration(TlsConfig :: make(rawCert, rawKey));
        connect(&raw, &QSslSocket :: sslErrors, &raw, [&raw] (const QList<QSslError> &e) { TlsConfig :: acceptSelfSigned(&raw, e); });
        raw.connectToHostEncrypted("127.0.0.1", receiver.listeningPort());
        QTRY_VERIFY_WITH_TIMEOUT(raw.isEncrypted(), 3000); // (not waitForEncrypted: that would block the receiver in this same thread)
        // a friend says hello first, as the protocol demands
        QJsonObject hello;
        hello["v"] = Protocol :: version;
        hello["type"] = "hello";
        hello["name"] = "Raw";
        hello["known"] = true;
        QByteArray inbox;
        connect(&raw, &QSslSocket :: readyRead, &raw, [&] { inbox += raw.readAll(); });
        raw.write(Protocol :: encodeFrame(hello));
        QJsonObject helloReply;
        QTRY_VERIFY_WITH_TIMEOUT(inbox.size() > 4, 3000); // (a condition without side effects: QTRY evaluates it twice)
        QVERIFY(Protocol :: tryDecodeFrame(inbox, helloReply) == Protocol :: decodeStatus :: Ok);
        QCOMPARE(helloReply.value("status").toString(), QString("ok"));
        raw.write(QByteArray(4, '\0')); // a header length of 0 is not allowed (must be 1 to 16384)
        raw.flush();

        QTRY_COMPARE_WITH_TIMEOUT(events -> count("failed"), 1, 3000);

        const QList<Event> list = events -> forId(1);
        QCOMPARE(list.size(), 2); // the name was never known, but the row still gets announced first
        QCOMPARE(list.first().kind, QString("started"));
        QVERIFY(list.first().direction == CoreBridge :: Direction :: Receive);
        QCOMPARE(list.last().kind, QString("failed"));
    }

    // ======== pairing between two real bridges over real TLS ========

    void firstContactPairsWithThePinAndLaterSendsNeedNone()
    {
        Pair p;
        setUpStrangers(p, "pairinbox");
        auto *senderEvents = new Recorder(&p.sender);
        QSignalSpy receiverPaired(&p.receiver, &CoreBridge :: devicePaired);
        QSignalSpy senderPaired(&p.sender, &CoreBridge :: devicePaired);
        QSignalSpy receiverRows(&p.receiver, &CoreBridge :: transferStarted);

        p.receiver.startPairing();
        QVERIFY(!p.pin.isEmpty());
        QVERIFY(p.receiver.pairingActive());

        p.sender.sendFile("127.0.0.1", makeFile("first.bin", 5000), p.port);
        QTRY_COMPARE_WITH_TIMEOUT(p.pinRequests, 1, 5000);

        QVERIFY(!p.sender.submitPin(p.lastPinId, "12").isEmpty());          // wrong format: asked again, nothing sent
        QVERIFY(p.sender.submitPin(p.lastPinId, p.pin).isEmpty());          // the PIN as shown ("482 913" with its space)

        QTRY_COMPARE_WITH_TIMEOUT(senderEvents -> count("finished"), 1, 8000);
        QVERIFY(QFile :: exists(tempDir.path() + "/pairinbox/first.bin"));

        QVERIFY(p.receiver.trustStore() -> isTrusted(p.sender.myFingerprint()));
        QVERIFY(p.sender.trustStore() -> isTrusted(p.receiver.myFingerprint()));
        QCOMPARE(receiverPaired.count(), 1);
        QCOMPARE(senderPaired.count(), 1);
        QVERIFY(!p.receiver.pairingActive()); // one PIN pairs one device

        // second send: no PIN, no pairing mode needed
        p.sender.sendFile("127.0.0.1", makeFile("second.bin", 3000), p.port);
        QTRY_COMPARE_WITH_TIMEOUT(senderEvents -> count("finished"), 2, 5000);
        QCOMPARE(p.pinRequests, 1);
        QVERIFY(QFile :: exists(tempDir.path() + "/pairinbox/second.bin"));
        Q_UNUSED(receiverRows);
    }

    void aWrongPinFailsWithAClearMessageAndPairsNobody()
    {
        Pair p;
        setUpStrangers(p, "wrongpin");
        QSignalSpy failed(&p.sender, &CoreBridge :: transferFailed);
        QSignalSpy receiverRows(&p.receiver, &CoreBridge :: transferStarted);

        p.receiver.startPairing();
        p.sender.sendFile("127.0.0.1", makeFile("x.bin", 100), p.port);
        QTRY_COMPARE_WITH_TIMEOUT(p.pinRequests, 1, 5000);

        const int shown = QString(p.pin).remove(' ').toInt();
        QVERIFY(p.sender.submitPin(p.lastPinId, QString("%1").arg((shown + 1) % 1000000, 6, 10, QChar('0'))).isEmpty());

        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 8000);
        QVERIFY2(failed.first().at(2).toString().contains("Pairing failed"), qPrintable(failed.first().at(2).toString()));
        QVERIFY(!p.receiver.trustStore() -> isTrusted(p.sender.myFingerprint()));
        QVERIFY(!p.sender.trustStore() -> isTrusted(p.receiver.myFingerprint()));
        QVERIFY(!p.receiver.pairingActive());          // burnt
        QCOMPARE(receiverRows.count(), 0);              // and the receiver's list of transfers never saw it
    }

    void aStrangerIsToldHowToPairAndTheReceiverShowsNothing()
    {
        Pair p;
        setUpStrangers(p, "nopairing");
        QSignalSpy failed(&p.sender, &CoreBridge :: transferFailed);
        QSignalSpy receiverRows(&p.receiver, &CoreBridge :: transferStarted);
        QSignalSpy receiverFailed(&p.receiver, &CoreBridge :: transferFailed);

        p.sender.sendFile("127.0.0.1", makeFile("x.bin", 100), p.port); // the receiver is NOT in pairing mode
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
        QVERIFY2(failed.first().at(2).toString().contains("Pair a new device"), qPrintable(failed.first().at(2).toString()));
        QCOMPARE(p.pinRequests, 0);

        QTest :: qWait(100);
        QCOMPARE(receiverRows.count(), 0);   // a stranger cannot make anything appear on the receiver's screen
        QCOMPARE(receiverFailed.count(), 0);
    }

    void severalFilesAskForThePinOnlyOnce()
    {
        Pair p;
        setUpStrangers(p, "threefiles");
        auto *senderEvents = new Recorder(&p.sender);

        p.receiver.startPairing();
        const QString problem = p.sender.sendTo("127.0.0.1:" + QString :: number(p.port),
            { QUrl :: fromLocalFile(makeFile("a.bin", 1000)), QUrl :: fromLocalFile(makeFile("b.bin", 2000)), QUrl :: fromLocalFile(makeFile("c.bin", 3000)) });
        QVERIFY2(problem.isEmpty(), qPrintable(problem));

        QTRY_COMPARE_WITH_TIMEOUT(p.pinRequests, 1, 5000);
        QVERIFY(p.sender.submitPin(p.lastPinId, p.pin).isEmpty());

        QTRY_COMPARE_WITH_TIMEOUT(senderEvents -> count("finished"), 3, 10000);
        QCOMPARE(p.pinRequests, 1); // asked once, not three times
        QVERIFY(QFile :: exists(tempDir.path() + "/threefiles/a.bin"));
        QVERIFY(QFile :: exists(tempDir.path() + "/threefiles/b.bin"));
        QVERIFY(QFile :: exists(tempDir.path() + "/threefiles/c.bin"));
    }

    void ifTheFirstFileCannotStartTheOthersAreReportedNotSent()
    {
        Pair p;
        setUpStrangers(p, "firstfails");
        QSignalSpy failed(&p.sender, &CoreBridge :: transferFailed);

        // the receiver is not in pairing mode, so the first file is refused, and the others must not even try
        const QString problem = p.sender.sendTo("127.0.0.1:" + QString :: number(p.port),
            { QUrl :: fromLocalFile(makeFile("a.bin", 10)), QUrl :: fromLocalFile(makeFile("b.bin", 10)) });
        QVERIFY(problem.isEmpty());

        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 2, 5000);
        QVERIFY2(failed.last().at(2).toString().startsWith("Not sent:"), qPrintable(failed.last().at(2).toString()));
    }

    void cancellingThePinStopsTheSendAndKeepsThePinAlive()
    {
        Pair p;
        setUpStrangers(p, "cancelpin");
        QSignalSpy failed(&p.sender, &CoreBridge :: transferFailed);

        p.receiver.startPairing();
        p.sender.sendFile("127.0.0.1", makeFile("x.bin", 100), p.port);
        QTRY_COMPARE_WITH_TIMEOUT(p.pinRequests, 1, 5000);

        p.sender.cancelPin(p.lastPinId);
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 3000);
        QVERIFY(failed.first().at(2).toString().contains("cancelled"));
        QTest :: qWait(100);
        QVERIFY(p.receiver.pairingActive()); // nothing had begun: the code still works
    }

    void removingADeviceMakesItPairAgain()
    {
        Pair p;
        setUpStrangers(p, "removed");
        makeFriends(p.receiver, p.sender);
        QSignalSpy changed(&p.receiver, &CoreBridge :: trustedDevicesChanged);
        QSignalSpy failed(&p.sender, &CoreBridge :: transferFailed);

        QCOMPARE(p.receiver.trustedDevices().size(), 1);
        QVERIFY(p.receiver.removeTrustedDevice(p.sender.myFingerprint()).isEmpty());
        QCOMPARE(p.receiver.trustedDevices().size(), 0);
        QCOMPARE(changed.count(), 1);

        // the sender still has the receiver on its list ("known"), but the receiver forgot it: a plain ok would be unsafe
        p.sender.sendFile("127.0.0.1", makeFile("x.bin", 100), p.port);
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
        QVERIFY(failed.first().at(2).toString().contains("Pair a new device"));
    }

    void trustedDevicesAreListedForTheScreen()
    {
        CoreBridge bridge;
        giveIdentity(bridge);
        QVERIFY(bridge.trustStore() -> add(QString(64, 'a'), "Dwarag's phone").isEmpty());

        const QVariantList list = bridge.trustedDevices();
        QCOMPARE(list.size(), 1);
        const QVariantMap entry = list.first().toMap();
        QCOMPARE(entry.value("name").toString(), QString("Dwarag's phone"));
        QCOMPARE(entry.value("fingerprint").toString(), QString(64, 'a'));
        QVERIFY(entry.value("shortFingerprint").toString().startsWith("aaaaaaaa"));
        QVERIFY(!entry.value("pairedAt").toString().isEmpty());
    }

    void startingPairingNeedsAnIdentity()
    {
        CoreBridge bridge; // no identity
        bridge.startPairing();
        QVERIFY(!bridge.pairingActive());
    }

    void aDamagedTrustListStopsTheBridgeFromStarting()
    {
        QTemporaryDir dir;
        QFile list(dir.filePath("trusted_devices.json"));
        QVERIFY(list.open(QIODevice :: WriteOnly));
        list.write("{ this is not json");
        list.close();

        CoreBridge bridge;
        QVERIFY(!bridge.loadIdentity(dir.path()).isEmpty());
        QVERIFY(!bridge.startListening(0));      // refuses to run without a list it can trust
    }

    void changeDeviceNameCleansRemembersAndRefusesEmpty()
    {
        CoreBridge bridge;
        QVERIFY(!bridge.changeDeviceName("   ").isEmpty());           // an empty name is refused, nothing changes
        QVERIFY(bridge.changeDeviceName("  Bob\tPC  ").isEmpty());
        QCOMPARE(bridge.deviceName(), QString("Bob PC"));              // cleaned like every name
        QCOMPARE(QSettings("OneNode", "OneNode").value("deviceName").toString(), QString("Bob PC")); // remembered for the next start
        QCOMPARE(bridge.pairingSeconds(), 120);                        // the screen's countdown starts from the real PIN lifetime
    }

    // ======== nearby devices (mDNS) over a pretend room, with real TLS between the bridges ========

    // The fake transports need an address that is not loopback (loopback is never accepted as a peer address).
    static QHostAddress lanAddress()
    {
        const auto list = UdpMulticastTransport :: usableInterfaces();
        return list.isEmpty() ? QHostAddress() : list.first().address;
    }

    // a bridge that listens and can be found, in the given room
    void makeVisible(CoreBridge &bridge, FakeRoom &room, const QString &name, const QString &inbox)
    {
        giveIdentity(bridge);
        bridge.setDeviceName(name);
        bridge.setReceiveDir(tempDir.path() + "/" + inbox);
        QVERIFY(bridge.startListening(0));
        auto *transport = new FakeTransport(&room, lanAddress());
        room.nodes << transport;
        bridge.setDiscoveryTransport(transport);
        const QString problem = bridge.startDiscovery();
        QVERIFY2(problem.isEmpty(), qPrintable(problem));
    }

    void nearbyDevicesAreListedButNeverTrusted()
    {
        if (lanAddress().isNull()) { QSKIP("no network connection here"); }
        FakeRoom room;
        CoreBridge a, b;
        makeVisible(a, room, "Laptop A", "nearA");
        makeVisible(b, room, "Phone B", "nearB");
        QSignalSpy changed(&a, &CoreBridge :: nearbyDevicesChanged);

        QTRY_COMPARE_WITH_TIMEOUT(a.nearbyDevices().size(), 1, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(b.nearbyDevices().size(), 1, 3000);
        const QVariantMap seen = a.nearbyDevices().first().toMap();
        QCOMPARE(seen["name"].toString(), QString("Phone B"));
        QCOMPARE(seen["fingerprint"].toString(), b.myFingerprint());
        QCOMPARE(seen["port"].toUInt(), uint(b.listeningPort()));
        QVERIFY(!seen["trusted"].toBool());   // being nearby proves nothing
        QVERIFY(!seen["pairing"].toBool());
        QVERIFY(changed.count() >= 1);

        // paired devices are marked, and the screen is told when the paired list changes
        QVERIFY(a.trustStore() -> add(b.myFingerprint(), "Phone B").isEmpty());
        QVERIFY(a.nearbyDevices().first().toMap()["trusted"].toBool());
        const int before = changed.count();
        QVERIFY(a.removeTrustedDevice(b.myFingerprint()).isEmpty());
        QVERIFY(changed.count() > before);
        QVERIFY(!a.nearbyDevices().first().toMap()["trusted"].toBool());
    }

    void pairingModeIsShownToNearbyDevices()
    {
        if (lanAddress().isNull()) { QSKIP("no network connection here"); }
        FakeRoom room;
        CoreBridge a, b;
        makeVisible(a, room, "A", "pmA");
        makeVisible(b, room, "B", "pmB");
        QTRY_COMPARE_WITH_TIMEOUT(a.nearbyDevices().size(), 1, 3000);
        QVERIFY(!a.nearbyDevices().first().toMap()["pairing"].toBool());

        b.startPairing();
        QTRY_VERIFY_WITH_TIMEOUT(a.nearbyDevices().first().toMap()["pairing"].toBool(), 3000);
        b.cancelPairing();
        QTRY_VERIFY_WITH_TIMEOUT(!a.nearbyDevices().first().toMap()["pairing"].toBool(), 3000);
    }

    void sendingToANearbyPairedDeviceNeedsNoAddressAndNoPin()
    {
        if (lanAddress().isNull()) { QSKIP("no network connection here"); }
        FakeRoom room;
        CoreBridge a, b;
        makeVisible(a, room, "A", "sendA");
        makeVisible(b, room, "B", "sendB");
        makeFriends(a, b);
        auto *events = new Recorder(&a);
        QTRY_COMPARE_WITH_TIMEOUT(a.nearbyDevices().size(), 1, 3000);

        const QString file = makeFile("nearby.bin", 20000);
        QVERIFY(a.sendToNearby(b.myFingerprint(), { QUrl :: fromLocalFile(file) }).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(events -> count("finished"), 1, 8000);
        QVERIFY(QFile :: exists(tempDir.path() + "/sendB/nearby.bin"));
    }

    void aDeviceThatLiesAboutItsFingerprintIsRefusedBeforeAnythingIsSent()
    {
        if (lanAddress().isNull()) { QSKIP("no network connection here"); }
        FakeRoom room;
        CoreBridge a, impostor;
        makeVisible(a, room, "A", "impA");
        giveIdentity(impostor);
        impostor.setReceiveDir(tempDir.path() + "/impX");
        QVERIFY(impostor.startListening(0));
        QSignalSpy impostorRows(&impostor, &CoreBridge :: transferStarted);

        // A pairs with the real B earlier, so B's fingerprint is on A's list ...
        const QString realBFingerprint = QString("b2").repeated(32);
        QVERIFY(a.trustStore() -> add(realBFingerprint, "B").isEmpty());

        // ... and now somebody announces "I am B" but sits on the impostor's address and port
        Dns :: Message m; m.response = true;
        Dns :: Record srv; srv.name = "OneNode-b2b2b2b2._onenode._tcp.local"; srv.type = Dns :: TypeSRV; srv.ttl = 120;
        srv.port = static_cast<quint16>(impostor.listeningPort()); srv.target = "onenode-b2b2b2b2.local";
        Dns :: Record txt; txt.name = srv.name; txt.type = Dns :: TypeTXT; txt.ttl = 120;
        txt.txt = { { "v", "2" }, { "fp", realBFingerprint }, { "name", "B" } };
        Dns :: Record ad; ad.name = srv.target; ad.type = Dns :: TypeA; ad.ttl = 120; ad.address = lanAddress();
        m.answers << srv << txt << ad;
        room.inject(Dns :: encode(m), lanAddress());
        QTRY_COMPARE_WITH_TIMEOUT(a.nearbyDevices().size(), 1, 3000);

        QSignalSpy failed(&a, &CoreBridge :: transferFailed);
        QVERIFY(a.sendToNearby(realBFingerprint, { QUrl :: fromLocalFile(makeFile("secret.bin", 1000)) }).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
        QVERIFY2(failed.first().at(2).toString().contains("not the device it claims"), qPrintable(failed.first().at(2).toString()));
        QCOMPARE(impostorRows.count(), 0);
        QVERIFY(!QFile :: exists(tempDir.path() + "/impX/secret.bin"));
    }

    void sendingToAVanishedNearbyDeviceSaysSo()
    {
        if (lanAddress().isNull()) { QSKIP("no network connection here"); }
        FakeRoom room;
        CoreBridge a;
        makeVisible(a, room, "A", "goneA");
        const QString message = a.sendToNearby(QString("c3").repeated(32), { QUrl :: fromLocalFile(makeFile("g.bin", 10)) });
        QVERIFY(message.contains("not nearby"));
    }

    void stoppingDiscoveryClearsTheListAndSaysGoodbye()
    {
        if (lanAddress().isNull()) { QSKIP("no network connection here"); }
        FakeRoom room;
        CoreBridge a, b;
        makeVisible(a, room, "A", "stopA");
        makeVisible(b, room, "B", "stopB");
        QTRY_COMPARE_WITH_TIMEOUT(a.nearbyDevices().size(), 1, 3000);
        b.stopDiscovery();
        QTRY_COMPARE_WITH_TIMEOUT(a.nearbyDevices().size(), 0, 3000);
        QVERIFY(b.nearbyDevices().isEmpty());
    }

    void discoveryNeedsAListeningDeviceWithAnIdentity()
    {
        CoreBridge bridge;
        QVERIFY(!bridge.startDiscovery().isEmpty());    // no identity
        giveIdentity(bridge);
        QVERIFY(!bridge.startDiscovery().isEmpty());    // not listening yet
    }

    void renamingTheDeviceIsAnnouncedToOthers()
    {
        if (lanAddress().isNull()) { QSKIP("no network connection here"); }
        FakeRoom room;
        CoreBridge a, b;
        makeVisible(a, room, "A", "renA");
        makeVisible(b, room, "Old", "renB");
        QTRY_COMPARE_WITH_TIMEOUT(a.nearbyDevices().size(), 1, 3000);
        b.setDeviceName("Brand new");
        QTRY_COMPARE_WITH_TIMEOUT(a.nearbyDevices().first().toMap()["name"].toString(), QString("Brand new"), 3000);
    }
};

QTEST_GUILESS_MAIN(TestCoreBridge)
#include "test_corebridge.moc"
