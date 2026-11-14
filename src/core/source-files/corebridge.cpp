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
#include <QNetworkInterface>
#include <QRegularExpression>
#include <QSettings>

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

QUrl CoreBridge :: receiveFolderUrl() const
{
    return QUrl :: fromLocalFile(receiveDir);
}

QString CoreBridge :: changeReceiveFolder(const QUrl &folder)
{
    const QString path = folder.toLocalFile();
    const QFileInfo info(path);
    if (path.isEmpty() || !info.isDir()) {
        return "That is not a folder on this computer";
    }
    if (!info.isWritable()) {
        return "OneNode cannot save files into that folder. Choose another one.";
    }

    setReceiveDir(path);
    QSettings settings("OneNode", "OneNode"); // main.cpp reads the same place at startup
    settings.setValue("receiveDir", path);
    return QString();
}

quint64 CoreBridge :: listeningPort() const
{
    return server -> serverPort(); // returns 0 when server is not listening
}

void CoreBridge :: announce (quint64 id, const QString &fileName, Direction direction, qint64 total, const QString &peer)
{
    if (announced.contains(id)) { // checking if process is already started or no
        return;
    }

    announced.insert(id); // storing id when starting the transfer process
    emit transferStarted(id, fileName, direction, total, peer);
}

QString CoreBridge :: parseAddress(const QString &text, QString &host, quint16 &port)
{
    const QString address = text.trimmed();
    if (address.isEmpty()) {
        return "Type the address of the other device";
    }

    const int colons = address.count(':');
    if (colons > 1) {
        return "IPv6 addresses are not supported yet";
    }

    QString hostPart = address;
    quint16 portPart = Protocol :: defaultPort; // no port typed: use the standard one
    if (colons == 1) {
        hostPart = address.section(':', 0, 0);
        bool numberOk = false;
        const uint number = address.section(':', 1).toUInt(&numberOk);
        if (!numberOk || number < 1 || number > 65535) {
            return "The port must be a number between 1 and 65535";
        }
        portPart = static_cast <quint16> (number);
    }

    // letters, digits, dots and dashes only: enough for "192.168.1.23" and for names like "my-pc.local"
    static const QRegularExpression valid("^[A-Za-z0-9]([A-Za-z0-9.-]*[A-Za-z0-9])?$");
    if (!valid.match(hostPart).hasMatch()) {
        return "That does not look like an address. Example: 192.168.1.23:45454";
    }

    host = hostPart;
    port = portPart;
    return QString();
}

QString CoreBridge :: sendTo(const QString &address, const QList<QUrl> &files)
{
    QString host;
    quint16 port = 0;
    const QString problem = parseAddress(address, host, port);
    if (!problem.isEmpty()) {
        return problem;
    }

    if (files.isEmpty()) {
        return "Choose at least one file";
    }

    // check everything first, so that either all the files are started or none of them
    QStringList paths;
    for (const QUrl &url : files) {
        if (!url.isLocalFile()) {
            return "Only files on this computer can be sent";
        }
        paths.append(url.toLocalFile()); // "file:///home/me/a.txt" -> "/home/me/a.txt" (also right on Windows)
    }

    for (const QString &path : paths) {
        sendFile(host, path, port);
    }
    return QString();
}

// The address another device on the same network should type in to reach us.
// Prefers a private LAN address (192.168.x.x, 10.x.x.x, 172.16-31.x.x) over anything else.
QString CoreBridge :: localAddress() const
{
    QString fallback;
    for (const QHostAddress &address : QNetworkInterface :: allAddresses()) {
        if (address.protocol() != QAbstractSocket :: IPv4Protocol || address.isLoopback()) {
            continue; // only real IPv4 addresses
        }
        if (address.isInSubnet(QHostAddress("192.168.0.0"), 16) || address.isInSubnet(QHostAddress("10.0.0.0"), 8)
            || address.isInSubnet(QHostAddress("172.16.0.0"), 12)) {
            return address.toString();
        }
        if (fallback.isEmpty()) {
            fallback = address.toString();
        }
    }
    return fallback; // empty when there is no network at all
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
    announce (id, info.fileName(), Direction :: Send, info.size(), host);

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
    const QString peer = prettyNetworkAddress(client -> peerAddress()); // who is sending to us
    qDebug() << "A client has connected: " << peer;

    const quint64 id = nextId ++; // creating the new unique id
    auto *receiveSession = new ReceiveSession(client, receiveDir, this); // start the ReceiveSession instance with the arguments

    // connecting signal of ReceiveSession with that of CoreBridge
    connect(receiveSession, &ReceiveSession :: progress, this, [this, id, peer] (const QString &fileName, qint64 done, qint64 total)
    {
        announce (id, fileName, Direction :: Receive, total, peer);
        emit transferProgress(id, fileName, done, total);
    });

    connect(receiveSession, &ReceiveSession :: finished, this, [this, id, peer] (const QString &fileName)
    {
        announce (id, fileName, Direction :: Receive, 0, peer);
        emit transferFinished(id, fileName);
        announced.remove(id); // process id finished, so remove it
    });

    connect(receiveSession, &ReceiveSession :: failed, this, [this, id, peer] (const QString &fileName, const QString &reason)
    {
        announce (id, fileName, Direction :: Receive, 0, peer);
        emit transferFailed(id, fileName, reason);
        announced.remove(id); // process id finished, so remove it
    });

    // Disconnect handle
    connect (client, &QTcpSocket :: disconnected, this, [client] {
        qDebug() << "The client has disconnected: " << prettyNetworkAddress(client -> peerAddress());

        client -> deleteLater();
    });
}
