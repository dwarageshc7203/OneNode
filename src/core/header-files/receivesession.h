#ifndef RECEIVESESSION_H
#define RECEIVESESSION_H

#include "pairingengine.h"
#include "progressthrottle.h"
#include "protocol.h"

#include <QObject>
#include <QTcpSocket>
#include <QFile>
#include <QString>
#include <QTimer>
#include <QCryptographicHash>

#include <memory>

class TrustStore;
class PairingMode;

// One connection from another device, from "hello" to the end of one file (PROTOCOL.md section 4).
//
// Analogy: the person at the door of a flat. First they ask "who are you?" (hello). A known guest
// walks in. A stranger is only let in if the owner has opened pairing mode and the stranger knows the code word.
// Only then does the guest hand over a parcel (the file).
class ReceiveSession : public QObject
{
    Q_OBJECT

public:

    // Everything the session needs from the outside, collected in one place.
    // The pointers are owned by whoever creates the session (CoreBridge) and must outlive it.
    struct Services {
        QString destinationDir;      // where received files go
        QString ownFingerprint;      // this device, 64 hex characters
        QString ownName;             // shown to the other device
        QString peerFingerprint;     // the other device, read from the TLS handshake (never from a message)
        TrustStore *trust = nullptr;
        PairingMode *pairing = nullptr;
        Protocol :: Timeouts timeouts;
    };

    explicit ReceiveSession (QTcpSocket *clientSocket, const Services &services, QObject *parent = nullptr);
    ~ReceiveSession() override; // destructor

    // helper function for the suffix for the file being written
    static QString partSuffix() {
        return QStringLiteral(".onenode-part"); // the suffix for the file being written
    }

signals:
    // about a file (only after the other device is trusted)
    void progress(const QString &fileName, const qint64 done, const qint64 total);
    void finished(const QString &fileName);
    void failed(const QString &fileName, const QString &reason);

    // about a device that is NOT (yet) trusted: refused, timed out, failed pairing ...
    // Never shown on screen (a stranger must not be able to make anything appear), only logged.
    void refused(const QString &code, const QString &reason);

private:

    // Where the conversation is. The first three are "before the file", the last four are the file itself.
    enum class State { AwaitHello, AwaitFirstPair, Pairing, AwaitPrefix, AwaitHeader, ReceiveBody, AwaitTrailer };

    QTcpSocket *socket; // this is the connection
    Services services;
    QString peerName; // from hello, already cleaned
    QFile *incomingFile = nullptr; // Points to the file or image sent as an object
    qint64 expectedSize = 0; // Size of the image or file before sending
    qint64 receivedSize = 0; // Size of the image after receiving
    QByteArray receivedBuffer; // Stores the bytes of Header length prefix, JSON header and the body
    quint32 expectedHeaderSize = 0; // Stores the size of the JSON header
    QCryptographicHash hash {QCryptographicHash :: Sha256}; // fingerprint of the body bytes
    State state = State :: AwaitHello;
    QString finalPath; // Actual and valid name the file gets after completing all the checks
    QString currentName; // file name derived from the header
    bool closing = false; // allows no other action ; much like operation is closed
    ProgressThrottle progressThrottle; // gate keeper that limits the amount of signal sent to QML

    QTimer deadline;      // "if nothing happens by then, give up" for the current phase
    QTimer exchangeTimer; // the whole pairing exchange must finish within pairTotalMs
    std :: unique_ptr<PairingEngine> engine;
    bool holdsAttempt = false; // true while we occupy the single place at the pairing door

    //methods

    void onDisconnected(); // disconnection handle
    void onDeadline(); // a phase took too long
    void onPairingEnded(const QString &reason); // the PIN expired or was cancelled while we were waiting

    bool handleHello(); // first message of every conversation
    bool handlePairMessage(); // one message of the pairing exchange
    void driveEngine(); // sends what the engine wants to send, then waits for the next message
    void finishPairing(); // the 20 rounds passed
    void releaseAttempt(); // give the place at the pairing door back (burns the PIN if the exchange had begun)

    bool handlePrefix(); // prefix handler
    bool handleHeader(); // header handler
    void finishFile(); // checks if the file is received successfully and handles file writing
    bool handleBody(); // handles body bytes and hashing
    bool processBuffer(); // switch case for the different states
    void readData(); // managing and reading data on the socket and appending to buffer with condition
    void sendResult(const QString &status, const QString &code = QString(), const QString &type = QString()); // reply to the sender
    void reject(const QString &code, const QString &reason); // fail, tell the sender once, disconnect
    bool inFilePhase() const { return state == State :: AwaitPrefix || state == State :: AwaitHeader || state == State :: ReceiveBody || state == State :: AwaitTrailer; }
    bool handleTrailer(); // handles the trailer(file_end) and checksum after the body is received

};

#endif // RECEIVESESSION_H
