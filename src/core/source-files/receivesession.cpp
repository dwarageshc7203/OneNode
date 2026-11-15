#include "receivesession.h"
#include "protocol.h"
#include "netutil.h"
#include "pairingmode.h"
#include "truststore.h"

#include <QDebug>
#include <QJsonParseError>
#include <QJsonObject>
#include <QFileInfo>
#include <QDir>
#include <QString>
#include <QJsonDocument>
#include <QtEndian>

// Finding the valid filename for the arriving file
static QString uniquePath(const QString &path)
{
    auto taken = [] (const QString &p) {
        return QFileInfo :: exists(p) ||  QFileInfo :: exists(p + ReceiveSession :: partSuffix());
    };

    if (!taken(path)) {
        return path;
    }

    const QFileInfo info(path);
    const QString baseName = info.completeBaseName();
    const QString suffix = info.suffix();

    for (int i = 1; i < 1000; i++) {
        const QString name = suffix.isEmpty() ?
        QString("%1(%2)").arg(baseName).arg(i) :
        QString("%1(%2).%3").arg(baseName).arg(i).arg(suffix);

        const QString candidate = info.dir().filePath(name);
        if (!taken(candidate)) {
            return candidate;
        }
    }
    return path;
}

// Constructor
ReceiveSession :: ReceiveSession (QTcpSocket *clientSocket, const Services &services, QObject *parent) : QObject(parent), socket(clientSocket), services(services) {
    qDebug() << "Session created for " << prettyNetworkAddress(socket -> peerAddress());

    connect(socket, &QTcpSocket :: readyRead, this, &ReceiveSession :: readData);
    connect(socket, &QTcpSocket :: disconnected, this, &ReceiveSession :: onDisconnected);

    deadline.setSingleShot(true);
    exchangeTimer.setSingleShot(true);
    connect(&deadline, &QTimer :: timeout, this, &ReceiveSession :: onDeadline);
    connect(&exchangeTimer, &QTimer :: timeout, this, &ReceiveSession :: onDeadline);
    if (services.pairing) {
        connect(services.pairing, &PairingMode :: ended, this, &ReceiveSession :: onPairingEnded);
    }

    deadline.start(services.timeouts.helloMs); // the other device must say hello soon

    // With TLS 1.3 the sender may finish ITS side of the handshake and send "hello" before this session exists.
    // Those bytes are then already waiting in the socket, and no readyRead will ever announce them again.
    // So: look once, right after construction (queued, so the creator can finish connecting its signals first).
    QMetaObject :: invokeMethod(this, &ReceiveSession :: readData, Qt :: QueuedConnection);
}

// Destructor
ReceiveSession :: ~ReceiveSession()
{
    releaseAttempt(); // never leave the pairing door blocked
    if (incomingFile) {
        qDebug() << "Session destroyed mid-transfer, removing " << incomingFile -> fileName();
        incomingFile -> remove();
    }
    qDebug() << "Session Destroyed";
}

// called everytime when the bytes arrive
void ReceiveSession :: readData()
{
    if (closing) { // we decided to stop
        socket -> readAll(); // we are saving the bytes no where - just discarding the available bytes on the socket => to make sure we can maintain the connection and send the reply to sender
        return;
    }

    receivedBuffer.append(socket->readAll());

    // while a file is moving, every arrival restarts the "nothing for 30 s" countdown
    if (inFilePhase() && deadline.isActive()) {
        deadline.start(services.timeouts.idleMs);
    }

    while (processBuffer()) { // proceed with the next action with the current status

    }
}

// Disconnection handle
void ReceiveSession :: onDisconnected()
{
    releaseAttempt(); // left in the middle of pairing: burns the PIN if the exchange had begun

    if (incomingFile) {

        if (!closing) {
            emit failed(currentName, "Connection lost mid-transfer");
        }

        qDebug() << "Connection lost, file transfer is stopped midway for file: " << incomingFile -> fileName();
        incomingFile -> remove(); // close the file and remove the half written file from disk
        delete incomingFile;
        incomingFile = nullptr;
    }

    deleteLater();
}

