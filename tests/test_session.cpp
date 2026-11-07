#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QCryptographicHash>
#include <QJsonObject>

#include "protocol.h"
#include "receivesession.h"
#include "sendsession.h"

// A tiny client that speaks raw bytes to a ReceiveSession, so tests can send good and bad frames on purpose
class RawClient
{
public:
    explicit RawClient(quint16 port)
    {
        socket.connectToHost("127.0.0.1", port);
        QObject :: connect(&socket, &QTcpSocket :: readyRead, [this] { buffer += socket.readAll(); });
    }

    void send(const QByteArray &bytes) { socket.write(bytes); }

    // spins the event loop (so the server side can run) until a full reply frame arrives or time runs out
    bool waitReply(QJsonObject &reply, int timeoutMs = 3000)
    {
        for (int waited = 0; waited < timeoutMs; waited += 10) {
            if (Protocol :: tryDecodeFrame(buffer, reply) == Protocol :: decodeStatus :: Ok) {
                return true;
            }
            QTest :: qWait(10);
        }
        return false;
    }

    void hangUp() { socket.disconnectFromHost(); }

private:
    QTcpSocket socket;
    QByteArray buffer;
};

class TestSession : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir tempDir; // a throwaway folder: tests never touch the real Downloads
    QTcpServer server;
    QString destination;
    QStringList failedReasons;

    static QString sha256Hex(const QByteArray &data)
    {
        return QString :: fromLatin1(QCryptographicHash :: hash(data, QCryptographicHash :: Sha256).toHex());
    }

    static QByteArray headerFrame(const QString &name, const QJsonValue &size)
    {
        QJsonObject header;
        header["v"] = Protocol :: version;
        header["type"] = "file";
        header["filename"] = name;
        header["size"] = size;
        return Protocol :: encodeFrame(header);
    }

    static QByteArray trailerFrame(const QString &type, const QJsonValue &sha)
    {
        QJsonObject trailer;
        trailer["v"] = Protocol :: version;
        trailer["type"] = type;
        trailer["sha256"] = sha;
        return Protocol :: encodeFrame(trailer);
    }

    QStringList filesOnDisk() const
    {
        return QDir(destination).entryList(QDir :: Files | QDir :: NoDotAndDotDot);
    }

