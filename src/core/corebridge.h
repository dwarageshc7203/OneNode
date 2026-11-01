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

    // Methods

    void handleNewConnection(); // This method creates a new session using ReceiveSession
};

#endif // COREBRIDGE_H
