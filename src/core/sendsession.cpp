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

    qDebug() << "Sendr session: connecting to: " << host << port;
    socket -> connectToHost(host, port);
}

SendSession :: ~SendSession()
{
    qDebug() << "Sender session destroyed";
}

void SendSession :: onConnected()
{
    const QFileInfo info(filePath);

    if (!info.exists() || !info.isFile()) {
        qDebug() << "Sender Session file not found: " << filePath;
        socket -> disconnectFromHost();
    }

    file = new QFile(filePath, this);
    if (!file -> open(QIODevice :: ReadOnly)) {
        qDebug() << "Sender session cannot open this file: " << file -> fileName() << " error: " << file -> errorString();
        delete file;
        file = nullptr;
        socket -> disconnectFromHost();
        return;
    }

    totalSize = file -> size();


    QJsonObject header;
    header["v"] = currentVersionNumber;
    header["type"] = "file";
    header["filename"] = info.fileName();
    header["size"] = info.size();

    const QByteArray json = QJsonDocument(header).toJson(QJsonDocument :: Compact);

    QByteArray frame(4, '\0');
    qToBigEndian<quint32>(static_cast<quint32>(json.size()), frame.data());
    frame.append(json);

    qDebug() << "Sender session sending header: " << info.fileName() << " size " <<info.size();
    socket -> write(frame);

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

    while (sentSize < totalSize && socket -> bytesToWrite() < maxQueuedBytes) {
        const QByteArray chunk = file -> read (qMin (chunkSize, totalSize - sentSize));

        if(chunk.isEmpty()) {
            qDebug() << "Sender session file ended early or could not be read";
            file -> close();
            delete file;
            file = nullptr;
            socket -> disconnectFromHost();
            return;
        }

        socket -> write(chunk);
        sentSize += chunk.size();
    }

    if (sentSize == totalSize) {
        qDebug() << "Send session all " << sentSize << " bytes queued";
        file -> close();
        delete file;
        file = nullptr;
    }
}
