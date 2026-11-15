#include "sendsession.h"
#include "identity.h"
#include "protocol.h"
#include "tlsconfig.h"
#include "truststore.h"

#include <QDebug>
#include <QFileInfo>
#include <QJsonObject>

// SendSession constructor
SendSession :: SendSession (const QString &host, quint16 port, const QString &filePath, const Services &services, QObject *parent) : QObject(parent), socket(new QSslSocket(this)), services(services), filePath(filePath), fileName(QFileInfo(filePath).fileName())
{
    socket -> setSslConfiguration(services.tls); // our ID card + TLS 1.3 only

    // "encrypted" (not "connected") is the moment we may start talking: the TLS handshake is finished
    connect(socket, &QSslSocket :: encrypted, this, &SendSession :: onEncrypted);
    // the other device's card is self-signed, so TLS complains; we forgive only that (see tlsconfig.h)
    connect(socket, &QSslSocket :: sslErrors, this, [this] (const QList<QSslError> &errors) { TlsConfig :: acceptSelfSigned(socket, errors); });
    connect(socket, &QTcpSocket :: disconnected, this, &SendSession :: onDisconnected);
    connect(socket, &QTcpSocket :: errorOccurred, this, &SendSession :: onSocketError);
    connect(socket, &QTcpSocket :: bytesWritten, this, &SendSession :: sendMore);
    connect(socket, &QTcpSocket :: readyRead, this, &SendSession :: onReadyRead);

    deadline.setSingleShot(true);
    exchangeTimer.setSingleShot(true);
    connect(&deadline, &QTimer :: timeout, this, &SendSession :: onDeadline);
    connect(&exchangeTimer, &QTimer :: timeout, this, &SendSession :: onDeadline);

    qDebug() << "Sender session: connecting to: " << host << port;
    socket -> connectToHostEncrypted(host, port); // connect, then do the TLS handshake

    // A wrong address often gets no answer at all (no "refused"), and the system would wait for minutes.
    // So: if the secure connection is still not ready after 10 s (connecting + handshake), report it and give up.
    // The timer belongs to this session (context "this"), so it disappears if the session is deleted earlier.
    QTimer :: singleShot(connectTimeoutMs, this, [this] {
        if (!socket -> isEncrypted() && !reported) {
            fail("No answer from the other device. Check the address, and that OneNode is open there.");
            socket -> abort();
            deleteLater();
        }
    });
}

// destructor
SendSession :: ~SendSession()
{
    qDebug() << "Sender session destroyed";
}

// TLS is ready. Look at the file first (no point in pairing for a file that cannot be read), then introduce ourselves.
void SendSession :: onEncrypted()
{
    const QFileInfo info(filePath); // gets the path's info

    if (!info.exists() || !info.isFile()) { // checks if the path exist and points to a file rather than a directory
        abortWith("Sender Session file not found: " + filePath);
        return;
    }

    file = new QFile(filePath, this);
    if (!file -> open(QIODevice :: ReadOnly)) { // checkes if the file has Read access
        const QString reason = file -> errorString(); // save the error before init the file to nullptr
        const QString name = file -> fileName();
        delete file;
        file = nullptr; // when the condition fails, we are reinitializing file to point towards nullptr and disconnect
        abortWith("Sender session cannot open this file: " + name + " error: " + reason);
        return;
    }
    totalSize = file -> size(); // update totalSize with the file's size in bytes

    // who is on the other end? Read from the certificate of the handshake, never from anything the other side says
    peerFingerprint = Identity :: fingerprint(socket -> peerCertificate());
    if (!TrustStore :: isValidFingerprint(peerFingerprint)) {
        abortWith("The other device did not present a certificate.");
        return;
    }
    // The nearby list only shows what a device SAYS about itself. If the certificate tells another story,
    // somebody is pretending: stop before anything (even our name) is sent.
    if (!services.expectedFingerprint.isEmpty() && services.expectedFingerprint != peerFingerprint) {
        abortWith("This is not the device it claims to be, so nothing was sent. Try again, or pair it again.");
        return;
    }

    QJsonObject hello;
    hello["v"] = Protocol :: version;
    hello["type"] = "hello";
    hello["name"] = services.ownName;
    hello["known"] = services.trust && services.trust -> isTrusted(peerFingerprint); // do WE have the other device on our list?
    socket -> write(Protocol :: encodeFrame(hello));

    state = State :: AwaitHelloResult;
    deadline.start(services.timeouts.helloMs);
}

void SendSession :: onDisconnected()
{
    fail("Sender session disconnected before reply; transfer failed");
    deleteLater();
}

void SendSession :: onSocketError(QAbstractSocket :: SocketError)
{
    fail(socket -> errorString());

    if (socket -> state() == QAbstractSocket :: UnconnectedState) {
        deleteLater();
    } else {
        socket -> abort(); // a failed handshake leaves the socket half open: close it (that also ends this session)
    }
}

