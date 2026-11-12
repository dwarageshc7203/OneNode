#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
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
    void idsAreUniqueAndStartedComesFirst()
    {
        CoreBridge receiver;
        receiver.setReceiveDir(tempDir.path() + "/inbox");
        QVERIFY(receiver.startListening(0)); // port 0: the OS picks a free port
        auto *receiverEvents = new Recorder(&receiver);

        CoreBridge sender;
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
        auto *events = new Recorder(&sender);
        sender.sendFile("127.0.0.1", makeFile("x.bin", 10), closedPort);

        QTRY_COMPARE_WITH_TIMEOUT(events -> count("failed"), 1, 5000);

        const QList<Event> list = events -> forId(1);
        QCOMPARE(list.size(), 2); // exactly started, then failed
        QCOMPARE(list.first().kind, QString("started"));
        QCOMPARE(list.first().name, QString("x.bin"));
        QCOMPARE(list.last().kind, QString("failed"));
    }

    void badHeaderIsAnnouncedBeforeItFails()
    {
        CoreBridge receiver;
        receiver.setReceiveDir(tempDir.path() + "/inbox2");
        QVERIFY(receiver.startListening(0));
        auto *events = new Recorder(&receiver);

        QTcpSocket raw;
        raw.connectToHost("127.0.0.1", receiver.listeningPort());
        QVERIFY(raw.waitForConnected(2000));
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
