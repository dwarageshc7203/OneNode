#include "sendsession.h"
#include "protocol.h"

#include <QDebug>
#include <QFileInfo>
#include <QJsonObject>

// SendSession constructor
SendSession :: SendSession (const QString &host, quint16 port, const QString &filePath, QObject *parent) : QObject(parent), socket(new QTcpSocket(this)), filePath(filePath), fileName(QFileInfo(filePath).fileName())
{
    connect(socket, &QTcpSocket :: connected, this, &SendSession :: onConnected);
    connect(socket, &QTcpSocket :: disconnected, this, &SendSession :: onDisconnected);
    connect(socket, &QTcpSocket :: errorOccurred, this, &SendSession :: onSocketError);
    connect(socket, &QTcpSocket :: bytesWritten, this, &SendSession :: sendMore);
    connect(socket, &QTcpSocket :: readyRead, this, &SendSession :: onReadyRead);

    qDebug() << "Sender session: connecting to: " << host << port;
    socket -> connectToHost(host, port);
}

// destructor
SendSession :: ~SendSession()
{
    qDebug() << "Sender session destroyed";
}

void SendSession :: onConnected()
{
    const QFileInfo info(filePath); // gets the path's info

    if (!info.exists() || !info.isFile()) { // checks if the path exist and points to a file rather than a directory
        fail("Sender Session file not found: " + filePath); // passing on the fail func with the reason to create a signal failed()
        socket -> disconnectFromHost();
        return;
    }

    file = new QFile(filePath, this);
    if (!file -> open(QIODevice :: ReadOnly)) { // checkes if the file has Read access
        const QString reason = file -> errorString(); // save the error before init the file to nullptr
        const QString name = file -> fileName();
        delete file;
        file = nullptr; // when the condition fails, we are reinitializing file to point towards nullptr and disconnect
        fail("Sender session cannot open this file: " + name + " error: " + reason); // passing on the fail func with the reason to create a signal failed()
        socket -> disconnectFromHost();
        return;
    }

    totalSize = file -> size(); // update totalSize with the file's size in bytes

    // creating the JSON header as part of the protocol, with all it's fields
    QJsonObject header;
    header["v"] = Protocol :: version;
    header["type"] = "file";
    header["filename"] = info.fileName();
    header["size"] = totalSize;

    socket -> write(Protocol :: encodeFrame(header)); // writes the frame onto the connected session socket using Protocol method

    sendMore(); // after writing, make signal to send more bytes to fill the queue
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
    }
}

// function to handle sending of chunks over the limited size queue
void SendSession :: sendMore()
{
    if (reported) { // checking if the oepration is already completed
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
void SendSession :: onReadyRead()
{

    if (reported) { // exactly one reply per session
        return;
    }

    receivedBuffer.append(socket -> readAll());

    if (receivedBuffer.size() < 4) { // length prefix not yet arrived
        return;
    }

    QJsonObject reply;

    const Protocol :: decodeStatus status = Protocol :: tryDecodeFrame(receivedBuffer, reply);

    if (status == Protocol :: decodeStatus :: NeedMoreData) {
        return;
    }

    if (status == Protocol :: decodeStatus :: Invalid) {
        fail("Invalid reply from receiver");
        socket -> disconnectFromHost();
        return;
    }

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