void SendSession :: onDeadline()
{
    abortWith(peerName + " took too long to answer.");
}

// give up with a reason AND drop the connection
void SendSession :: abortWith (const QString &reason)
{
    fail(reason);
    deadline.stop();
    exchangeTimer.stop();
    socket -> disconnectFromHost();
}

// ---- reading what the receiver says ----

void SendSession :: onReadyRead()
{
    if (reported) { // exactly one reply per session
        return;
    }

    receivedBuffer.append(socket -> readAll());

    // there may be several frames in the buffer (the receiver's last pairing message and pair_result come together)
    while (!reported) {
        QJsonObject reply;
        const Protocol :: decodeStatus status = Protocol :: tryDecodeFrame(receivedBuffer, reply);
        if (status == Protocol :: decodeStatus :: NeedMoreData) {
            return;
        }
        if (status == Protocol :: decodeStatus :: Invalid) {
            abortWith("Invalid reply from receiver");
            return;
        }

        bool more = false;
        switch (state) {
        case State :: AwaitHelloResult: more = handleHelloResult(reply); break;
        case State :: Pairing:          more = handlePairMessage(reply);  break;
        case State :: AwaitPairResult:  more = handlePairResult(reply);   break;
        case State :: Sending:          handleFileResult(reply);          return;
        case State :: WaitingForPin:
        case State :: Connecting:
            // the receiver has nothing to say yet, except an error that ends the conversation (its own timeout, for example)
            if (reply.value("type").toString() == "pair_result" && reply.value("status").toString() == "error") {
                abortWith(friendlyError(reply.value("code").toString()));
            } else {
                abortWith("Unexpected message from receiver");
            }
            return;
        }
        if (!more) {
            return;
        }
    }
}

bool SendSession :: handleHelloResult (const QJsonObject &reply)
{
    if (reply.value("type").toString() != "hello_result" || reply.value("v").toInt() != Protocol :: version) {
        abortWith("Unexpected reply to hello");
        return false;
    }
    deadline.stop();

    if (reply.value("name").isString()) {
        peerName = TrustStore :: cleanName(reply.value("name").toString());
    }

    const QString result = reply.value("status").toString();

    if (result == "ok") {
        // We only believe "ok" for a device WE have on our list. Anything else would let an unknown device
        // (maybe an impostor at this address) receive our file.
        if (!services.trust || !services.trust -> isTrusted(peerFingerprint)) {
            abortWith("The other device is not paired with this one.");
            return false;
        }
        emit ready();
        beginFile();
        return false;
    }

    if (result == "pairing_required") {
        state = State :: WaitingForPin;
        emit pinNeeded(peerName); // the screen asks for the PIN; the answer comes back through submitPin()
        return false;
    }

    abortWith(friendlyError(reply.value("code").toString()));
    return false;
}

QString SendSession :: submitPin (const QString &text)
{
    if (state != State :: WaitingForPin || reported) {
        return "There is nothing to pair with right now.";
    }

    int pin = 0;
    if (!PairingEngine :: parsePin(text, pin)) {
        return "The PIN is 6 digits.";
    }

    engine = std :: make_unique<PairingEngine>(PairingEngine :: Role :: Sender, pin,
                                               PairingEngine :: fingerprintBytes(Identity :: fingerprint(services.tls.localCertificate())),
                                               PairingEngine :: fingerprintBytes(peerFingerprint));
    state = State :: Pairing;
    exchangeTimer.start(services.timeouts.pairTotalMs);
    driveEngine();
    return QString();
}

void SendSession :: cancelPairing()
{
    if (state == State :: WaitingForPin || state == State :: Pairing || state == State :: AwaitPairResult) {
        abortWith("Pairing was cancelled.");
    }
}

// Sends what the engine wants to send, then waits (with a short timer) for the other side.
void SendSession :: driveEngine()
{
    while (engine -> action() == PairingEngine :: Action :: SendCommit || engine -> action() == PairingEngine :: Action :: SendOpen) {
        socket -> write(Protocol :: encodeFrame(engine -> makeMessage()));
    }

    if (engine -> action() == PairingEngine :: Action :: Done) {
        state = State :: AwaitPairResult; // all 20 rounds passed on our side: now the receiver's verdict
    } else if (engine -> action() == PairingEngine :: Action :: Failed) {
        abortWith(friendlyError("pairing_failed"));
        return;
    }
    deadline.start(services.timeouts.pairMessageMs);
}

bool SendSession :: handlePairMessage (const QJsonObject &reply)
{
    // the receiver may end the exchange with an error instead of the message we expect
    if (reply.value("type").toString() == "pair_result") {
        abortWith(friendlyError(reply.value("code").toString()));
        return false;
    }
    if (!engine -> accept(reply)) {
        abortWith(friendlyError("pairing_failed"));
        return false;
    }
    driveEngine();
    return !reported;
}