// a phase took too long (slow-loris protection: nobody may hold a connection open by saying nothing)
void ReceiveSession :: onDeadline()
{
    reject("timeout", "The other device took too long");
}

// the PIN expired or the user cancelled pairing mode while this sender was still at the door
void ReceiveSession :: onPairingEnded (const QString &)
{
    if (holdsAttempt && !closing) {
        holdsAttempt = false; // the mode is already over, nothing left to release
        reject("pairing_timeout", "The PIN is no longer valid");
    }
}

// gives the place at the pairing door back; PairingMode burns the PIN itself if the exchange had begun
void ReceiveSession :: releaseAttempt()
{
    if (!holdsAttempt) {
        return;
    }
    holdsAttempt = false;
    if (services.pairing) {
        services.pairing -> attemptAbandoned();
    }
}

// ---- before the file: hello and pairing (PROTOCOL.md sections 4 and 5) ----

bool ReceiveSession :: handleHello()
{
    QJsonObject message;
    const Protocol :: decodeStatus status = Protocol :: tryDecodeFrame(receivedBuffer, message);
    if (status == Protocol :: decodeStatus :: NeedMoreData) {
        return false;
    }
    if (status == Protocol :: decodeStatus :: Invalid) {
        reject("invalid_header", "Invalid first message");
        return false;
    }

    // a device that jumps straight to "file" without introducing itself
    if (message.value("type").toString() == "file") {
        reject("not_paired", "A file was offered before hello");
        return false;
    }
    if (!message.value("v").isDouble() || message.value("v").toInt() != Protocol :: version) {
        reject("unsupported", "Unsupported protocol version");
        return false;
    }
    if (message.value("type").toString() != "hello" || !message.value("name").isString()) {
        reject("invalid_header", "First message must be hello with a name");
        return false;
    }

    peerName = TrustStore :: cleanName(message.value("name").toString()); // from the network: untrusted text, cleaned
    const bool senderKnowsUs = message.value("known").toBool(false);

    const bool trusted = services.trust && services.trust -> isTrusted(services.peerFingerprint);
    if (trusted && senderKnowsUs) {
        // an old friend, in both directions: straight to the file
        sendResult("ok", QString(), "hello_result");
        state = State :: AwaitPrefix;
        deadline.start(services.timeouts.idleMs);
        return true;
    }

    // not paired (or only one of us remembers the other): pairing is needed, and only works in pairing mode
    if (services.pairing && services.pairing -> beginAttempt()) {
        holdsAttempt = true;
        sendResult("pairing_required", QString(), "hello_result");
        state = State :: AwaitFirstPair;
        deadline.start(services.timeouts.firstPairMessageMs);
        return true;
    }

    reject((services.pairing && services.pairing -> active()) ? "busy" : "pairing_off", "Pairing refused");
    return false;
}

bool ReceiveSession :: handlePairMessage()
{
    QJsonObject message;
    const Protocol :: decodeStatus status = Protocol :: tryDecodeFrame(receivedBuffer, message);
    if (status == Protocol :: decodeStatus :: NeedMoreData) {
        return false;
    }
    if (status == Protocol :: decodeStatus :: Invalid) {
        reject("pairing_failed", "Invalid pairing message");
        return false;
    }

    if (state == State :: AwaitFirstPair) {
        // round 0 begins now: from here on a failure burns the PIN
        if (!services.pairing || !services.pairing -> active()) {
            reject("pairing_timeout", "The PIN is no longer valid");
            return false;
        }
        engine = std :: make_unique<PairingEngine>(PairingEngine :: Role :: Receiver, services.pairing -> pin(),
                                                   PairingEngine :: fingerprintBytes(services.peerFingerprint),
                                                   PairingEngine :: fingerprintBytes(services.ownFingerprint));
        services.pairing -> attemptStartedExchange();
        exchangeTimer.start(services.timeouts.pairTotalMs);
        state = State :: Pairing;
    }

    if (!engine -> accept(message)) {
        reject("pairing_failed", "A pairing check failed");
        return false;
    }

    driveEngine();
    return !closing && !receivedBuffer.isEmpty(); // more bytes already waiting: look at them too
}