private slots:
    void init() // runs before every test: fresh server, fresh folder
    {
        destination = tempDir.path() + "/" + QString :: number(QRandomGenerator :: global() -> generate());
        failedReasons.clear();

        QVERIFY(server.listen(QHostAddress :: LocalHost, 0));
        connect(&server, &QTcpServer :: newConnection, this, [this] {
            auto *session = new ReceiveSession(server.nextPendingConnection(), destination, &server);
            connect(session, &ReceiveSession :: failed, this, [this] (const QString &, const QString &reason) {
                failedReasons << reason;
            });
        });
    }

    void cleanup() // runs after every test
    {
        server.close();
        server.disconnect(this);
    }

    void realTransfer()
    {
        QByteArray data(3 * 1024 * 1024 + 17, 0); // not a multiple of the chunk size on purpose
        for (int i = 0; i < data.size(); ++i) {
            data[i] = static_cast<char>((i * 131 + 7) ^ (i >> 8));
        }

        const QString source = tempDir.path() + "/source.bin";
        QFile sourceFile(source);
        QVERIFY(sourceFile.open(QIODevice :: WriteOnly));
        sourceFile.write(data);
        sourceFile.close();

        QStringList finished;
        QStringList senderFailed;
        auto *sender = new SendSession("127.0.0.1", server.serverPort(), source, this);
        connect(sender, &SendSession :: finished, this, [&] (const QString &name) { finished << name; });
        connect(sender, &SendSession :: failed, this, [&] (const QString &, const QString &reason) { senderFailed << reason; });

        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QVERIFY(senderFailed.isEmpty());
        QCOMPARE(filesOnDisk(), QStringList{"source.bin"});

        QFile received(destination + "/source.bin");
        QVERIFY(received.open(QIODevice :: ReadOnly));
        QCOMPARE(received.readAll(), data);
    }

    void emptyFile()
    {
        RawClient client(server.serverPort());
        client.send(headerFrame("empty.txt", 0) + trailerFrame("file_end", sha256Hex(QByteArray())));

        QJsonObject reply;
        QVERIFY(client.waitReply(reply));
        QCOMPARE(reply.value("status").toString(), QString("ok"));
        QTRY_COMPARE(filesOnDisk(), QStringList{"empty.txt"});
    }

    void wrongChecksum()
    {
        const QByteArray body("hello world");
        RawClient client(server.serverPort());
        client.send(headerFrame("bad.txt", body.size()) + body + trailerFrame("file_end", sha256Hex("something else")));

        QJsonObject reply;
        QVERIFY(client.waitReply(reply));
        QCOMPARE(reply.value("status").toString(), QString("error"));
        QCOMPARE(reply.value("code").toString(), QString("checksum_mismatch"));
        QTRY_VERIFY(filesOnDisk().isEmpty()); // neither bad.txt nor bad.txt.onenode-part may remain
    }

    void missingTrailer()
    {
        const QByteArray body("hello world");
        RawClient client(server.serverPort());
        client.send(headerFrame("notrailer.txt", body.size()) + body);
        QTest :: qWait(100);
        client.hangUp();

        QTRY_COMPARE(failedReasons.size(), 1);
        QTRY_VERIFY(filesOnDisk().isEmpty());
    }

    void wrongTrailerType()
    {
        const QByteArray body("abc");
        RawClient client(server.serverPort());
        client.send(headerFrame("a.txt", body.size()) + body + trailerFrame("nope", sha256Hex(body)));

        QJsonObject reply;
        QVERIFY(client.waitReply(reply));
        QCOMPARE(reply.value("code").toString(), QString("invalid_header"));
        QTRY_VERIFY(filesOnDisk().isEmpty());
    }

    void nonStringChecksum()
    {
        const QByteArray body("abc");
        RawClient client(server.serverPort());
        client.send(headerFrame("a.txt", body.size()) + body + trailerFrame("file_end", 5));

        QJsonObject reply;
        QVERIFY(client.waitReply(reply));
        QCOMPARE(reply.value("code").toString(), QString("invalid_header"));
    }

    void uppercaseChecksumIsAccepted()
    {
        const QByteArray body("abc");
        RawClient client(server.serverPort());
        client.send(headerFrame("upper.txt", body.size()) + body + trailerFrame("file_end", sha256Hex(body).toUpper()));

        QJsonObject reply;
        QVERIFY(client.waitReply(reply));
        QCOMPARE(reply.value("status").toString(), QString("ok"));
    }

    void pathTraversalFilenameIsRejected()
    {
        RawClient client(server.serverPort());
        client.send(headerFrame("../evil.txt", 0));

        QJsonObject reply;
        QVERIFY(client.waitReply(reply));
        QCOMPARE(reply.value("code").toString(), QString("invalid_filename"));
        QVERIFY(!QFileInfo :: exists(tempDir.path() + "/evil.txt"));
    }

    void nonIntegerSizeIsRejected_data()
    {
        QTest :: addColumn<double>("size");
        QTest :: newRow("fraction") << 1.5;
        QTest :: newRow("huge") << 1e30;
        QTest :: newRow("negative fraction") << -1.5;
        QTest :: newRow("negative") << -1.0;
    }

    void nonIntegerSizeIsRejected()
    {
        QFETCH(double, size);

        RawClient client(server.serverPort());
        client.send(headerFrame("size.txt", size));

        QJsonObject reply;
        QVERIFY(client.waitReply(reply));
        QCOMPARE(reply.value("code").toString(), QString("invalid_header"));
        QVERIFY(filesOnDisk().isEmpty());
    }

    void twoBadFramesGetExactlyOneReply()
    {
        RawClient client(server.serverPort());
        client.send(headerFrame("../a", 0) + headerFrame("../b", 0)); // second frame arrives in the same connection

        QJsonObject first;
        QVERIFY(client.waitReply(first));
        QJsonObject second;
        QVERIFY(!client.waitReply(second, 300)); // there must be no second reply
    }
};

QTEST_GUILESS_MAIN(TestSession)
#include "test_session.moc"