bool SendSession :: handlePairResult (const QJsonObject &reply)
{
    deadline.stop();
    exchangeTimer.stop();

    if (reply.value("type").toString() != "pair_result" || reply.value("status").toString() != "ok") {
        abortWith(friendlyError(reply.value("code").toString()));
        return false;
    }

    // save the receiver BEFORE sending the file, so the next transfer needs no PIN
    const QString problem = services.trust ? services.trust -> add(peerFingerprint, peerName) : QString("no trust list");
    if (!problem.isEmpty()) {
        abortWith("Paired, but the list of paired devices could not be saved: " + problem);
        return false;
    }

    engine.reset();
    emit paired(peerFingerprint, peerName);
    emit ready();
    beginFile();
    return !reported;
}

// ---- sending the file (unchanged logic, now started by beginFile) ----

void SendSession :: beginFile()
{
    state = State :: Sending;

    // creating the JSON header as part of the protocol, with all it's fields
    QJsonObject header;
    header["v"] = Protocol :: version;
    header["type"] = "file";
    header["filename"] = fileName;
    header["size"] = totalSize;

    socket -> write(Protocol :: encodeFrame(header)); // writes the frame onto the connected session socket using Protocol method

    sendMore(); // after writing, make signal to send more bytes to fill the queue
}

// function to handle sending of chunks over the limited size queue
void SendSession :: sendMore()
{
    if (reported || state != State :: Sending) { // checking if the oepration is already completed, or we have not reached the file yet
        return;
    }

    if (file) { // when the operation is done or failed, file is set to nullptr. we are checking on that
        while (sentSize < totalSize && socket -> bytesToWrite() < maxQueuedBytes) { // checks whether there are bytes to transfer and if there is any space within the limit for more bytes
            const QByteArray chunk = file -> read (qMin (chunkSize, totalSize - sentSize)); // slice or partition of data of either defined size (chunkSize) or if only less bytes are available, then the remaining bytes

            if(chunk.isEmpty()) { // if the chunk of data is empty, it means there is no data to partition - signalling session break or unreadable file
                file -> close();
                delete file;
                file = nullptr;

                fail("Sender session file ended early or could not be read");

                socket -> disconnectFromHost();
                return;
            }

            hash.addData(chunk);
            socket -> write(chunk); // write chunk onto the socket
            sentSize += chunk.size(); // update sent size after writing
        }

        if (sentSize == totalSize) {
            qDebug() << "Sender session all " << sentSize << " bytes queued";

            QJsonObject trailer;
            trailer["v"] = Protocol :: version;
            trailer["type"] = "file_end";
            trailer["sha256"] = QString :: fromLatin1(hash.result().toHex());
            socket -> write(Protocol :: encodeFrame(trailer));
            file -> close();
            delete file;
            file = nullptr;
        }
    }

    // we tell how much bytes left the socket by finding (sent bytes - still waiting in queue bytes)
    const qint64 done = qMax<qint64>(0, sentSize - socket -> bytesToWrite()); // bytes actually handed to the network, not just queued

    if (progressThrottle.shouldEmit(done, totalSize)) {
        emit progress(fileName, done, totalSize);
    }
}

// checking out for a reply from the receiver - file received successfully or not
void SendSession :: handleFileResult (const QJsonObject &reply)
{
    if(reply.value("status").toString() == "ok") { // checking the value of the reply code
        reported = true;
        emit finished(fileName);
    }
    else {
        fail("Sender session transfer failed, code: " + reply.value("code").toString());
    }

    socket -> disconnectFromHost();
}

void SendSession :: fail(const QString &reason)
{
    if (reported) {
        return; // if the process is done, no need for declaring fail
    }

    reported = true;
    qDebug() << "Sender session failed: " << reason;
    emit failed(fileName, reason);
}

// Turns the receiver's error code into a sentence the user can act on.
QString SendSession :: friendlyError (const QString &code) const
{
    if (code == "pairing_off") {
        return "This device is not paired with " + peerName + ". On " + peerName + ", choose \"Pair a new device\", then send again.";
    }
    if (code == "busy") {
        return peerName + " is busy pairing with another device. Try again in a moment.";
    }
    if (code == "not_paired") {
        return peerName + " does not know this device yet.";
    }
    if (code == "pairing_failed") {
        return "Pairing failed: the PIN was wrong, or someone is interfering. On " + peerName + ", start pairing again to get a new PIN.";
    }
    if (code == "pairing_timeout") {
        return "The PIN has expired. On " + peerName + ", start pairing again to get a new PIN.";
    }
    if (code == "timeout") {
        return peerName + " took too long to answer.";
    }
    if (code == "unsupported") {
        return peerName + " uses a different version of OneNode.";
    }
    return "Sender session transfer failed, code: " + code;
}
