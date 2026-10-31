#include "corebridge.h"
#include <QTcpSocket>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QFile>

// Constructor - member initializer list
// I am instantiating the QObject and the QTcpServer objects here

CoreBridge :: CoreBridge (QObject *parent) : QObject (parent), server (new QTcpServer (this))
{
    // connect server's signal to handler method
    connect(server, &QTcpServer :: newConnection, this, &CoreBridge :: handleNewConnection);
}

bool CoreBridge :: startListening()
{
    return server -> listen (QHostAddress :: Any, 45454);
}

void CoreBridge :: stopListening()
{
    server -> close();
}

void CoreBridge :: handleNewConnection()
{
    QTcpSocket *client = server -> nextPendingConnection();

    // Checking null pointer when no connection available
    if (!client) {
        return;
    }

    clients.append(client);

    // Connect handle
    qDebug() << "A client has connected: " << client -> peerAddress().toString();

    connect(client, &QTcpSocket :: readyRead, this, &CoreBridge :: readClientData);

    // Disconnect handle
    connect (client, &QTcpSocket :: disconnected, this, [this, client] {
        qDebug() << "The client has disconnected: " << client -> peerAddress().toString();

        clients.removeOne(client);
        client -> deleteLater();
    });
}

void CoreBridge :: readClientData()
{
    QTcpSocket *client = qobject_cast<QTcpSocket *>(sender());

    if (!client) {
        return;
    }

    QByteArray data = client -> readAll();
    qDebug() << "Client received: " << data;

    headerBuffer.append(data);

    while (processBuffer(client)) {

    }
}

bool CoreBridge :: handlePrefix(QTcpSocket *client)
{
    if (headerBuffer.size() < 4) {
        return false;
    }

    expectedHeaderSize = qFromBigEndian<quint32>(headerBuffer.constData());
    headerBuffer.remove(0, 4);

    if (expectedHeaderSize == 0 || expectedHeaderSize > 16384) { // 16 KiB = 16384 -> standard norm; also protects unnecessary wait for large header sizes
        qDebug() << "Invalid header size: " << expectedHeaderSize;
        client -> disconnectFromHost();
        return false;
    }

    state = State :: AwaitHeader;
    return true;
}

bool CoreBridge :: handleHeader(QTcpSocket *client)
{
    if (headerBuffer.size() < static_cast<qsizetype>(expectedHeaderSize)) {
        return false;
    }

    // Cutting out the buffer
    QByteArray jsonData = headerBuffer.left(expectedHeaderSize);
    headerBuffer.remove(0, expectedHeaderSize);

    QJsonParseError error;
    QJsonDocument document = QJsonDocument :: fromJson(jsonData, &error); // parsing the bytes into JSON and this creates the document

    // Checking if the bytes transformed into JSON is an actual object
    if (error.error != QJsonParseError :: NoError || !document.isObject())
    {
        qDebug() << "Invalid JSON header: " << error.errorString();
        client -> disconnectFromHost();
        return false;
    }

    QJsonObject metaData = document.object(); // This stores the actual JSON header object

    if (!metaData.value("filename").isString() || !metaData.value("size").isDouble()) {
        qDebug() << "Header is missing FileName or Size";
        client -> disconnectFromHost();
        return false;
    }

    QString fileName = metaData.value("filename").toString();
    qint64 fileSize = metaData.value("size").toInteger();

    // Validating fileName and preventing path traversal
    if (fileName.isEmpty() || fileSize < 0) {
        qDebug() << "Filename empty or size is negative ";
        client -> disconnectFromHost();
        return false;
    }

    QFileInfo fileInfo(fileName);

    if (
        fileInfo.fileName() != fileName ||
        fileName == "." || // rejecting special directory name - . -> current directory
        fileName == ".." // rejecting special directory name - .. -> parent directory
        ) {
        qDebug() << "Invalid fileName: " << fileName;
        client -> disconnectFromHost();
        return false;
    }

    // Initialization of Directory to save files
    QString destination = QDir :: homePath() + "/Downloads/OneNode";

    QDir dir;

    // Checking if the destination directory already exists. If not, create the directory
    if (!dir.exists(destination)) {
        dir.mkpath(destination);
    }

    qDebug() << "Received metaData: " << metaData;
    qDebug() << "File Name: " << metaData.value("filename").toString();
    qDebug() << "File Size: " << metaData.value("size").toInteger();

    // Saving file to the directory

    QString savedPath = QDir(destination).filePath(fileName);
    qDebug() << "Saved path: " << savedPath;

    // creating the file (empty)
    incomingFile = new QFile(savedPath);
    if(!incomingFile -> open(QIODevice :: WriteOnly)) {
        qDebug() << "Destination file couldn't be accessed: " << incomingFile -> errorString();

        delete incomingFile;
        incomingFile = nullptr;
        client -> disconnectFromHost();
        return false;
    }

    qDebug() << "Destination file accessed successfully";

    expectedSize = fileSize;
    receivedSize = 0;
    state = State :: ReceiveBody; // Change the value of state to ReceiveBody

    qDebug() << "Recevied " << fileName << " of size " << fileSize << " bytes";

    return true;
}

// actual file writing process
bool CoreBridge :: handleBody()
{
    // bytes left to write - from the header data
    const qint64 remaining = expectedSize - receivedSize;
    const qint64 toWrite = qMin(static_cast<qint64>(headerBuffer.size()), remaining); // checking if all the required number of data is in buffer. if yes, do the body fully. if only partial, do everything in the buffer

    if (toWrite > 0) {
        const qint64 written = incomingFile->write(headerBuffer.constData(), toWrite); // write the required amount of body for the file

        if (written < 0){
            qDebug() << "FIle write failed: " << incomingFile -> errorString();
            return false;
        }

        receivedSize += toWrite; // updating the written bytes
        headerBuffer.remove(0,written);
    }

    if (receivedSize == expectedSize) {
        finishFile();
        return true;
    }
    return false;
}

// dispatching function based on the state
bool CoreBridge ::  processBuffer(QTcpSocket *client)
{
    switch (state) {
    case State :: AwaitPrefix:
        return handlePrefix(client);

    case State :: AwaitHeader:
        return handleHeader(client);

    case State :: ReceiveBody:
        return handleBody();
    }

    return false;
}

// file closing and reinit
void CoreBridge :: finishFile()
{
    incomingFile -> close();
    delete incomingFile;
    incomingFile = nullptr;

    expectedSize = 0;
    expectedHeaderSize = 0;
    receivedSize = 0;
    state = State :: AwaitPrefix; // setting the bookmark to the first progress again - basically i am resetting it

    qDebug() << "File received successfully";
}
