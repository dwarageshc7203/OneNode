#include "corebridge.h"
#include "receivesession.h"
#include "sendsession.h"

#include <QTcpSocket>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QFile>

// Constructor - member initializer list
// I am instantiating the QObject and the QTcpServer objects here

CoreBridge :: CoreBridge (QObject *parent) : QObject (parent), server (new QTcpServer (this))
{
    // connect server's signal to handler method
    connect(server, &QTcpServer :: newConnection, this, &CoreBridge :: handleNewConnection);
}

bool CoreBridge :: startListening()
{
    return server -> listen (QHostAddress :: Any, 45454);
}

void CoreBridge :: stopListening()
{
    server -> close();
}

void CoreBridge :: sendFile(const QString &host, const QString &filePath)
{
    new SendSession(host, 45454, filePath, this); // calls out Sender Session
}

void CoreBridge :: handleNewConnection()
{
    QTcpSocket *client = server -> nextPendingConnection();

    // Checking null pointer when no connection available
    if (!client) {
        return;
    }

    clients.append(client);

    // Connect handle
    qDebug() << "A client has connected: " << client -> peerAddress().toString();

    new ReceiveSession(client, this); // We are now bringing in a new session with the new connection :)

    // Disconnect handle
    connect (client, &QTcpSocket :: disconnected, this, [this, client] {
        qDebug() << "The client has disconnected: " << client -> peerAddress().toString();

        clients.removeOne(client);
        client -> deleteLater();
    });
}
