#ifndef RECEIVESESSION_H
#define RECEIVESESSION_H

#include <QObject>
#include <QTcpSocket>
#include <QFile>
#include <QtEndian>
#include <QString>

class ReceiveSession : public QObject
{
    Q_OBJECT

public:
    explicit ReceiveSession (QTcpSocket *clientSocket, QObject *parent = nullptr);
    ~ReceiveSession() override; // destructor and with overriding power over CoreBridge

private:

    QTcpSocket *socket; // this is the connection
    QFile *incomingFile = nullptr; // Points to the file or image sent as an object
    qint64 expectedSize = 0; // Size of the image or file before sending
    qint64 receivedSize = 0; // Size of the image after receiving
    QByteArray receivedBuffer; // Stores the bytes of Header length prefix, JSON header and the body
    quint32 expectedHeaderSize = 0; // Stores the size of the JSON header
    enum class State { AwaitPrefix, AwaitHeader, ReceiveBody}; // enum with 3 states
    State state = State :: AwaitPrefix;
    static constexpr quint32 maxHeaderSize = 16384; // to validate incoming header to the size of 16 KiB
    QString finalPath; // Actual and valid name the file gets after completing all the checks
    static constexpr int currentVersionNumber = 1;

    //methods

    void onDisconnected();
    bool handlePrefix();
    bool handleHeader();
    void finishFile();
    bool handleBody();
    bool processBuffer();
    void readData();
    void sendResult(const QString &status, const QString &code = QString());
    void reject(const QString &code, const QString &reason);

};

#endif // RECEIVESESSION_H
