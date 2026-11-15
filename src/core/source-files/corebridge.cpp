#include "corebridge.h"
#include "receivesession.h"
#include "sendsession.h"
#include "netutil.h"
#include "identity.h"
#include "tlsconfig.h"
#include "pairingmode.h"
#include "pairingengine.h"
#include "truststore.h"
#include "mdnsservice.h"

#include <QDebug>
#include <QSslServer>
#include <QString>
#include <QTcpSocket>
#include <QDir>
#include <QStandardPaths>
#include <QFileInfo>
#include <QNetworkInterface>
#include <QRegularExpression>
#include <QSettings>
#include <QSysInfo>
#include <QSharedPointer>

// Constructor - member initializer list
CoreBridge :: CoreBridge (QObject *parent) : CoreBridge (Protocol :: Timeouts(), parent)
{
}

CoreBridge :: CoreBridge (const Protocol :: Timeouts &timeouts, QObject *parent) : QObject (parent), timeouts (timeouts), server (new QSslServer (this)), pairing (new PairingMode (timeouts, this)), name (QSysInfo :: machineHostName()), receiveDir (QStandardPaths :: writableLocation(QStandardPaths :: DownloadLocation) + "/OneNode")
{
    // pendingConnectionAvailable (not newConnection): it comes only AFTER the TLS handshake succeeded,
    // so handleNewConnection never sees a stranger who could not complete it
    connect(server, &QTcpServer :: pendingConnectionAvailable, this, &CoreBridge :: handleNewConnection);
    // incoming cards are self-signed: forgive only that (see tlsconfig.h)
    connect(server, &QSslServer :: sslErrors, this, &TlsConfig :: acceptSelfSigned);
    server -> setHandshakeTimeout(10000); // PROTOCOL.md section 7: 10 s for the handshake

    // pairing mode talks to the screen through the bridge
    connect(pairing, &PairingMode :: started, this, [this] (int pin) { emit pairingStarted(PairingEngine :: formatPin(pin)); });
    connect(pairing, &PairingMode :: ended, this, &CoreBridge :: pairingEnded);
    connect(pairing, &PairingMode :: paired, this, [this] (const QString &fingerprint, const QString &deviceName) {
        emit devicePaired(deviceName, fingerprint);
        emit trustedDevicesChanged();
    });

    // the nearby list shows which devices are paired, so it must follow the paired list
    connect(this, &CoreBridge :: trustedDevicesChanged, this, &CoreBridge :: nearbyDevicesChanged);
    // and the others learn when we are in "Pair a new device" mode
    connect(pairing, &PairingMode :: started, this, [this] (int) { if (discovery) { discovery -> setPairing(true); } });
    connect(pairing, &PairingMode :: ended, this, [this] (const QString &) { if (discovery) { discovery -> setPairing(false); } });
}

CoreBridge :: ~CoreBridge() = default; // (defined here because TrustStore is only declared in the header)

QString CoreBridge :: defaultDataFolder()
{
    const QString fromEnvironment = qEnvironmentVariable("ONENODE_DATA_DIR");
    if (!fromEnvironment.isEmpty()) {
        return fromEnvironment;
    }
    return QStandardPaths :: writableLocation(QStandardPaths :: GenericDataLocation) + "/OneNode"; // Linux: ~/.local/share/OneNode
}

QString CoreBridge :: loadIdentity(const QString &folder)
{
    QSslCertificate cert;
    QSslKey key;
    QString error;
    if (!Identity :: loadOrCreate(folder, cert, key, error)) {
        return error;
    }

    auto list = std :: make_unique<TrustStore>(folder);
    const QString listError = list -> load(); // a damaged list is an error, never silently replaced
    if (!listError.isEmpty()) {
        return listError;
    }
    trust = std :: move(list);

    tls = TlsConfig :: make(cert, key);
    server -> setSslConfiguration(tls);
    fingerprint = Identity :: fingerprint(cert);
    haveIdentity = true;
    return QString();
}

QString CoreBridge :: myFingerprint() const
{
    return fingerprint;
}

QString CoreBridge :: deviceName() const
{
    return name;
}

void CoreBridge :: setDeviceName (const QString &newName)
{
    name = TrustStore :: cleanName(newName);
    if (discovery) {
        discovery -> setDeviceName(name);
    }
}

QString CoreBridge :: changeDeviceName (const QString &newName)
{
    if (newName.trimmed().isEmpty()) {
        return "The name cannot be empty.";
    }
    setDeviceName(newName);
    QSettings settings("OneNode", "OneNode"); // main.cpp reads the same place at startup
    settings.setValue("deviceName", name);
    return QString();
}