// Sends everything the engine wants to send now, then waits (with a short timer) for the other side.
void ReceiveSession :: driveEngine()
{
    while (engine -> action() == PairingEngine :: Action :: SendCommit || engine -> action() == PairingEngine :: Action :: SendOpen) {
        socket -> write(Protocol :: encodeFrame(engine -> makeMessage()));
    }

    if (engine -> action() == PairingEngine :: Action :: Done) {
        finishPairing();
    } else if (engine -> action() == PairingEngine :: Action :: Failed) {
        reject("pairing_failed", "A pairing check failed");
    } else {
        deadline.start(services.timeouts.pairMessageMs); // each pairing message must arrive within 5 s
    }
}

void ReceiveSession :: finishPairing()
{
    exchangeTimer.stop();

    // remember the other device BEFORE telling it "ok", so a crash cannot leave one side paired and the other not
    const QString problem = services.trust ? services.trust -> add(services.peerFingerprint, peerName) : QString("no trust list");
    if (!problem.isEmpty()) {
        reject("write_failed", "Could not save the paired device: " + problem);
        return;
    }

    holdsAttempt = false;
    if (services.pairing) {
        services.pairing -> attemptSucceeded(services.peerFingerprint, peerName); // closes pairing mode, tells the screen
    }
    sendResult("ok", QString(), "pair_result");

    engine.reset();
    state = State :: AwaitPrefix; // the same connection now carries the file
    deadline.start(services.timeouts.idleMs);
}

// prefix handler
bool ReceiveSession :: handlePrefix()
{
    if (receivedBuffer.size() < 4) { // header must be the length of size 4. we are removing instances where buffer hasn't collected enough data
        return false;
    }

    expectedHeaderSize = qFromBigEndian<quint32>(receivedBuffer.constData());
    receivedBuffer.remove(0, 4);

    if (expectedHeaderSize == 0 || expectedHeaderSize > Protocol :: maxHeaderSize) {
        reject("invalid_header", "Invalid header size");
        return false;
    }

    state = State :: AwaitHeader;
    return true;
}

