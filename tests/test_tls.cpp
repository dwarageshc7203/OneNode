#include "identity.h"
#include "tlsconfig.h"

#include <QtTest>
#include <QSslServer>
#include <QSslSocket>
#include <QTemporaryDir>

// Two devices, A (receiver, listens) and B (sender, connects), on this machine.
// What we want to learn: do the handshake, TLS version and fingerprints behave as PROTOCOL.md says?
class TestTls : public QObject {

    Q_OBJECT

private:

    struct Device {
        QTemporaryDir dir;
        QSslCertificate cert;
        QSslKey key;
        QString fingerprint;

        Device ()
        {
            QString error;
            if (Identity :: loadOrCreate(dir.path(), cert, key, error))
                fingerprint = Identity :: fingerprint(cert);
        }
    };

    // A server that keeps its accepted socket in `accepted` once the handshake is done.
    struct Receiver {
        QSslServer server;
        QSslSocket *accepted = nullptr;

        Receiver (const Device &device)
        {
            server.setSslConfiguration(TlsConfig :: make(device.cert, device.key));
            // self-signed cards are expected: ignore that one complaint, we check fingerprints ourselves
            QObject :: connect(&server, &QSslServer :: sslErrors, &server,
                               &TlsConfig :: acceptSelfSigned);
            QObject :: connect(&server, &QSslServer :: pendingConnectionAvailable, &server,
                               [this] { accepted = static_cast<QSslSocket *>(server.nextPendingConnection()); });
            server.listen(QHostAddress :: LocalHost, 0);
        }
    };

private slots:

    void bothSidesSeeEachOthersFingerprint()
    {
        Device a, b;
        QVERIFY(!a.fingerprint.isEmpty() && !b.fingerprint.isEmpty());
        Receiver receiver(a);
        QVERIFY(receiver.server.isListening());

        QSslSocket client;
        client.setSslConfiguration(TlsConfig :: make(b.cert, b.key));
        connect(&client, &QSslSocket :: sslErrors, &client, [&client] (const QList<QSslError> &e) { TlsConfig :: acceptSelfSigned(&client, e); });
        QSignalSpy encrypted(&client, &QSslSocket :: encrypted);
        client.connectToHostEncrypted("127.0.0.1", receiver.server.serverPort());

        QTRY_COMPARE_WITH_TIMEOUT(encrypted.count(), 1, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(receiver.accepted && receiver.accepted->isEncrypted(), 5000);

        // the sender sees the receiver's fingerprint ...
        QCOMPARE(Identity :: fingerprint(client.peerCertificate()), a.fingerprint);
        // ... and the receiver sees the sender's
        QCOMPARE(Identity :: fingerprint(receiver.accepted->peerCertificate()), b.fingerprint);
    }

    void itReallyIsTls13()
    {
        Device a, b;
        Receiver receiver(a);

        QSslSocket client;
        client.setSslConfiguration(TlsConfig :: make(b.cert, b.key));
        connect(&client, &QSslSocket :: sslErrors, &client, [&client] (const QList<QSslError> &e) { TlsConfig :: acceptSelfSigned(&client, e); });
        client.connectToHostEncrypted("127.0.0.1", receiver.server.serverPort());

        QTRY_VERIFY_WITH_TIMEOUT(client.isEncrypted(), 5000);
        QCOMPARE(client.sessionProtocol(), QSsl :: TlsV1_3);
    }

    void dataFlowsBothWaysInsideTheTunnel()
    {
        Device a, b;
        Receiver receiver(a);

        QSslSocket client;
        client.setSslConfiguration(TlsConfig :: make(b.cert, b.key));
        connect(&client, &QSslSocket :: sslErrors, &client, [&client] (const QList<QSslError> &e) { TlsConfig :: acceptSelfSigned(&client, e); });
        client.connectToHostEncrypted("127.0.0.1", receiver.server.serverPort());
        QTRY_VERIFY_WITH_TIMEOUT(client.isEncrypted() && receiver.accepted && receiver.accepted->isEncrypted(), 5000);

        client.write("hello");
        QTRY_COMPARE_WITH_TIMEOUT(receiver.accepted->bytesAvailable(), 5, 5000);
        QCOMPARE(receiver.accepted->readAll(), QByteArray("hello"));

        receiver.accepted->write("hi back");
        QTRY_COMPARE_WITH_TIMEOUT(client.bytesAvailable(), 7, 5000);
        QCOMPARE(client.readAll(), QByteArray("hi back"));
    }

    void aSenderWithoutACertificateIsRefused()
    {
        Device a;
        Receiver receiver(a);

        // a client that shows no ID card at all
        QSslConfiguration noCard = QSslConfiguration :: defaultConfiguration();
        noCard.setProtocol(QSsl :: TlsV1_3);
        QSslSocket client;
        client.setSslConfiguration(noCard);
        connect(&client, &QSslSocket :: sslErrors, &client, [&client] (const QList<QSslError> &e) { TlsConfig :: acceptSelfSigned(&client, e); });
        client.connectToHostEncrypted("127.0.0.1", receiver.server.serverPort());

        // wait until the connection is over one way or the other
        QTest :: qWait(1500);
        // the receiver must never hand us over an encrypted, accepted connection
        QVERIFY(!(receiver.accepted && receiver.accepted->isEncrypted() && receiver.accepted->state() == QAbstractSocket :: ConnectedState));
    }
};

QTEST_MAIN(TestTls)
#include "test_tls.moc"
