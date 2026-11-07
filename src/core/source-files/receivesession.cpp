#include "receivesession.h"
#include "protocol.h"
#include "netutil.h"

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
ReceiveSession :: ReceiveSession (QTcpSocket *clientSocket, const QString &destinationDir, QObject *parent) : QObject(parent), socket(clientSocket), destinationDir (destinationDir) {
    qDebug() << "Session created for " << prettyNetworkAddress(socket -> peerAddress());

    connect(socket, &QTcpSocket :: readyRead, this, &ReceiveSession :: readData);
    connect(socket, &QTcpSocket :: disconnected, this, &ReceiveSession :: onDisconnected);
}

// Destructor
ReceiveSession :: ~ReceiveSession()
{
    if (incomingFile) {
        qDebug() << "Session destroyed mid-transfer, removing " << incomingFile -> fileName();
        incomingFile -> remove();
    }
    qDebug() << "Session Destroyed";
}

// Reads data once a connection is instantiated, and a session is created
void ReceiveSession :: readData()
{
    if (closing) {
        socket -> readAll();
        return;
    }

    receivedBuffer.append(socket->readAll());

    while (processBuffer()) {

    }
}

// Disconnection handle
void ReceiveSession :: onDisconnected()
{
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

bool ReceiveSession :: handleHeader()
{
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
    if (fileName.isEmpty()) {
        reject("invalid_filename", "Filename empty");
        return false;
    }

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
    const QString &destination = destinationDir;

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

    if (!QFile::rename(partPath, finalPath)) {
        QFile :: remove(partPath); // removing the .part file (partially written file) from the disk
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

// sends acknowledgement result to the sender - handled by onReadyRead()
void ReceiveSession :: sendResult(const QString &status, const QString &code)
{
    QJsonObject reply;
    reply["v"] = Protocol :: version;
    reply["type"] = "file_result";
    reply["status"] = status;

    if (!code.isEmpty()) { // only when there is an error, code is added
        reply["code"] = code;
    }

    const QByteArray frame = Protocol :: encodeFrame(reply);

    qDebug() << "Sending frames: " << frame.toHex(' ');
    socket -> write(frame); // queues it for sending

    // in short, this function does this:
    // Init a JSON object and it's protocol format
    // convert it into text (byte array) with no extra spaces or lines
    // then creates a byte array and init with 4 bytes and 0's
    // adds the JSON object's size (as BigEndian) into the byte array
    // appends the JSON object's text as bytes to the existing byte array
    // sends the byte array to the socket to send to the client (from receiver)
}

// rejection handler (yes, it is, not alcohol)
void ReceiveSession :: reject(const QString &code, const QString &reason)
{
    if (closing) {
        return;
    }

    closing = true;
    qDebug() << "Rejected: " << reason;
    emit failed(currentName, reason);

    sendResult("error", code);
    socket -> disconnectFromHost();
}

bool ReceiveSession :: handleTrailer()
{
    QJsonObject trailer;
    const Protocol :: decodeStatus status = Protocol :: tryDecodeFrame(receivedBuffer, trailer);

    if (status == Protocol :: decodeStatus :: NeedMoreData) {
        return false;
    }
    if (status == Protocol :: decodeStatus :: Invalid) {
        reject("invalid_header", "Invalid trailer frame");
        return false;
    }
    if (trailer.value("type").toString() != "file_end" || !trailer.value("sha256").isString()) {
        reject("invalid_header", "Trailer must be type file_end with a sha256 string");
        return false;
    }

    const QString expected = trailer.value("sha256").toString().toLower();
    const QString actual = QString :: fromLatin1(hash.result().toHex());

    if (expected != actual) {
        reject("checksum_mismatch", "Checksum mismatch: sender said " + expected + ", received file is " + actual);
        return false;
    }

    qDebug() << "Checksum verified: " << actual;
    finishFile(); // renames and replies ok
    return false; // nothing more to parse for this file
}