// header handler
bool ReceiveSession :: handleHeader()
{
    // waiting until we receive all the required bytes for the header
    if (receivedBuffer.size() < static_cast<qsizetype>(expectedHeaderSize)) {
        return false;
    }

    // Cutting out the buffer
    QByteArray jsonData = receivedBuffer.left(expectedHeaderSize);
    receivedBuffer.remove(0, expectedHeaderSize);

    QJsonParseError error;
    QJsonDocument document = QJsonDocument :: fromJson(jsonData, &error); // parsing the bytes into JSON and this creates the document

    // Checking if the bytes transformed into JSON is an actual object
    if (error.error != QJsonParseError :: NoError || !document.isObject())
    {
        reject("invalid_header", error.errorString());
        return false;
    }

    QJsonObject metaData = document.object(); // This stores the actual JSON header object

    // Checking version number from the Protocol header field from the JSON header object
    if (!metaData.value("v").isDouble() || metaData.value("v").toInt() != Protocol :: version) {
        reject("unsupported", "Unsupported protocol version");
        return false;
    }

    // Checking for unsupported "type" field
    if (metaData.value("type").toString() != "file") { // for the upcoming version, will include checks to allow "copied text" or others ...
        reject("unsupported", "Unsupported message type: " + metaData.value("type").toString());
        return false;
    }

    // Checking filename and size from the JSON header object
    if (!metaData.value("filename").isString() || !metaData.value("size").isDouble()) {
        reject("invalid_header", "Header is missing FileName or Size");
        return false;
    }

    QString fileName = metaData.value("filename").toString();
    currentName = fileName;
    const QJsonValue sizeValue = metaData.value("size");
    const qint64 fileSize = sizeValue.toInteger(-1); // converting the JSON value of size to Integer with a default value of -1 (for out of range numbers)

    // Validating fileName and preventing path traversal
    if (!sizeValue.isDouble()) {
        reject("invalid_header", "File size is not of Double type");
        return false;
    }

    if(fileSize < 0) {
        reject("invalid_header", "File size is not a whole number >= 0");
        return false;
    }

    QFileInfo fileInfo(fileName);

    if (
        fileInfo.fileName() != fileName ||
        fileName == "." || // rejecting special directory name - . -> current directory
        fileName == ".." // rejecting special directory name - .. -> parent directory
        ) {
        reject("invalid_filename", "Invalid fileName: " + fileName);
        return false;
    }

    qDebug() << "Session header is valid";

    // Initialization of Directory to save files
    const QString &destination = services.destinationDir;

    QDir dir;

    // Checking if the destination directory already exists. If not, create the directory
    if (!dir.exists(destination)) {
        dir.mkpath(destination);
    }

    qDebug() << "Received metaData: " << metaData;
    qDebug() << "File Name: " << metaData.value("filename").toString();
    qDebug() << "File Size: " << metaData.value("size").toInteger();

    // Saving file to the directory

    finalPath = uniquePath(QDir(destination).filePath(fileName));
    qDebug() << "Final path: " << finalPath;

    // creating the file (empty) ; partSuffix() method writes the suffix .onenode-part : refers to the file being partial and not fully written
    // when the file writing is complete, we remove the .part suffix
    incomingFile = new QFile(finalPath + partSuffix(), this); // fixing the data leak with "this"-> when the parent method is deleted, the child object (this QFile) is also deleted(closed to be specific) along with it, making sure no data leak

    if(!incomingFile -> open(QIODevice :: WriteOnly)) { // write failed
        const QString reason = incomingFile -> errorString();
        reject("write_failed", "Destination file couldn't be accessed: " + reason);
        delete incomingFile;
        incomingFile = nullptr;
        return false;
    }

    qDebug() << "Destination file accessed successfully";

    expectedSize = fileSize;
    receivedSize = 0;

    hash.reset();
    state = State :: ReceiveBody; // Change the value of state to ReceiveBody

    qDebug() << "Receiving " << fileName << " of size " << fileSize << " bytes";

    emit progress(currentName, 0, fileSize);
    return true;
}

void ReceiveSession :: finishFile()
{
    incomingFile -> close();
    const QString partPath = incomingFile -> fileName();
    delete incomingFile;
    incomingFile = nullptr;

    expectedSize = 0;
    expectedHeaderSize = 0;
    receivedSize = 0;
    state = State :: AwaitPrefix; // setting the bookmark to the first progress again - basically i am resetting it
    deadline.stop(); // the file is complete: no more "nothing arrives" countdown

    if (!QFile::rename(partPath, finalPath)) {
        QFile :: remove(partPath); // removing the .onenode-part file (partially written file) from the disk
        reject("write_failed", "Could not rename " + partPath + " to " + finalPath);
        return;
    }

    qDebug() << "File renamed from " << partPath << " to " << finalPath;
    sendResult("ok");

    emit finished(currentName);

    qDebug() << "File received successfully";
}

bool ReceiveSession :: handleBody()
{
    // bytes left to write - from the header data
    const qint64 remaining = expectedSize - receivedSize;
    const qint64 toWrite = qMin(static_cast<qint64>(receivedBuffer.size()), remaining); // checking if all the required number of data is in buffer. if yes, do the body fully. if only partial, do everything in the buffer

    if (toWrite > 0) {
        const qint64 written = incomingFile->write(receivedBuffer.constData(), toWrite); // write the required amount of body for the file

        if (written < 0){
            reject("write_failed", "File write failed: " + incomingFile -> errorString());
            return false;
        }

        hash.addData(QByteArrayView(receivedBuffer.constData(), written)); // fingerprint the bytes we write
        receivedSize += written; // updating the written bytes
        receivedBuffer.remove(0,written);

        if (progressThrottle.shouldEmit(receivedSize, expectedSize)) {
            emit progress (currentName, receivedSize, expectedSize);
        }
    }

    if (receivedSize == expectedSize) {
        incomingFile -> close();
        state = State :: AwaitTrailer;
        return true;
    }
    return false;
}

