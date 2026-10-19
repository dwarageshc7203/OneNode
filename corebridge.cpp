#include "corebridge.h"
#include <QTcpSocket>
#include <QDebug>

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

void CoreBridge :: handleNewConnection()
{
    QTcpSocket *client = server -> nextPendingConnection();
    qDebug() << "A client has connected: " << client -> peerAddress().toString();
    connect(client, &QTcpSocket :: readyRead, this, &CoreBridge :: readClientData);
}

void CoreBridge :: readClientData()
{
    QTcpSocket *client = qobject_cast<QTcpSocket *>(sender ());
    if (client) {
        QByteArray data = client->readAll();
        qDebug() << "Client received: " << data;
    }
}
