#include <QtTest>
#include <QTcpServer>
#include <QSslServer>
#include "identity.h"
#include "tlsconfig.h"
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QCryptographicHash>
#include <QJsonObject>

#include "pairingengine.h"
#include "pairingmode.h"
#include "protocol.h"
#include "receivesession.h"
#include "sendsession.h"
#include "truststore.h"

#include <memory>

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
    QStringList failedReasons;  // the "failed" signal of the sessions (about files, from trusted devices)
    QStringList refusedCodes;   // the "refused" signal (about devices that are not trusted)

    // Raw clients have no TLS here, so the receiver is TOLD who they are (normally it reads that from the handshake).
    const QString ownFp = QString(64, 'b');   // the receiver
    QString peerFp = QString(64, 'a');        // the raw client
    Protocol :: Timeouts timeouts;            // tests that need tiny timeouts change this before connecting

    std :: unique_ptr<QTemporaryDir> trustDir;
    std :: unique_ptr<TrustStore> trust;
    std :: unique_ptr<PairingMode> pairing;
    QStringList pairingEnded;                 // reasons, in order

    void rebuildPairing()
    {
        pairing = std :: make_unique<PairingMode>(timeouts);
        connect(pairing.get(), &PairingMode :: ended, this, [this] (const QString &reason) { pairingEnded << reason; });
    }

    static QByteArray helloFrame(const QString &name = "Test phone", bool known = true, int version = Protocol :: version)
    {
        QJsonObject hello;
        hello["v"] = version;
        hello["type"] = "hello";
        hello["name"] = name;
        hello["known"] = known;
        return Protocol :: encodeFrame(hello);
    }

    // connects a raw client that is already on the receiver's list, and says hello. Returns after hello_result ok.
    std :: unique_ptr<RawClient> connectTrusted()
    {
        auto client = std :: make_unique<RawClient>(server.serverPort());
        client -> send(helloFrame());
        QJsonObject reply;
        if (!client -> waitReply(reply) || reply.value("status").toString() != "ok") {
            qWarning() << "hello was not accepted:" << reply;
        }
        return client;
    }

    // plays the sender's side of the 20 rounds over a raw connection. Returns the receiver's pair_result frame
    // (or an empty object if nothing came). `stopAfterMessages` lets a test walk away in the middle.
    QJsonObject runExchange(RawClient &client, int pin, int stopAfterMessages = -1)
    {
        PairingEngine engine(PairingEngine :: Role :: Sender, pin, PairingEngine :: fingerprintBytes(peerFp), PairingEngine :: fingerprintBytes(ownFp));
        int sent = 0;
        while (true) {
            const PairingEngine :: Action action = engine.action();
            if (action == PairingEngine :: Action :: SendCommit || action == PairingEngine :: Action :: SendOpen) {
                client.send(Protocol :: encodeFrame(engine.makeMessage()));
                if (++sent == stopAfterMessages) {
                    return QJsonObject();
                }
            } else if (action == PairingEngine :: Action :: ExpectCommit || action == PairingEngine :: Action :: ExpectOpen) {
                QJsonObject message;
                if (!client.waitReply(message)) {
                    return QJsonObject();
                }
                if (message.value("type").toString() == "pair_result") {
                    return message; // the receiver ended it
                }
                if (!engine.accept(message)) {
                    return QJsonObject();
                }
            } else if (action == PairingEngine :: Action :: Done) {
                QJsonObject result;
                client.waitReply(result);
                return result;
            } else {
                return QJsonObject();
            }
        }
    }

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
    void initTestCase()
    {
        const QString problem = TlsConfig :: selectBackend();
        QVERIFY2(problem.isEmpty(), qPrintable(problem));
    }

    void init() // runs before every test: fresh server, fresh folder
    {
        destination = tempDir.path() + "/" + QString :: number(QRandomGenerator :: global() -> generate());
        failedReasons.clear();
        refusedCodes.clear();
        pairingEnded.clear();
        timeouts = Protocol :: Timeouts();
        peerFp = QString(64, 'a');

        trustDir = std :: make_unique<QTemporaryDir>();
        trust = std :: make_unique<TrustStore>(trustDir -> path());
        QVERIFY(trust -> load().isEmpty());
        QVERIFY(trust -> add(peerFp, "Raw client").isEmpty()); // by default the raw client is an old friend
        rebuildPairing();

        QVERIFY(server.listen(QHostAddress :: LocalHost, 0));
        connect(&server, &QTcpServer :: newConnection, this, [this] {
            ReceiveSession :: Services services;
            services.destinationDir = destination;
            services.ownFingerprint = ownFp;
            services.ownName = "Test receiver";
            services.peerFingerprint = peerFp;
            services.trust = trust.get();
            services.pairing = pairing.get();
            services.timeouts = timeouts;
            auto *session = new ReceiveSession(server.nextPendingConnection(), services, &server);
            connect(session, &ReceiveSession :: failed, this, [this] (const QString &, const QString &reason) {
                failedReasons << reason;
            });
            connect(session, &ReceiveSession :: refused, this, [this] (const QString &code, const QString &) {
                refusedCodes << code;
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

        // This test goes over real TLS: two devices, each with its own identity.
        // (The other tests speak raw bytes to ReceiveSession on purpose; the frame rules do not depend on TLS.)
        QTemporaryDir receiverIdentityDir, senderIdentityDir;
        QSslCertificate receiverCert, senderCert;
        QSslKey receiverKey, senderKey;
        QString identityError;
        QVERIFY2(Identity :: loadOrCreate(receiverIdentityDir.path(), receiverCert, receiverKey, identityError), qPrintable(identityError));
        QVERIFY2(Identity :: loadOrCreate(senderIdentityDir.path(), senderCert, senderKey, identityError), qPrintable(identityError));

        // both devices already know each other (pairing itself is tested below and in test_corebridge)
        const QString receiverFp = Identity :: fingerprint(receiverCert);
        const QString senderFp = Identity :: fingerprint(senderCert);
        QTemporaryDir receiverTrustDir, senderTrustDir;
        TrustStore receiverTrust(receiverTrustDir.path()), senderTrust(senderTrustDir.path());
        QVERIFY(receiverTrust.add(senderFp, "Sender").isEmpty());
        QVERIFY(senderTrust.add(receiverFp, "Receiver").isEmpty());

        QSslServer tlsServer;
        tlsServer.setSslConfiguration(TlsConfig :: make(receiverCert, receiverKey));
        connect(&tlsServer, &QSslServer :: sslErrors, &tlsServer, &TlsConfig :: acceptSelfSigned);
        connect(&tlsServer, &QTcpServer :: pendingConnectionAvailable, this, [&] {
            QTcpSocket *client = tlsServer.nextPendingConnection();
            ReceiveSession :: Services services;
            services.destinationDir = destination;
            services.ownFingerprint = receiverFp;
            services.ownName = "Receiver";
            services.peerFingerprint = Identity :: fingerprint(static_cast<QSslSocket *>(client) -> peerCertificate());
            services.trust = &receiverTrust;
            new ReceiveSession(client, services, &tlsServer);
        });
        QVERIFY(tlsServer.listen(QHostAddress :: LocalHost, 0));

        QStringList finished;
        QStringList senderFailed;
        SendSession :: Services senderServices;
        senderServices.tls = TlsConfig :: make(senderCert, senderKey);
        senderServices.ownName = "Sender";
        senderServices.trust = &senderTrust;
        auto *sender = new SendSession("127.0.0.1", tlsServer.serverPort(), source, senderServices, this);
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
        auto clientPtr = connectTrusted();
        RawClient &client = *clientPtr;
        client.send(headerFrame("empty.txt", 0) + trailerFrame("file_end", sha256Hex(QByteArray())));

        QJsonObject reply;
        QVERIFY(client.waitReply(reply));
        QCOMPARE(reply.value("status").toString(), QString("ok"));
        QTRY_COMPARE(filesOnDisk(), QStringList{"empty.txt"});
    }

    void wrongChecksum()
    {
        const QByteArray body("hello world");
        auto clientPtr = connectTrusted();
        RawClient &client = *clientPtr;
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
        auto clientPtr = connectTrusted();
        RawClient &client = *clientPtr;
        client.send(headerFrame("notrailer.txt", body.size()) + body);
        QTest :: qWait(100);
        client.hangUp();

        QTRY_COMPARE(failedReasons.size(), 1);
        QTRY_VERIFY(filesOnDisk().isEmpty());
    }

    void wrongTrailerType()
    {
        const QByteArray body("abc");
        auto clientPtr = connectTrusted();
        RawClient &client = *clientPtr;
        client.send(headerFrame("a.txt", body.size()) + body + trailerFrame("nope", sha256Hex(body)));

        QJsonObject reply;
        QVERIFY(client.waitReply(reply));
        QCOMPARE(reply.value("code").toString(), QString("invalid_header"));
        QTRY_VERIFY(filesOnDisk().isEmpty());
    }

    void nonStringChecksum()
    {
        const QByteArray body("abc");
        auto clientPtr = connectTrusted();
        RawClient &client = *clientPtr;
        client.send(headerFrame("a.txt", body.size()) + body + trailerFrame("file_end", 5));

        QJsonObject reply;
        QVERIFY(client.waitReply(reply));
        QCOMPARE(reply.value("code").toString(), QString("invalid_header"));
    }

    void uppercaseChecksumIsAccepted()
    {
        const QByteArray body("abc");
        auto clientPtr = connectTrusted();
        RawClient &client = *clientPtr;
        client.send(headerFrame("upper.txt", body.size()) + body + trailerFrame("file_end", sha256Hex(body).toUpper()));

        QJsonObject reply;
        QVERIFY(client.waitReply(reply));
        QCOMPARE(reply.value("status").toString(), QString("ok"));
    }

    void pathTraversalFilenameIsRejected()
    {
        auto clientPtr = connectTrusted();
        RawClient &client = *clientPtr;
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

        auto clientPtr = connectTrusted();
        RawClient &client = *clientPtr;
        client.send(headerFrame("size.txt", size));

        QJsonObject reply;
        QVERIFY(client.waitReply(reply));
        QCOMPARE(reply.value("code").toString(), QString("invalid_header"));
        QVERIFY(filesOnDisk().isEmpty());
    }

    void twoBadFramesGetExactlyOneReply()
    {
        auto clientPtr = connectTrusted();
        RawClient &client = *clientPtr;
        client.send(headerFrame("../a", 0) + headerFrame("../b", 0)); // second frame arrives in the same connection

        QJsonObject first;
        QVERIFY(client.waitReply(first));
        QJsonObject second;
        QVERIFY(!client.waitReply(second, 300)); // there must be no second reply
    }

    // ======== v2: hello, trust and pairing (PROTOCOL.md sections 4 and 5) ========

    void strangerIsRefusedWhenNotInPairingMode()
    {
        peerFp = QString(64, 'c'); // not on the list
        RawClient client(server.serverPort());
        client.send(helloFrame());

        QJsonObject reply;
        QVERIFY(client.waitReply(reply));
        QCOMPARE(reply.value("type").toString(), QString("hello_result"));
        QCOMPARE(reply.value("status").toString(), QString("error"));
        QCOMPARE(reply.value("code").toString(), QString("pairing_off"));
        QCOMPARE(reply.value("name").toString(), QString("Test receiver"));
        QTRY_COMPARE(refusedCodes, QStringList{"pairing_off"});
        QVERIFY(failedReasons.isEmpty()); // nothing that could reach the screen
    }

    void aFileBeforeHelloIsNotPaired()
    {
        peerFp = QString(64, 'c');
        RawClient client(server.serverPort());
        client.send(headerFrame("sneaky.txt", 0));

        QJsonObject reply;
        QVERIFY(client.waitReply(reply));
        QCOMPARE(reply.value("type").toString(), QString("file_result"));
        QCOMPARE(reply.value("code").toString(), QString("not_paired"));
        QVERIFY(filesOnDisk().isEmpty());
    }

    void helloProblems_data()
    {
        QTest :: addColumn<QByteArray>("frame");
        QTest :: addColumn<QString>("code");

        QJsonObject noName; noName["v"] = Protocol :: version; noName["type"] = "hello";
        QJsonObject wrongType; wrongType["v"] = Protocol :: version; wrongType["type"] = "hi"; wrongType["name"] = "x";
        QJsonObject nameIsNumber; nameIsNumber["v"] = Protocol :: version; nameIsNumber["type"] = "hello"; nameIsNumber["name"] = 5;

        QTest :: newRow("old version")   << helloFrame("x", true, 1) << "unsupported";
        QTest :: newRow("no name")       << Protocol :: encodeFrame(noName) << "invalid_header";
        QTest :: newRow("wrong type")    << Protocol :: encodeFrame(wrongType) << "invalid_header";
        QTest :: newRow("name a number") << Protocol :: encodeFrame(nameIsNumber) << "invalid_header";
        QTest :: newRow("zero length")   << QByteArray(4, '\0') << "invalid_header";
    }

    void helloProblems()
    {
        QFETCH(QByteArray, frame);
        QFETCH(QString, code);
        RawClient client(server.serverPort());
        client.send(frame);

        QJsonObject reply;
        QVERIFY(client.waitReply(reply));
        QCOMPARE(reply.value("code").toString(), code);
    }

    void aTrustedSenderThatDoesNotKnowUsMustPairAgain()
    {
        // we know the sender, but the sender says it has forgotten us ("known": false): a plain "ok" would be unsafe
        RawClient client(server.serverPort());
        client.send(helloFrame("Test phone", false));

        QJsonObject reply;
        QVERIFY(client.waitReply(reply));
        QCOMPARE(reply.value("code").toString(), QString("pairing_off")); // and pairing mode is off
    }

    void pairingFromStrangerToFriend()
    {
        peerFp = QString(64, 'c');
        pairing -> start();
        const int pin = pairing -> pin();

        RawClient client(server.serverPort());
        client.send(helloFrame(QString("Lap\ttop\n\x01 of Ann"))); // the name is untrusted text and must come out cleaned

        QJsonObject reply;
        QVERIFY(client.waitReply(reply));
        QCOMPARE(reply.value("status").toString(), QString("pairing_required"));

        const QJsonObject result = runExchange(client, pin);
        QCOMPARE(result.value("type").toString(), QString("pair_result"));
        QCOMPARE(result.value("status").toString(), QString("ok"));

        QVERIFY(trust -> isTrusted(peerFp));
        QCOMPARE(trust -> nameOf(peerFp), QString("Lap top of Ann"));
        QCOMPARE(pairingEnded, QStringList{"paired"}); // one PIN pairs one device
        QVERIFY(!pairing -> active());

        // the same connection now carries a file
        const QByteArray body("after pairing");
        client.send(headerFrame("hi.txt", body.size()) + body + trailerFrame("file_end", sha256Hex(body)));
        QJsonObject fileReply;
        QVERIFY(client.waitReply(fileReply));
        QCOMPARE(fileReply.value("type").toString(), QString("file_result"));
        QCOMPARE(fileReply.value("status").toString(), QString("ok"));
        QTRY_COMPARE(filesOnDisk(), QStringList{"hi.txt"});
    }

    void aWrongPinFailsAndBurnsTheCode()
    {
        peerFp = QString(64, 'c');
        pairing -> start();
        const int pin = pairing -> pin();

        RawClient client(server.serverPort());
        client.send(helloFrame());
        QJsonObject reply;
        QVERIFY(client.waitReply(reply));

        const QJsonObject result = runExchange(client, (pin + 1) % 1000000);
        QCOMPARE(result.value("type").toString(), QString("pair_result"));
        QCOMPARE(result.value("code").toString(), QString("pairing_failed"));

        QVERIFY(!trust -> isTrusted(peerFp));
        QCOMPARE(pairingEnded, QStringList{"failed"});

        // the burnt code cannot be tried again, not even with the right PIN
        RawClient again(server.serverPort());
        again.send(helloFrame());
        QJsonObject second;
        QVERIFY(again.waitReply(second));
        QCOMPARE(second.value("code").toString(), QString("pairing_off"));
    }

    void onlyOneSenderMayPairAtATime()
    {
        peerFp = QString(64, 'c');
        pairing -> start();

        RawClient first(server.serverPort());
        first.send(helloFrame());
        QJsonObject reply;
        QVERIFY(first.waitReply(reply));
        QCOMPARE(reply.value("status").toString(), QString("pairing_required"));

        RawClient second(server.serverPort());
        second.send(helloFrame());
        QJsonObject busy;
        QVERIFY(second.waitReply(busy));
        QCOMPARE(busy.value("code").toString(), QString("busy"));

        QVERIFY(pairing -> active()); // being busy did not burn anything
    }

    void leavingBeforeTheExchangeKeepsTheCode()
    {
        peerFp = QString(64, 'c');
        pairing -> start();
        {
            RawClient first(server.serverPort());
            first.send(helloFrame());
            QJsonObject reply;
            QVERIFY(first.waitReply(reply));
            first.hangUp();
            QTest :: qWait(100);
        }
        QVERIFY(pairing -> active()); // nothing began, so the PIN lives on
        QVERIFY(pairingEnded.isEmpty());

        // and the place at the door is free again
        const int pin = pairing -> pin();
        RawClient second(server.serverPort());
        second.send(helloFrame());
        QJsonObject reply;
        QVERIFY(second.waitReply(reply));
        QCOMPARE(reply.value("status").toString(), QString("pairing_required"));
        QCOMPARE(runExchange(second, pin).value("status").toString(), QString("ok"));
    }

    void leavingInTheMiddleBurnsTheCode()
    {
        peerFp = QString(64, 'c');
        pairing -> start();
        const int pin = pairing -> pin();

        RawClient client(server.serverPort());
        client.send(helloFrame());
        QJsonObject reply;
        QVERIFY(client.waitReply(reply));

        runExchange(client, pin, 1); // sends round 0's commit and walks away
        client.hangUp();

        QTRY_COMPARE(pairingEnded, QStringList{"failed"});
        QVERIFY(!trust -> isTrusted(peerFp));
    }

    void anExpiredPinIsRefused()
    {
        peerFp = QString(64, 'c');
        timeouts.pinLifetimeMs = 150;
        rebuildPairing();
        pairing -> start();

        RawClient client(server.serverPort());
        client.send(helloFrame());
        QJsonObject reply;
        QVERIFY(client.waitReply(reply));
        QCOMPARE(reply.value("status").toString(), QString("pairing_required"));

        QJsonObject end; // the receiver tells the waiting sender that the PIN ran out
        QVERIFY(client.waitReply(end));
        QCOMPARE(end.value("type").toString(), QString("pair_result"));
        QCOMPARE(end.value("code").toString(), QString("pairing_timeout"));
        QCOMPARE(pairingEnded, QStringList{"expired"});
    }

    // ---- timeouts: nobody may hold a connection open by saying nothing ----
    void silenceAfterConnectingTimesOut()
    {
        timeouts.helloMs = 150;
        RawClient client(server.serverPort());
        QJsonObject reply;
        QVERIFY(client.waitReply(reply));
        QCOMPARE(reply.value("type").toString(), QString("hello_result"));
        QCOMPARE(reply.value("code").toString(), QString("timeout"));
    }

    void silenceWhileTypingThePinTimesOutAndKeepsTheCode()
    {
        peerFp = QString(64, 'c');
        timeouts.firstPairMessageMs = 150;
        rebuildPairing();
        pairing -> start();

        RawClient client(server.serverPort());
        client.send(helloFrame());
        QJsonObject reply, end;
        QVERIFY(client.waitReply(reply));
        QVERIFY(client.waitReply(end));
        QCOMPARE(end.value("code").toString(), QString("timeout"));
        QVERIFY(pairing -> active()); // the exchange never began: the PIN is not burnt
    }

    void silenceInTheMiddleOfTheExchangeTimesOutAndBurnsTheCode()
    {
        peerFp = QString(64, 'c');
        timeouts.pairMessageMs = 150;
        rebuildPairing();
        pairing -> start();
        const int pin = pairing -> pin();

        RawClient client(server.serverPort());
        client.send(helloFrame());
        QJsonObject reply;
        QVERIFY(client.waitReply(reply));

        runExchange(client, pin, 1); // commit of round 0, then silence
        QJsonObject commit, end;
        QVERIFY(client.waitReply(commit)); // the receiver's own commit
        QVERIFY(client.waitReply(end));
        QCOMPARE(end.value("code").toString(), QString("timeout"));
        QCOMPARE(pairingEnded, QStringList{"failed"});
    }

    void silenceInsteadOfTheFileTimesOut()
    {
        timeouts.idleMs = 150;
        auto clientPtr = connectTrusted();
        QJsonObject reply;
        QVERIFY(clientPtr -> waitReply(reply));
        QCOMPARE(reply.value("code").toString(), QString("timeout"));
        QTRY_COMPARE(failedReasons.size(), 1);
    }

    void aStalledBodyTimesOutAndLeavesNoFile()
    {
        timeouts.idleMs = 200;
        auto clientPtr = connectTrusted();
        clientPtr -> send(headerFrame("stall.bin", 1000) + QByteArray(100, 'x')); // promises 1000 bytes, sends 100

        QJsonObject reply;
        QVERIFY(clientPtr -> waitReply(reply));
        QCOMPARE(reply.value("code").toString(), QString("timeout"));
        QTRY_VERIFY(filesOnDisk().isEmpty());
    }

    void aFinishedFileDoesNotTriggerALateTimeout()
    {
        timeouts.idleMs = 150;
        auto clientPtr = connectTrusted();
        const QByteArray body("done");
        clientPtr -> send(headerFrame("fast.txt", body.size()) + body + trailerFrame("file_end", sha256Hex(body)));
        QJsonObject reply;
        QVERIFY(clientPtr -> waitReply(reply));
        QCOMPARE(reply.value("status").toString(), QString("ok"));

        QTest :: qWait(400); // longer than the idle limit
        QVERIFY(failedReasons.isEmpty());
        QCOMPARE(filesOnDisk(), QStringList{"fast.txt"});
    }
};

QTEST_GUILESS_MAIN(TestSession)
#include "test_session.moc"