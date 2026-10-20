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
    QTcpSocket *client = qobject_cast<QTcpSocket *>(sender ());

    if (!client) {
        return;
    }

    QByteArray data = client -> readAll();
    qDebug() << "Client received: " << data;

    headerBuffer.append(data);

    if (expectedHeaderSize == 0 && headerBuffer.size() >= 4)
    {
        expectedHeaderSize =
            (static_cast<quint32> (static_cast<unsigned char> (headerBuffer[0])) << 24) | // first set 2 bytes
            (static_cast<quint32> (static_cast<unsigned char> (headerBuffer[1])) << 16) | // second set of 2 bytes
            (static_cast<quint32> (static_cast<unsigned char> (headerBuffer[2])) << 8) | // third set of 2 bytes
            (static_cast<quint32> (static_cast<unsigned char> (headerBuffer[3])) ) // last set of 2 bytes
            ; // static_cast<unsigned char>() converts the byte into unsigned (positive) 8 bit value
        // << - this is a bitwise left shift operator
        // for positive int, the value of << n equals 2^n (for example, a << 24 == 2^24 => 6,777,216
        headerBuffer.remove(0, 4); // removing the prefix to only have the JSON header now
        qDebug() << "Expected JSON size: " << expectedHeaderSize;

        // Handling large or invalid header size
        if (expectedHeaderSize == 0 || expectedHeaderSize > 16384) { // 16 KiB = 16384 -> standard norm; also protects unnecessary wait for large header sizes
            qDebug() << "Invalid header size: " << expectedHeaderSize;
            client -> disconnectFromHost();
            return;
        }

        // Handling incomplete packet delivery
        if (headerBuffer.size() < expectedHeaderSize) {
            return;
        }

        // Handling File and Errors
        QByteArray jsonData = headerBuffer.left(expectedHeaderSize);
        headerBuffer.remove(0, expectedHeaderSize);

        QJsonParseError error;
        QJsonDocument document = QJsonDocument :: fromJson(jsonData, &error); // parsing the bytes into JSON and this creates the document

        if (error.error != QJsonParseError :: NoError || !document.isObject())
        {
            qDebug() << "Invalid JSON header: " << error.errorString();
            client -> disconnectFromHost();
            return;
        }

        QJsonObject metaData = document.object(); // This stores the actual JSON header object

        // Validating metaData for the fileName and size fields and check if they are valid
        if(
            !metaData.contains("filename") ||
            !metaData.value("filename").isString() ||
            !metaData.contains("size") ||
            !metaData.value("size").isDouble()
            ) {
            qDebug() << "Invalid JSON: ";
            client -> disconnectFromHost();
            return;
        }

        // Checking valid metaData if their fileName and size fields are empty
        if (
            metaData.value("filename").toString().isEmpty() ||
            metaData.value("size").toInteger() < 0
            ) {
            qDebug() << "Empty JSON fields: ";
            client -> disconnectFromHost();
            return;
        }

        QString fileName = metaData.value("filename").toString();
        qint64 fileSize = metaData.value("size").toInteger();

        // Validating fileName and preventing path traversal
        // We just want the fileName, do

        QFileInfo fileInfo(fileName);

        if (
            fileInfo.fileName() != fileName ||
            fileName == "." || // rejecting special directory name - . -> current directory
            fileName == ".." // rejecting special directory name - .. -> parent directory
            ) {
            qDebug() << "Invalid fileName: " << fileName;
            client -> disconnectFromHost();
            return;
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

        // Writing the file
        incomingFile = new QFile(savedPath);
        if(!incomingFile -> open(QIODevice :: WriteOnly)) {
            qDebug() << "Destination file couldn't be accessed: " << incomingFile -> errorString();

            delete incomingFile;
            incomingFile = nullptr;
            client -> disconnectFromHost();
            return;
        }

        qDebug() << "Destination file accessed successfully";

        expectedSize = fileSize;
        receivedSize = 0;

        qDebug() << "Expected file size: " << expectedSize;


    }
}







