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

private:

    QTcpSocket *socket; // sender connection
    QString filePath; // the file we are sending
    static constexpr int currentVersionNumber = 1;
    QFile *file = nullptr;
    qint64 totalSize = 0;
    qint64 sentSize = 0;
    static constexpr qint64 chunkSize = 64 * 1024;
    static constexpr qint64 maxQueuedBytes = 256 * 1024;

    //methods

    void onConnected(); // new connection
    void onDisconnected(); // disconnection signal
    void onSocketError(QAbstractSocket :: SocketError error); // catches sender errors
    void sendMore(); // signal to send bytes when some bytes left the queue
};

#endif // SENDSESSION_H
