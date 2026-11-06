#ifndef SENDSESSION_H
#define SENDSESSION_H

#include <QObject>
#include <QTcpSocket>
#include <QString>
#include <QFile>

class SendSession : public QObject
{
    Q_OBJECT

public:
    explicit SendSession (const QString &host, quint16 port, const QString &filePath, QObject *parent = nullptr);
    ~SendSession() override; // destructor and with overriding power over CoreBridge

signals:
    void progress(const QString &fileName, const qint64 done, const qint64 total);
    void finished(const QString &fileName);
    void failed(const QString &fileName, const QString &reason);

private:

    QTcpSocket *socket; // sender connection
    QString filePath; // the file we are sending
    QString fileName;
    QFile *file = nullptr; // will point at the open file to be shared
    qint64 totalSize = 0; // size of the file to be shared in bytes
    qint64 sentSize = 0; // bytes sent from the sender session to the receiver
    static constexpr qint64 chunkSize = 64 * 1024; // 64 KiB per chunk - slice of data accessed in a particular iteration
    static constexpr qint64 maxQueuedBytes = 256 * 1024; // sets the upper limit on how much unwritten memory is allowed to sit in memory waiting for receiver
    QByteArray receivedBuffer; // to process the sending bytes
    bool reported = false; // status to check if the sending process is complete (can be either positive or negative)

    //methods

    void onConnected(); // new connection
    void onDisconnected(); // disconnection signal
    void onSocketError(QAbstractSocket :: SocketError error); // catches sender errors
    void sendMore(); // signal to send bytes when some bytes left the queue
    void onReadyRead(); // activates whenever there is any bytes on the buffer
    void fail(const QString &reason);

};

#endif // SENDSESSION_H
