#include "filetransfer.h"
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QDataStream>

FileTransfer::FileTransfer(QObject *parent)
    : QObject(parent), socket(new QTcpSocket(this)),
    fileSize(0), totalWritten(0), transferActive(false), transferCompleted(false), peerPort(45679)
{
    connect(socket, &QTcpSocket::connected,
            this, &FileTransfer::onConnected);
    connect(socket, &QTcpSocket::bytesWritten,
            this, &FileTransfer::onBytesWritten);
    connect(socket, &QAbstractSocket::errorOccurred,
            this, &FileTransfer::onError);
    connect(socket, &QTcpSocket::disconnected,
            this, &FileTransfer::processNext);
}

void FileTransfer::sendFile(const QString &path, const QString &peerIp, int port, const QString &token) {
    fileQueue.enqueue(path);
    this->peerIp = peerIp;
    this->peerPort = port;
    this->token = token;

    if (!transferActive) {
        processNext();
    }
}

void FileTransfer::processNext() {
    if (fileQueue.isEmpty()) {
        transferActive = false;
        return;
    }

    filePath = fileQueue.dequeue();
    fileName = QFileInfo(filePath).fileName();
    totalWritten = 0;
    transferActive = true;
    transferCompleted = false;

    if (socket->state() != QAbstractSocket::UnconnectedState) {
        socket->abort();
    }

    QFile f(filePath);
    if (!f.exists()) {
        emit transferFailed("File not found: " + fileName);
        processNext(); // continue with next
        return;
    }
    fileSize = f.size();
    socket->connectToHost(QHostAddress(peerIp), peerPort);
}

void FileTransfer::onConnected() {
    qDebug() << "Connected to peer, sending:" << fileName;
    
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        qDebug() << "Cannot open file!";
        transferActive = false;
        emit transferFailed("Cannot open file: " + fileName);
        return;
    }

    qDebug() << "File size:" << fileSize;
    emit transferStarted(fileName);

    QByteArray nameBytes = fileName.toUtf8();
    qint32 nameLen = (qint32)nameBytes.size();
    qint64 fSize   = (qint64)fileSize;

    QByteArray header;
    
    QByteArray tokenBytes = token.toUtf8();
    qint32 tokenLen = (qint32)tokenBytes.size();
    header.resize(4);
    header[0] = (tokenLen >> 24) & 0xFF;
    header[1] = (tokenLen >> 16) & 0xFF;
    header[2] = (tokenLen >>  8) & 0xFF;
    header[3] = (tokenLen      ) & 0xFF;
    header.append(tokenBytes);

    QByteArray nameHeader(4, 0);
    nameHeader[0] = (nameLen >> 24) & 0xFF;
    nameHeader[1] = (nameLen >> 16) & 0xFF;
    nameHeader[2] = (nameLen >>  8) & 0xFF;
    nameHeader[3] = (nameLen      ) & 0xFF;
    header.append(nameHeader);
    header.append(nameBytes);

    QByteArray sizeBytes(8, 0);
    sizeBytes[0] = (fSize >> 56) & 0xFF;
    sizeBytes[1] = (fSize >> 48) & 0xFF;
    sizeBytes[2] = (fSize >> 40) & 0xFF;
    sizeBytes[3] = (fSize >> 32) & 0xFF;
    sizeBytes[4] = (fSize >> 24) & 0xFF;
    sizeBytes[5] = (fSize >> 16) & 0xFF;
    sizeBytes[6] = (fSize >>  8) & 0xFF;
    sizeBytes[7] = (fSize      ) & 0xFF;
    header.append(sizeBytes);

    qDebug() << "Header size:" << header.size();
    qDebug() << "Name length:" << nameLen;

    socket->write(header);

    QByteArray buffer;
    qint64 totalSent = 0;
    while (!f.atEnd()) {
        buffer = f.read(65536);
        socket->write(buffer);
        totalSent += buffer.size();
        qDebug() << "Sent chunk, total so far:" << totalSent;
    }
    f.close();
    socket->flush();

    qDebug() << "Transfer complete, total sent:" << totalSent;
    transferCompleted = true;
    // Don't set transferActive to false here, wait for disconnected signal to call processNext
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
    if (!transferActive) {
        return;
    }

    if (transferCompleted &&
        (error == QAbstractSocket::RemoteHostClosedError ||
         error == QAbstractSocket::OperationError)) {
        return;
    }

    transferActive = false;
    emit transferFailed("Connection error: " + socket->errorString());
}