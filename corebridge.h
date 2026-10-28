#ifndef COREBRIDGE_H
#define COREBRIDGE_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QList>
#include <QFile>
#include <QJsonObject>
#include <QByteArray>

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
    qint32 expectedHeaderSize = 0; // Stores the size of the JSON header
    bool receivingFile = false;

    // Methods

    void handleNewConnection();
    void readClientData();
    void processFileData();
};

#endif // COREBRIDGE_H
