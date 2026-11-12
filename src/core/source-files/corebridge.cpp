#include "corebridge.h"
#include "receivesession.h"
#include "sendsession.h"
#include "netutil.h"

#include <QDebug>
#include <QTcpServer>
#include <QString>
#include <QTcpSocket>
#include <QDir>
#include <QStandardPaths>
#include <QFileInfo>

// Constructor - member initializer list
CoreBridge :: CoreBridge (QObject *parent) : QObject (parent), server (new QTcpServer (this)), receiveDir (QStandardPaths :: writableLocation(QStandardPaths :: DownloadLocation) + "/OneNode")
{
    // connect server's signal to handler method
    connect(server, &QTcpServer :: newConnection, this, &CoreBridge :: handleNewConnection);    
}

void CoreBridge :: setReceiveDir(const QString &dir)
{
    receiveDir = dir;
}

QString CoreBridge :: receiveFolder() const
{
    return receiveDir;
}

quint64 CoreBridge :: listeningPort() const
{
    return server -> serverPort(); // returns 0 when server is not listening
}

void CoreBridge :: announce (quint64 id, const QString &fileName, Direction direction, qint64 total)
{
    if (announced.contains(id)) { // checking if process is already started or no
        return;
    }

    announced.insert(id); // storing id when starting the transfer process
    emit transferStarted(id, fileName, direction, total);
}

// app startup function
bool CoreBridge :: startListening(quint16 port)
{
    if (!server -> listen(QHostAddress :: Any, port)) { // if the port is busy or held by other tasks
        return false;
    }

    // removing partial and stale files on startup
    QDir dir(receiveDir);
    const QStringList leftOvers = dir.entryList(QStringList() << "*" + ReceiveSession :: partSuffix(), QDir :: Files); // find all the files with the suffix ".onenode-part"

    for (const QString &name : leftOvers) {
        qDebug() << "Removing stale and partial file: " << name;
        dir.remove(name); // deleting those files
    }

    return true;
}

// app quit function
void CoreBridge :: stopListening()
{
    server -> close();
}

// instantiates the SendSession module with the arguments and signals
void CoreBridge :: sendFile(const QString &host, const QString &filePath, quint16 port)
{
    const quint64 id = nextId ++;
    auto *sendSession = new SendSession(host, port, filePath, this); // calls out SenderSession

    // connecting signal of SendSession with that of CoreBridge

    const QFileInfo info (filePath);
    announce (id, info.fileName(), Direction :: Send, info.size());

    connect(sendSession, &SendSession :: progress, this, [this, id] (const QString &fileName, qint64 done, qint64 total)
    {
        emit transferProgress(id, fileName, done, total);
    });

    connect(sendSession, &SendSession :: finished, this, [this, id] (const QString &fileName)
    {
        emit transferFinished(id, fileName);
        announced.remove(id); // process id finished, so remove it
    });

    connect(sendSession, &SendSession :: failed, this, [this, id] (const QString &fileName, const QString &reason)
    {
        emit transferFailed(id, fileName, reason);
        announced.remove(id); // process id finished, so remove it
    });
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

    const quint64 id = nextId ++; // creating the new unique id
    auto *receiveSession = new ReceiveSession(client, receiveDir, this); // start the ReceiveSession instance with the arguments

    // connecting signal of ReceiveSession with that of CoreBridge
    connect(receiveSession, &ReceiveSession :: progress, this, [this, id] (const QString &fileName, qint64 done, qint64 total)
    {
        announce (id, fileName, Direction :: Receive, total);
        emit transferProgress(id, fileName, done, total);
    });

    connect(receiveSession, &ReceiveSession :: finished, this, [this, id] (const QString &fileName)
    {
        announce (id, fileName, Direction :: Receive, 0);
        emit transferFinished(id, fileName);
        announced.remove(id); // process id finished, so remove it
    });

    connect(receiveSession, &ReceiveSession :: failed, this, [this, id] (const QString &fileName, const QString &reason)
    {
        announce (id, fileName, Direction :: Receive, 0);
        emit transferFailed(id, fileName, reason);
        announced.remove(id); // process id finished, so remove it
    });

    // Disconnect handle
    connect (client, &QTcpSocket :: disconnected, this, [client] {
        qDebug() << "The client has disconnected: " << prettyNetworkAddress(client -> peerAddress());

        client -> deleteLater();
    });
}
