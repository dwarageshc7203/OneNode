#include "sendsession.h"

#include <QDebug>
#include <QFileInfo>
#include <QJsonObject>
#include <QJsonDocument>
#include <QtEndian>

// SendSession constructor
SendSession :: SendSession (const QString &host, quint16 port, const QString &filePath, QObject *parent) : QObject(parent), socket(new QTcpSocket(this)), filePath(filePath)
{
    connect(socket, &QTcpSocket :: connected, this, &SendSession :: onConnected);
    connect(socket, &QTcpSocket :: disconnected, this, &SendSession :: onDisconnected);
    connect(socket, &QTcpSocket :: errorOccurred, this, &SendSession :: onSocketError);
    connect(socket, &QTcpSocket :: bytesWritten, this, &SendSession :: sendMore);

    qDebug() << "Sender session: connecting to: " << host << port;
    socket -> connectToHost(host, port);
}

SendSession :: ~SendSession()
{
    qDebug() << "Sender session destroyed";
}

void SendSession :: onConnected()
{
    const QFileInfo info(filePath); // gets the path's info

    if (!info.exists() || !info.isFile()) { // checks if the path exist and points to a file rather than a directory
        qDebug() << "Sender Session file not found: " << filePath;
        socket -> disconnectFromHost();
    }

    file = new QFile(filePath, this);
    if (!file -> open(QIODevice :: ReadOnly)) { // checkes if the file has Read access
        qDebug() << "Sender session cannot open this file: " << file -> fileName() << " error: " << file -> errorString();
        delete file;
        file = nullptr; // when the condition fails, we are reinitializing file to point towards nullptr and disconnect
        socket -> disconnectFromHost();
        return;
    }

    totalSize = file -> size(); // update totalSize with the file's size in bytes

    // creating the JSON header as part of the protocol, with all it's fields
    QJsonObject header;
    header["v"] = currentVersionNumber;
    header["type"] = "file";
    header["filename"] = info.fileName();
    header["size"] = info.size();

    // converts the header into JSON (text) and removes unnecessary spaces and lines to make it 'compact'
    const QByteArray json = QJsonDocument(header).toJson(QJsonDocument :: Compact);

    QByteArray frame(4, '\0'); // creates an empty byte array of set of 4 bytes, all '00'
    qToBigEndian<quint32>(static_cast<quint32>(json.size()), frame.data()); // adds the json.size() as quint32 type into frame array
    frame.append(json); // appends json with frame

    qDebug() << "Sender session sending header: " << info.fileName() << " size " <<info.size();
    socket -> write(frame); // writes the frame onto the connected session socket

    sendMore(); // after writing, make signal to send more bytes to fill the queue
}

void SendSession :: onDisconnected()
{
    qDebug() << "Sender session disconnected";
    deleteLater();
}

void SendSession :: onSocketError(QAbstractSocket :: SocketError)
{
    qDebug() << "Sender session socket error: " << socket -> errorString();
}

void SendSession :: sendMore()
{
    if (!file) {
        return;
    }

    while (sentSize < totalSize && socket -> bytesToWrite() < maxQueuedBytes) { // checks whether there are bytes to transfer and if there is any space within the limit for more bytes
        const QByteArray chunk = file -> read (qMin (chunkSize, totalSize - sentSize)); // slice or partition of data of either defined size (chunkSize) or if only less bytes are available, then the remaining bytes

        if(chunk.isEmpty()) { // if the chunk of data is empty, it means there is no data to partition - signalling session break or unreadable file
            qDebug() << "Sender session file ended early or could not be read";
            file -> close();
            delete file;
            file = nullptr;
            socket -> disconnectFromHost();
            return;
        }

        socket -> write(chunk); // write chunk onto the socket
        sentSize += chunk.size(); // update sent size after writing
    }

    if (sentSize == totalSize) {
        qDebug() << "Send session all " << sentSize << " bytes queued";
        file -> close();
        delete file;
        file = nullptr;
    }
}
