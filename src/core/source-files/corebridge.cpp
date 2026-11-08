#include "corebridge.h"
#include "receivesession.h"
#include "sendsession.h"
#include "protocol.h"
#include "netutil.h"

#include <QDebug>
#include <QTcpServer>
#include <QString>
#include <QTcpSocket>
#include <QDir>
#include <QStandardPaths>

// Constructor - member initializer list
CoreBridge :: CoreBridge (QObject *parent) : QObject (parent), server (new QTcpServer (this)), receiveDir (QStandardPaths :: writableLocation(QStandardPaths :: DownloadLocation) + "/OneNode")
{
    // connect server's signal to handler method
    connect(server, &QTcpServer :: newConnection, this, &CoreBridge :: handleNewConnection);

    // removing partial and stale files on startup
    QDir dir(receiveDir);
    const QStringList leftOvers = dir.entryList(QStringList() << "*" + ReceiveSession :: partSuffix(), QDir :: Files); // find all the files with the suffix ".onenode-part"

    for (const QString &name : leftOvers) {
        qDebug() << "Removing stale and partial file: " << name;
        dir.remove(name); // deleting those files
    }
}

// app startup function
bool CoreBridge :: startListening()
{
    return server -> listen (QHostAddress :: Any, Protocol :: defaultPort); // default port is 45454 on both the sender and receiver end
}

// app quit function
void CoreBridge :: stopListening()
{
    server -> close();
}

// instantiates the SendSession module with the arguments and signals
void CoreBridge :: sendFile(const QString &host, const QString &filePath)
{
    auto *sendSession = new SendSession(host, Protocol :: defaultPort, filePath, this); // calls out SenderSession

    // connecting signal of SendSession with that of CoreBridge

    connect(sendSession, &SendSession :: progress, this, &CoreBridge :: transferProgress);
    connect(sendSession, &SendSession :: finished, this, &CoreBridge :: transferFinished);
    connect(sendSession, &SendSession :: failed, this, &CoreBridge :: transferFailed);
}

void CoreBridge :: handleNewConnection()
{
    QTcpSocket *client = server -> nextPendingConnection();

    // checking for any receiver
    if (!client) {
        return;
    }

    // Connect handle
    qDebug() << "A client has connected: " << prettyNetworkAddress(client -> peerAddress());

    auto *receiveSession = new ReceiveSession(client, receiveDir, this); // start the ReceiveSession instance with the arguments

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
