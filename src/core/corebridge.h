#ifndef COREBRIDGE_H
#define COREBRIDGE_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QList>
#include <QFile>
#include <QJsonObject>
#include <QByteArray>
#include <QtEndian>

class CoreBridge : public QObject {

    Q_OBJECT

public:
    explicit CoreBridge (QObject *parent = nullptr);
    Q_INVOKABLE bool startListening();
    Q_INVOKABLE void stopListening();

private:

    // Objects and variables

    QTcpServer *server; // The engine
    QList <QTcpSocket *> clients; // List of clients saved in memory
    QFile *incomingFile = nullptr; // Points to the file or image sent as an object
    qint64 expectedSize = 0; // Size of the image or file before sending
    qint64 receivedSize = 0; // Size of the image after receiving
    QByteArray headerBuffer; // Stores the bytes of Header length prefix and the JSON header
    quint32 expectedHeaderSize = 0; // Stores the size of the JSON header
    bool receivingFile = false;
    // Bookmark upon the received bytes instead of using lots of other variables as statuses
    enum class State { AwaitPrefix, AwaitHeader, ReceiveBody};
    State state = State :: AwaitPrefix;

    // Methods

    void handleNewConnection();
    void readClientData();
    bool handlePrefix(QTcpSocket *client);
    bool handleHeader(QTcpSocket *client);
    bool handleBody();
    bool processBuffer(QTcpSocket *client);
    void finishFile();
};

#endif // COREBRIDGE_H