bool ReceiveSession :: processBuffer()
{
    switch (state) {
    case State :: AwaitHello:
        return handleHello();

    case State :: AwaitFirstPair:
    case State :: Pairing:
        return handlePairMessage();

    case State :: AwaitPrefix:
        return handlePrefix();

    case State :: AwaitHeader:
        return handleHeader();

    case State :: ReceiveBody:
        return handleBody();

    case State :: AwaitTrailer:
        return handleTrailer();
    }

    return false;
}

// sends a reply to the sender. `type` is hello_result / pair_result / file_result; empty means "the one that fits the phase".
void ReceiveSession :: sendResult(const QString &status, const QString &code, const QString &type)
{
    QString replyType = type;
    if (replyType.isEmpty()) {
        replyType = state == State :: AwaitHello ? "hello_result"
                  : (state == State :: AwaitFirstPair || state == State :: Pairing) ? "pair_result"
                  : "file_result";
    }

    QJsonObject reply;
    reply["v"] = Protocol :: version;
    reply["type"] = replyType;
    reply["status"] = status;

    if (replyType == "hello_result") {
        reply["name"] = services.ownName; // so the other device can show who it reached (and save our name when pairing)
    }
    if (!code.isEmpty()) { // only when there is an error, code is added
        reply["code"] = code;
    }

    socket -> write(Protocol :: encodeFrame(reply)); // queues it for sending
}

// rejection handler (yes, it is, not alcohol)
void ReceiveSession :: reject(const QString &code, const QString &reason)
{
    if (closing) {
        return;
    }

    closing = true;
    deadline.stop();
    exchangeTimer.stop();
    qDebug() << "Rejected: " << code << reason;

    // A device that is not (yet) trusted must not be able to put anything on our screen:
    // for it we only log. Once trusted, a failure is a real event the user may want to see.
    if (inFilePhase()) {
        emit failed(currentName, reason);
    } else {
        emit refused(code, reason);
    }

    // "not_paired" is the answer to a file offered too early, so it goes in a file_result
    sendResult("error", code, code == "not_paired" ? QString("file_result") : QString());
    releaseAttempt(); // after the reply, so the PIN burns only when the exchange really began
    socket -> disconnectFromHost();
}

// handles the protocol's file_end
bool ReceiveSession :: handleTrailer()
{
    QJsonObject trailer;
    const Protocol :: decodeStatus status = Protocol :: tryDecodeFrame(receivedBuffer, trailer); // send the bytes from the buffer, decode that, and place it in the JSON object trailer

    if (status == Protocol :: decodeStatus :: NeedMoreData) { // waiting for more data
        return false;
    }

    if (status == Protocol :: decodeStatus :: Invalid) {
        reject("invalid_header", "Invalid trailer frame");
        return false;
    }
    
    //we need the type "file_end" with the value of "sha256" as String for the trailer. Else, invalid
    if (trailer.value("type").toString() != "file_end" || !trailer.value("sha256").isString()) {
        reject("invalid_header", "Trailer must be type file_end with a sha256 string");
        return false;
    }

    const QString expected = trailer.value("sha256").toString().toLower(); // sender's claim; normalizing to lowercase to compare (hexadecimal is case insensitive, we can use uppercase too)
    const QString actual = QString :: fromLatin1(hash.result().toHex()); // returns the fingerprint as hex by converting the ByteArray into QString

    if (expected != actual) {
        reject("checksum_mismatch", "Checksum mismatch: sender said " + expected + ", received file is " + actual);
        return false;
    }

    qDebug() << "Checksum verified: " << actual;
    finishFile(); // renames and replies ok
    return false;
}