void CoreBridge :: startPairing()
{
    if (haveIdentity) {
        pairing -> start();
    }
}

void CoreBridge :: cancelPairing()
{
    pairing -> cancel();
}

bool CoreBridge :: pairingActive() const
{
    return pairing -> active();
}

QString CoreBridge :: submitPin (quint64 id, const QString &pin)
{
    const QPointer<SendSession> session = waitingForPin.value(id);
    if (!session) {
        return "There is nothing to pair with right now.";
    }
    return session -> submitPin(pin);
}

void CoreBridge :: cancelPin (quint64 id)
{
    if (const QPointer<SendSession> session = waitingForPin.value(id)) {
        session -> cancelPairing();
    }
}

QVariantList CoreBridge :: trustedDevices() const
{
    QVariantList list;
    if (!trust) {
        return list;
    }
    for (const TrustStore :: Device &device : trust -> devices()) {
        QVariantMap entry;
        entry["name"] = device.name;
        entry["fingerprint"] = device.fingerprint;
        entry["shortFingerprint"] = device.fingerprint.left(8) + "…" + device.fingerprint.right(4); // enough to tell devices apart on screen
        entry["pairedAt"] = device.pairedAt;
        list.append(entry);
    }
    return list;
}

QString CoreBridge :: removeTrustedDevice (const QString &fingerprint)
{
    if (!trust) {
        return "No list of paired devices is loaded.";
    }
    const QString problem = trust -> remove(fingerprint);
    if (problem.isEmpty()) {
        emit trustedDevicesChanged();
    }
    return problem;
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
    return sendToAddress(address, files, QString()); // typed address: no claim about who is there
}

QString CoreBridge :: sendToAddress(const QString &address, const QList<QUrl> &files, const QString &expectedFingerprint)
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

    // The FIRST file goes alone. Only when the other device has accepted us (hello done, or pairing done)
    // do the others follow. Otherwise a first-time pairing would ask for a PIN once per file.
    SendSession *first = startSend(host, paths.first(), port, expectedFingerprint);
    const QStringList rest = paths.mid(1);

    if (rest.isEmpty()) {
        return QString();
    }
    if (!first) {
        for (const QString &path : rest) {
            failRow(path, host, "Not sent: the first file could not be started.");
        }
        return QString();
    }

    auto decided = QSharedPointer<bool>::create(false); // true once the rest were started or failed
    connect(first, &SendSession :: ready, this, [this, rest, host, port, expectedFingerprint, decided] {
        if (!*decided) {
            *decided = true;
            for (const QString &path : rest) {
                startSend(host, path, port, expectedFingerprint);
            }
        }
    });
    connect(first, &SendSession :: failed, this, [this, rest, host, decided] (const QString &, const QString &reason) {
        if (!*decided) {
            *decided = true;
            for (const QString &path : rest) {
                failRow(path, host, "Not sent: " + reason);
            }
        }
    });
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
    if (!haveIdentity) { // never listen without an identity: there would be nothing to show in the TLS handshake
        return false;
    }

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
    stopDiscovery(); // say goodbye before the door closes
    server -> close();
}

// a row on the screen for a file that never started
void CoreBridge :: failRow (const QString &filePath, const QString &host, const QString &reason)
{
    const quint64 id = nextId ++;
    const QFileInfo info (filePath);
    announce (id, info.fileName(), Direction :: Send, info.size(), host);
    emit transferFailed(id, info.fileName(), reason);
    announced.remove(id);
}

// used by the CLI and by tests: send one file now
void CoreBridge :: sendFile(const QString &host, const QString &filePath, quint16 port)
{
    startSend(host, filePath, port);
}

