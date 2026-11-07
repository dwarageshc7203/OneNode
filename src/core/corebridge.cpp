#include "corebridge.h"
#include "receivesession.h"
#include "sendsession.h"
#include "protocol.h"
#include "netutil.h"

#include <QDebug>
#include <QTcpServer>
#include <QString>
#include <QTcpSocket>

// Constructor - member initializer list
// I am instantiating the QObject and the QTcpServer objects here

CoreBridge :: CoreBridge (QObject *parent) : QObject (parent), server (new QTcpServer (this))
{
    // connect server's signal to handler method
    connect(server, &QTcpServer :: newConnection, this, &CoreBridge :: handleNewConnection);
}

bool CoreBridge :: startListening()
{
    return server -> listen (QHostAddress :: Any, Protocol :: defaultPort);
}

void CoreBridge :: stopListening()
{
    server -> close();
}

void CoreBridge :: sendFile(const QString &host, const QString &filePath)
{
    auto *sendSession = new SendSession(host, Protocol :: defaultPort, filePath, this); // calls out Sender Session

    // connecting signal of SendSession with that of CoreBridge

    connect(sendSession, &SendSession :: progress, this, &CoreBridge :: transferProgress);
    connect(sendSession, &SendSession :: finished, this, &CoreBridge :: transferFinished);
    connect(sendSession, &SendSession :: failed, this, &CoreBridge :: transferFailed);
}

void CoreBridge :: handleNewConnection()
{
    QTcpSocket *client = server -> nextPendingConnection();

    // Checking null pointer when no connection available
    if (!client) {
        return;
    }

    // Connect handle
    qDebug() << "A client has connected: " << prettyNetworkAddress(client -> peerAddress());

    auto *receiveSession = new ReceiveSession(client, this); // We are now bringing in a new session with the new connection :)

    // connecting signal of ReceiveSession with that of CoreBridge

    connect(receiveSession, &ReceiveSession :: progress, this, &CoreBridge :: transferProgress);
    connect(receiveSession, &ReceiveSession :: finished, this, &CoreBridge :: transferFinished);
    connect(receiveSession, &ReceiveSession :: failed, this, &CoreBridge :: transferFailed);

    // Disconnect handle
    connect (client, &QTcpSocket :: disconnected, this, [client] {
        qDebug() << "The client has disconnected: " << prettyNetworkAddress(client -> peerAddress());

        client -> deleteLater();
    });
}
