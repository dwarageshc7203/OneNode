#include "filetransfer.h"
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QDataStream>

FileTransfer::FileTransfer(QObject *parent)
    : QObject(parent), socket(new QTcpSocket(this)),
      fileSize(0), totalWritten(0)
{
    connect(socket, &QTcpSocket::connected,
            this, &FileTransfer::onConnected);
    connect(socket, &QTcpSocket::bytesWritten,
            this, &FileTransfer::onBytesWritten);
    connect(socket, &QAbstractSocket::errorOccurred,
            this, &FileTransfer::onError);
}

void FileTransfer::sendFile(const QString &path, const QString &peerIp, int port) {
    filePath     = path;
    fileName     = QFileInfo(path).fileName();
    totalWritten = 0;

    QFile f(filePath);
    if (!f.exists()) {
        emit transferFailed("File not found: " + fileName);
        return;
    }
    fileSize = f.size();

    socket->connectToHost(QHostAddress(peerIp), port);
}

void FileTransfer::onConnected() {
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        emit transferFailed("Cannot open file: " + fileName);
        return;
    }

    emit transferStarted(fileName);

    // Protocol:
    // [4 bytes] filename length
    // [N bytes] filename
    // [8 bytes] file size
    // [M bytes] file data

    QByteArray nameBytes = fileName.toUtf8();
    int nameLen = nameBytes.size();

    QByteArray header;
    QDataStream ds(&header, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::BigEndian);
    ds << (qint32)nameLen;
    header.append(nameBytes);
    ds << (qint64)fileSize;

    socket->write(header);

    // Stream the file in chunks
    QByteArray buffer;
    while (!f.atEnd()) {
        buffer = f.read(65536); // 64 KB chunks
        socket->write(buffer);
    }

    f.close();
    socket->flush();

    emit transferDone(fileName);
    socket->disconnectFromHost();
}

void FileTransfer::onBytesWritten(qint64 bytes) {
    totalWritten += bytes;
    if (fileSize > 0) {
        int percent = (int)((totalWritten * 100) / fileSize);
        emit transferProgress(percent);
    }
}

void FileTransfer::onError(QAbstractSocket::SocketError error) {
    Q_UNUSED(error)
    emit transferFailed("Connection error: " + socket->errorString());
}