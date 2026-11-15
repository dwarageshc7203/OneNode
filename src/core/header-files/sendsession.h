#ifndef SENDSESSION_H
#define SENDSESSION_H

#include "pairingengine.h"
#include "progressthrottle.h"
#include "protocol.h"

#include <QObject>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QString>
#include <QFile>
#include <QTimer>
#include <QCryptographicHash>

#include <memory>

class TrustStore;

// One outgoing file, from the TLS handshake to the receiver's verdict (PROTOCOL.md section 4).
//
// Analogy: the visitor. First the visitor says "hello, I am X". If the host knows them, they hand over the parcel.
// If not, the host (who opened pairing mode) shows a code on their screen, the visitor's user types it in,
// they play the 20-round game, and from then on they know each other.
class SendSession : public QObject
{
    Q_OBJECT

public:

    struct Services {
        QSslConfiguration tls;   // our certificate, key and TLS rules (see tlsconfig.h)
        QString ownName;         // how we introduce ourselves in hello
        TrustStore *trust = nullptr; // owned by CoreBridge, must outlive the session
        QString expectedFingerprint; // when the user picked a device from the nearby list: who it CLAIMED to be (empty = no claim)
        Protocol :: Timeouts timeouts;
    };

    explicit SendSession (const QString &host, quint16 port, const QString &filePath, const Services &services, QObject *parent = nullptr);
    ~SendSession() override; // destructor

    // The user typed the PIN (only meaningful after pinNeeded). Returns "" or a message for the user.
    QString submitPin (const QString &text);
    // The user gave up on pairing
    void cancelPairing();

signals:
    void progress(const QString &fileName, const qint64 done, const qint64 total);
    void finished(const QString &fileName);
    void failed(const QString &fileName, const QString &reason);

    void pinNeeded (const QString &peerName);  // this device is not paired yet: ask the user for the 6-digit PIN shown on the other device
    void ready();                              // the other device accepts us: the file is about to go
    void paired (const QString &fingerprint, const QString &name); // we just paired with the other device

private:

    enum class State { Connecting, AwaitHelloResult, WaitingForPin, Pairing, AwaitPairResult, Sending };

    QSslSocket *socket; // sender connection, encrypted with TLS
    Services services;
    State state = State :: Connecting;
    QString peerFingerprint; // read from the TLS handshake
    QString peerName = "the other device"; // from hello_result
    QString filePath; // the file we are sending
    QString fileName;
    QFile *file = nullptr; // will point at the open file to be shared
    qint64 totalSize = 0; // size of the file to be shared in bytes
    qint64 sentSize = 0; // bytes sent from the sender session to the receiver
    static constexpr int connectTimeoutMs = 10000; // give up after 10 s if nobody answers
    static constexpr qint64 chunkSize = 64 * 1024; // 64 KiB per chunk - slice of data accessed in a particular iteration
    static constexpr qint64 maxQueuedBytes = 256 * 1024; // sets the upper limit on how much unwritten memory is allowed to sit in memory waiting for receiver
    QByteArray receivedBuffer; // to process the sending bytes
    bool reported = false; // status to signal outcome has been reported
    ProgressThrottle progressThrottle; // gate keeper that limits the number of signal to QML
    QCryptographicHash hash { QCryptographicHash :: Sha256}; //
    QTimer deadline;      // "give up if nothing happens by then" for the current phase
    QTimer exchangeTimer; // the whole pairing exchange must finish in time
    std :: unique_ptr<PairingEngine> engine;

    //methods

    void onEncrypted(); // TLS is ready: check the file, say hello
    void onDisconnected(); // disconnection handle
    void onSocketError(QAbstractSocket :: SocketError error); // catches sender errors
    void onDeadline(); // a phase took too long
    void onReadyRead(); // activates whenever there is any bytes on the buffer
    bool handleHelloResult (const QJsonObject &reply);
    bool handlePairMessage (const QJsonObject &reply);
    bool handlePairResult (const QJsonObject &reply);
    void handleFileResult (const QJsonObject &reply);
    void driveEngine(); // send what the engine wants to send, then wait for the other side
    void beginFile(); // hello phase done: send the file header and start the body
    void sendMore(); // signal to send bytes when some bytes left the queue
    void fail(const QString &reason); // handles the failed signal and updates the report variable (operation status handling)
    void abortWith(const QString &reason); // fail, then drop the connection
    QString friendlyError(const QString &code) const; // turns an error code of the receiver into a sentence for the user

};

#endif // SENDSESSION_H