// instantiates the SendSession module with the arguments and signals
SendSession *CoreBridge :: startSend(const QString &host, const QString &filePath, quint16 port, const QString &expectedFingerprint)
{
    if (!haveIdentity) {
        failRow(filePath, host, "This device has no identity yet, so it cannot make a secure connection.");
        return nullptr;
    }

    const quint64 id = nextId ++;
    const QFileInfo info (filePath);
    announce (id, info.fileName(), Direction :: Send, info.size(), host);

    SendSession :: Services services;
    services.tls = tls;
    services.ownName = name;
    services.trust = trust.get();
    services.expectedFingerprint = expectedFingerprint;
    services.timeouts = timeouts;
    auto *sendSession = new SendSession(host, port, filePath, services, this); // calls out SenderSession

    // connecting signal of SendSession with that of CoreBridge
    connect(sendSession, &SendSession :: progress, this, [this, id] (const QString &fileName, qint64 done, qint64 total)
    {
        emit transferProgress(id, fileName, done, total);
    });

    connect(sendSession, &SendSession :: finished, this, [this, id] (const QString &fileName)
    {
        emit transferFinished(id, fileName);
        announced.remove(id); // process id finished, so remove it
        waitingForPin.remove(id);
    });

    connect(sendSession, &SendSession :: failed, this, [this, id] (const QString &fileName, const QString &reason)
    {
        emit transferFailed(id, fileName, reason);
        announced.remove(id); // process id finished, so remove it
        waitingForPin.remove(id);
    });

    // pairing: the user has to type the PIN shown on the other device
    connect(sendSession, &SendSession :: pinNeeded, this, [this, id, sendSession] (const QString &peerName)
    {
        waitingForPin.insert(id, sendSession);
        emit pinNeeded(id, peerName);
    });
    connect(sendSession, &SendSession :: ready, this, [this, id] { waitingForPin.remove(id); });
    connect(sendSession, &SendSession :: paired, this, [this] (const QString &fingerprint, const QString &deviceName)
    {
        emit devicePaired(deviceName, fingerprint);
        emit trustedDevicesChanged();
    });
    return sendSession;
}

// ---------- nearby devices ----------

void CoreBridge :: setDiscoveryTransport (MdnsTransport *transport)
{
    if (discovery) {
        return; // too late: already running
    }
    delete discoveryTransport;
    discoveryTransport = transport;
    if (transport) {
        transport -> setParent(this);
    }
}

QString CoreBridge :: startDiscovery()
{
    if (discovery) {
        return QString();
    }
    if (!haveIdentity || !server -> isListening()) {
        return "This device is not ready to be found yet.";
    }
    if (!discoveryTransport) {
        discoveryTransport = new UdpMulticastTransport(this);
    }
    auto service = std :: make_unique<MdnsService>(discoveryTransport);
    QString error;
    if (!service -> start(name, fingerprint, server -> serverPort(), pairing -> active(), error)) {
        return error;
    }
    connect(service.get(), &MdnsService :: peersChanged, this, &CoreBridge :: nearbyDevicesChanged);
    discovery = std :: move(service);
    emit nearbyDevicesChanged();
    return QString();
}

void CoreBridge :: stopDiscovery()
{
    if (!discovery) {
        return;
    }
    discovery -> stop(); // sends the goodbye
    discovery.reset();
    emit nearbyDevicesChanged();
}

QVariantList CoreBridge :: nearbyDevices() const
{
    QVariantList list;
    if (!discovery) {
        return list;
    }
    for (const MdnsPeer &peer : discovery -> peers()) {
        QVariantMap entry;
        entry["name"] = peer.name;
        entry["fingerprint"] = peer.fingerprint;
        entry["shortFingerprint"] = peer.fingerprint.left(8) + "…" + peer.fingerprint.right(4);
        entry["address"] = peer.address().toString();
        entry["port"] = peer.port;
        entry["trusted"] = trust && trust -> isTrusted(peer.fingerprint);
        entry["pairing"] = peer.pairing;
        list.append(entry);
    }
    return list;
}

QString CoreBridge :: sendToNearby (const QString &fingerprint, const QList<QUrl> &files)
{
    if (discovery) {
        for (const MdnsPeer &peer : discovery -> peers()) {
            if (peer.fingerprint == fingerprint) {
                return sendToAddress(peer.address().toString() + ":" + QString :: number(peer.port), files, fingerprint);
            }
        }
    }
    return "That device is not nearby any more.";
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
    ReceiveSession :: Services services;
    services.destinationDir = receiveDir;
    services.ownFingerprint = fingerprint;
    services.ownName = name;
    services.peerFingerprint = Identity :: fingerprint(static_cast<QSslSocket *>(client) -> peerCertificate()); // from the handshake
    services.trust = trust.get();
    services.pairing = pairing;
    services.timeouts = timeouts;
    auto *receiveSession = new ReceiveSession(client, services, this); // start the ReceiveSession instance with the arguments

    // refused / not-yet-trusted devices are only logged, never shown (a stranger must not make anything appear on screen)
    connect(receiveSession, &ReceiveSession :: refused, this, [peer] (const QString &code, const QString &reason)
    {
        qDebug() << "Refused" << peer << code << reason;
    });

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
