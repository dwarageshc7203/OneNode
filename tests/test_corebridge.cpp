#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSslSocket>
#include "identity.h"
#include "tlsconfig.h"
#include <QTemporaryDir>

#include "corebridge.h"

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

private slots:
    void initTestCase()
    {
        QSettings :: setPath(QSettings :: NativeFormat, QSettings :: UserScope, settingsDir.path());
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
        QSslSocket raw;
        raw.setSslConfiguration(TlsConfig :: make(rawCert, rawKey));
        connect(&raw, &QSslSocket :: sslErrors, &raw, [&raw] (const QList<QSslError> &e) { TlsConfig :: acceptSelfSigned(&raw, e); });
        raw.connectToHostEncrypted("127.0.0.1", receiver.listeningPort());
        QTRY_VERIFY_WITH_TIMEOUT(raw.isEncrypted(), 3000); // (not waitForEncrypted: that would block the receiver in this same thread)
        raw.write(QByteArray(4, '\0')); // a header length of 0 is not allowed (must be 1 to 16384)
        raw.flush();

        QTRY_COMPARE_WITH_TIMEOUT(events -> count("failed"), 1, 3000);

        const QList<Event> list = events -> forId(1);
        QCOMPARE(list.size(), 2); // the name was never known, but the row still gets announced first
        QCOMPARE(list.first().kind, QString("started"));
        QVERIFY(list.first().direction == CoreBridge :: Direction :: Receive);
        QCOMPARE(list.last().kind, QString("failed"));
    }
};

QTEST_GUILESS_MAIN(TestCoreBridge)
#include "test_corebridge.moc"
