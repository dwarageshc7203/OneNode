#include "mdnsservice.h"

#include "truststore.h"

#include <QElapsedTimer>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QUdpSocket>
#include <QVariant>

#include <algorithm>

namespace {

const QHostAddress multicastGroup("224.0.0.251");
constexpr quint16 mdnsPort = 5353;

qint64 realClock()
{
    static QElapsedTimer timer;
    if (!timer.isValid()) {
        timer.start();
    }
    return timer.elapsed();
}

// "name=value" lookups that tolerate a missing key
QString txtValue (const QMap<QString, QString> &txt, const QString &key)
{
    return txt.value(key);
}

bool usableAddress (const QHostAddress &address)
{
    return address.protocol() == QAbstractSocket :: IPv4Protocol && !address.isNull() && !address.isLoopback()
        && !address.isMulticast() && address != QHostAddress :: Broadcast && address != QHostAddress :: AnyIPv4;
}

} // namespace

// ================= UdpMulticastTransport =================

UdpMulticastTransport :: UdpMulticastTransport (QObject *parent) : MdnsTransport(parent) {}

UdpMulticastTransport :: ~UdpMulticastTransport()
{
    stop();
}

QList<MdnsInterface> UdpMulticastTransport :: usableInterfaces()
{
    QList<MdnsInterface> result;
    for (const QNetworkInterface &iface : QNetworkInterface :: allInterfaces()) {
        const auto flags = iface.flags();
        if (!(flags & QNetworkInterface :: IsUp) || !(flags & QNetworkInterface :: IsRunning)
            || !(flags & QNetworkInterface :: CanMulticast) || (flags & QNetworkInterface :: IsLoopBack)) {
            continue;
        }
        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            if (usableAddress(entry.ip())) {
                MdnsInterface item;
                item.index = iface.index();
                item.address = entry.ip();
                item.prefixLength = entry.prefixLength() >= 0 ? entry.prefixLength() : 24;
                result.append(item);
                break; // one address per connection is enough
            }
        }
    }
    return result;
}

bool UdpMulticastTransport :: onLink (const QHostAddress &from, const MdnsInterface &iface)
{
    if (from.protocol() != QAbstractSocket :: IPv4Protocol) {
        return false;
    }
    return from.isInSubnet(iface.address, iface.prefixLength);
}

bool UdpMulticastTransport :: start (QString &error)
{
    ifaces = usableInterfaces();
    if (ifaces.isEmpty()) {
        error = "No network connection that can find other devices. Connect to a Wi-Fi or a cable network.";
        return false;
    }

    socket = new QUdpSocket(this);
    // ShareAddress: other programs on this computer (the system's own mDNS, Avahi, Bonjour) use port 5353 too
    if (!socket -> bind(QHostAddress :: AnyIPv4, mdnsPort, QUdpSocket :: ShareAddress | QUdpSocket :: ReuseAddressHint)) {
        error = "Could not listen for nearby devices: " + socket -> errorString();
        delete socket;
        socket = nullptr;
        return false;
    }
    socket -> setSocketOption(QAbstractSocket :: MulticastLoopbackOption, QVariant(1)); // other programs here must hear us too
    socket -> setSocketOption(QAbstractSocket :: MulticastTtlOption, QVariant(255));    // RFC 6762: 255, so receivers can check "one hop"

    joinAll();
    if (joined.isEmpty()) {
        error = "Could not join the nearby-devices group on any network connection.";
        delete socket;
        socket = nullptr;
        return false;
    }
    connect(socket, &QUdpSocket :: readyRead, this, &UdpMulticastTransport :: readPending);
    return true;
}

void UdpMulticastTransport :: joinAll()
{
    for (const MdnsInterface &item : std :: as_const(ifaces)) {
        if (joined.contains(item.index)) {
            continue;
        }
        if (socket -> joinMulticastGroup(multicastGroup, QNetworkInterface :: interfaceFromIndex(item.index))) {
            joined.append(item.index);
        }
    }
}

void UdpMulticastTransport :: refresh()
{
    if (!socket) {
        return;
    }
    ifaces = usableInterfaces();
    // forget connections that are gone, so that they can be joined again when they come back
    joined.erase(std :: remove_if(joined.begin(), joined.end(), [this] (int index) {
        return std :: none_of(ifaces.begin(), ifaces.end(), [index] (const MdnsInterface &i) { return i.index == index; });
    }), joined.end());
    joinAll();
}

void UdpMulticastTransport :: stop()
{
    if (socket) {
        socket -> close();
        delete socket;
        socket = nullptr;
    }
    joined.clear();
    ifaces.clear();
}

void UdpMulticastTransport :: send (const QByteArray &packet, int interfaceIndex)
{
    if (!socket) {
        return;
    }
    for (const MdnsInterface &item : std :: as_const(ifaces)) {
        if (interfaceIndex != 0 && item.index != interfaceIndex) {
            continue;
        }
        if (!joined.contains(item.index)) {
            continue;
        }
        socket -> setMulticastInterface(QNetworkInterface :: interfaceFromIndex(item.index));
        socket -> writeDatagram(packet, multicastGroup, mdnsPort);
    }
}

void UdpMulticastTransport :: readPending()
{
    while (socket && socket -> hasPendingDatagrams()) {
        const QNetworkDatagram datagram = socket -> receiveDatagram();
        if (!datagram.isValid() || datagram.senderPort() != mdnsPort) {
            continue; // not a proper mDNS packet (RFC 6762: source port must be 5353)
        }
        const QHostAddress from = datagram.senderAddress();
        int index = datagram.interfaceIndex();

        // accept only packets from the same local network as one of our connections
        bool ok = false;
        for (const MdnsInterface &item : std :: as_const(ifaces)) {
            if ((index == 0 || item.index == index) && onLink(from, item)) {
                index = item.index;
                ok = true;
                break;
            }
        }
        if (ok) {
            emit received(datagram.data(), from, index);
        }
    }
}

// ================= MdnsService =================

MdnsService :: MdnsService (MdnsTransport *transport, std :: function<qint64()> clock, QObject *parent)
    : QObject(parent), transport(transport), clock(clock ? clock : std :: function<qint64()>(realClock))
{
    connect(transport, &MdnsTransport :: received, this, &MdnsService :: onPacket);
    timer.setInterval(1000);
    connect(&timer, &QTimer :: timeout, this, &MdnsService :: tick);
}

MdnsService :: ~MdnsService()
{
    stop();
}

bool MdnsService :: start (const QString &deviceName, const QString &fp, quint16 listenPort, bool pairingMode, QString &error)
{
    if (isRunning) {
        return true;
    }
    if (listenPort == 0 || !TrustStore :: isValidFingerprint(fp)) {
        error = "Cannot announce this device (no port or no identity).";
        return false;
    }
    if (!transport -> start(error)) {
        return false;
    }

    fingerprint = fp;
    name = TrustStore :: cleanName(deviceName);
    port = listenPort;
    pairing = pairingMode;
    const QString shortId = fp.left(8);
    instanceName = QString("OneNode-%1.%2").arg(shortId, serviceName);
    hostName = QString("onenode-%1.local").arg(shortId);
    isRunning = true;

    const qint64 now = clock();
    announcesSent = 0;
    queryInterval = 1000;
    windowStart = now;
    packetsInWindow = 0;
    lastResponseAt.clear();

    announce(recordTtl, 0);
    sendQuery();
    announcesSent = 1;
    nextAnnounceAt = now + 1000;   // a second announcement one second later (RFC 6762 section 8.3)
    nextQueryAt = now + queryInterval;
    timer.start();
    return true;
}

void MdnsService :: stop()
{
    if (!isRunning) {
        return;
    }
    announce(0, 0); // goodbye: "forget me" (ttl 0)
    timer.stop();
    isRunning = false;
    transport -> stop();
    entries.clear();
    hostAddresses.clear();
    rebuildVisible();
}

void MdnsService :: setPairing (bool value)
{
    if (value == pairing) {
        return;
    }
    pairing = value;
    if (isRunning) {
        announce(recordTtl, 0); // tell everybody now, so their lists show "pairing" immediately
    }
}

void MdnsService :: setDeviceName (const QString &deviceName)
{
    const QString clean = TrustStore :: cleanName(deviceName);
    if (clean == name) {
        return;
    }
    name = clean;
    if (isRunning) {
        announce(recordTtl, 0);
    }
}

Dns :: Message MdnsService :: ownRecords (const MdnsInterface &iface, quint32 ttl) const
{
    Dns :: Message message;
    message.response = true;

    Dns :: Record ptr;
    ptr.name = serviceName;
    ptr.type = Dns :: TypePTR;
    ptr.ttl = ttl;
    ptr.target = instanceName;

    Dns :: Record srv;
    srv.name = instanceName;
    srv.type = Dns :: TypeSRV;
    srv.cacheFlush = true;
    srv.ttl = ttl;
    srv.port = port;
    srv.target = hostName;

    Dns :: Record txt;
    txt.name = instanceName;
    txt.type = Dns :: TypeTXT;
    txt.cacheFlush = true;
    txt.ttl = ttl;
    txt.txt.insert("v", QString :: number(2));
    txt.txt.insert("fp", fingerprint);
    QString shown = name;
    while (shown.toUtf8().size() > 240) { // one TXT entry holds 255 bytes at most
        shown.chop(1);
    }
    txt.txt.insert("name", shown);
    if (pairing) {
        txt.txt.insert("pair", "1");
    }

    Dns :: Record a;
    a.name = hostName;
    a.type = Dns :: TypeA;
    a.cacheFlush = true;
    a.ttl = ttl;
    a.address = iface.address;

    message.answers << ptr << srv << txt << a;
    return message;
}

void MdnsService :: announce (quint32 ttl, int interfaceIndex)
{
    for (const MdnsInterface &iface : transport -> interfaces()) {
        if (interfaceIndex != 0 && iface.index != interfaceIndex) {
            continue;
        }
        const QByteArray packet = Dns :: encode(ownRecords(iface, ttl));
        if (!packet.isEmpty()) {
            transport -> send(packet, iface.index);
        }
    }
}

void MdnsService :: sendQuery()
{
    Dns :: Message query;
    Dns :: Question question;
    question.name = serviceName;
    question.type = Dns :: TypePTR;
    query.questions << question;
    transport -> send(Dns :: encode(query), 0);
}

void MdnsService :: tick()
{
    if (!isRunning) {
        return;
    }
    const qint64 now = clock();
    transport -> refresh();
    forgetExpired(now);

    if (now >= nextAnnounceAt) {
        announce(recordTtl, 0);
        ++announcesSent;
        nextAnnounceAt = now + (announcesSent < 2 ? 1000 : 60000); // then every minute (records live 120 s)
    }
    if (now >= nextQueryAt) {
        sendQuery();
        queryInterval = std :: min<qint64>(queryInterval * 2, 60000); // 1, 2, 4 ... 60 seconds
        nextQueryAt = now + queryInterval;
    }
}

void MdnsService :: onPacket (const QByteArray &packet, const QHostAddress &from, int interfaceIndex)
{
    if (!isRunning) {
        return;
    }
    const qint64 now = clock();
    if (now - windowStart >= 1000) { // flood guard: a second at a time
        windowStart = now;
        packetsInWindow = 0;
    }
    if (++packetsInWindow > maxPacketsPerSecond) {
        return;
    }

    Dns :: Message message;
    if (!Dns :: decode(packet, message)) {
        return; // garbage is dropped silently
    }
    if (message.response) {
        handleResponse(message, from);
    } else {
        handleQuery(message, interfaceIndex);
    }
}

void MdnsService :: handleQuery (const Dns :: Message &message, int interfaceIndex)
{
    bool aboutUs = false;
    for (const Dns :: Question &question : message.questions) {
        const QString asked = Dns :: normalized(question.name);
        const bool anyType = question.type == Dns :: TypeANY;
        if ((asked == Dns :: normalized(serviceName) && (anyType || question.type == Dns :: TypePTR))
            || (asked == Dns :: normalized(instanceName) && (anyType || question.type == Dns :: TypeSRV || question.type == Dns :: TypeTXT))
            || (asked == Dns :: normalized(hostName) && (anyType || question.type == Dns :: TypeA))) {
            aboutUs = true;
            break;
        }
    }
    if (!aboutUs) {
        return;
    }
    const qint64 now = clock();
    if (lastResponseAt.contains(interfaceIndex) && now - lastResponseAt.value(interfaceIndex) < 1000) {
        return; // at most one answer per second per connection (RFC 6762 section 6)
    }
    lastResponseAt.insert(interfaceIndex, now);
    announce(recordTtl, interfaceIndex);
}

void MdnsService :: handleResponse (const Dns :: Message &message, const QHostAddress &from)
{
    const qint64 now = clock();
    const QString suffix = "." + Dns :: normalized(serviceName);
    bool changed = false;

    QList<Dns :: Record> all = message.answers;
    all += message.additionals;

    for (const Dns :: Record &record : std :: as_const(all)) {
        const QString recordName = Dns :: normalized(record.name);
        if (recordName == Dns :: normalized(instanceName) || recordName == Dns :: normalized(hostName)) {
            continue; // that is us (we hear our own packets) or somebody pretending to be us
        }
        const qint64 expires = now + qint64(record.ttl) * 1000;

        switch (record.type) {
        case Dns :: TypePTR: {
            if (recordName != Dns :: normalized(serviceName)) {
                break;
            }
            const QString instance = Dns :: normalized(record.target);
            if (!instance.endsWith(suffix) || instance == Dns :: normalized(instanceName)) {
                break;
            }
            if (record.ttl == 0) { // goodbye
                changed |= entries.remove(instance) > 0;
            }
            break;
        }
        case Dns :: TypeSRV: {
            if (!recordName.endsWith(suffix)) {
                break;
            }
            if (record.ttl == 0) {
                changed |= entries.remove(recordName) > 0;
                break;
            }
            if (record.port == 0) {
                break;
            }
            if (!entries.contains(recordName) && entries.size() >= maxPeers) {
                break; // full: a flood of fake devices cannot push real ones out
            }
            Entry &entry = entries[recordName];
            entry.instance = recordName;
            entry.host = Dns :: normalized(record.target);
            entry.port = record.port;
            entry.srvExpires = expires;
            changed = true;
            break;
        }
        case Dns :: TypeTXT: {
            if (!recordName.endsWith(suffix)) {
                break;
            }
            if (record.ttl == 0) {
                changed |= entries.remove(recordName) > 0;
                break;
            }
            if (!entries.contains(recordName) && entries.size() >= maxPeers) {
                break;
            }
            Entry &entry = entries[recordName];
            entry.instance = recordName;
            entry.txt = record.txt;
            entry.txtExpires = expires;
            changed = true;
            break;
        }
        case Dns :: TypeA: {
            if (!usableAddress(record.address)) {
                break;
            }
            if (record.ttl == 0) {
                if (hostAddresses.contains(recordName)) {
                    changed |= hostAddresses[recordName].remove(record.address) > 0;
                }
                break;
            }
            if (!hostAddresses.contains(recordName) && hostAddresses.size() >= maxPeers * 2) {
                break;
            }
            QHash<QHostAddress, qint64> &addresses = hostAddresses[recordName];
            if (addresses.size() < 8 || addresses.contains(record.address)) {
                addresses.insert(record.address, expires);
                changed = true;
            }
            break;
        }
        default:
            break;
        }
    }

    // A sender that gave no address record at all: its packet's source address is the next best thing.
    if (usableAddress(from)) {
        for (const Dns :: Record &record : std :: as_const(all)) {
            if (record.type == Dns :: TypeSRV && record.ttl > 0 && entries.contains(Dns :: normalized(record.name))) {
                const QString host = entries.value(Dns :: normalized(record.name)).host;
                if (!hostAddresses.contains(host) || hostAddresses.value(host).isEmpty()) {
                    if (hostAddresses.size() < maxPeers * 2) {
                        hostAddresses[host].insert(from, now + qint64(record.ttl) * 1000);
                        changed = true;
                    }
                }
            }
        }
    }

    if (changed) {
        rebuildVisible();
    }
}

void MdnsService :: forgetExpired (qint64 now)
{
    bool changed = false;
    for (auto it = entries.begin(); it != entries.end();) {
        if (it -> srvExpires <= now && it -> srvExpires != 0) {
            it = entries.erase(it);
            changed = true;
        } else if (it -> txtExpires <= now && it -> txtExpires != 0) {
            it = entries.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    for (auto host = hostAddresses.begin(); host != hostAddresses.end();) {
        for (auto address = host -> begin(); address != host -> end();) {
            if (address.value() <= now) {
                address = host -> erase(address);
                changed = true;
            } else {
                ++address;
            }
        }
        if (host -> isEmpty()) {
            host = hostAddresses.erase(host);
        } else {
            ++host;
        }
    }
    if (changed) {
        rebuildVisible();
    }
}

void MdnsService :: rebuildVisible()
{
    QList<MdnsPeer> list;
    for (const Entry &entry : std :: as_const(entries)) {
        if (entry.port == 0 || entry.srvExpires == 0 || entry.txtExpires == 0) {
            continue; // not everything is known yet
        }
        if (txtValue(entry.txt, "v") != "2") {
            continue; // another protocol version: the transfer would be refused anyway
        }
        const QString fp = txtValue(entry.txt, "fp");
        if (!TrustStore :: isValidFingerprint(fp) || fp == fingerprint) {
            continue;
        }
        const QHash<QHostAddress, qint64> addresses = hostAddresses.value(entry.host);
        if (addresses.isEmpty()) {
            continue;
        }
        MdnsPeer peer;
        peer.fingerprint = fp;
        peer.name = TrustStore :: cleanName(txtValue(entry.txt, "name"));
        peer.port = entry.port;
        peer.pairing = txtValue(entry.txt, "pair") == "1";
        for (auto it = addresses.constBegin(); it != addresses.constEnd(); ++it) {
            peer.addresses.append(it.key());
        }
        std :: sort(peer.addresses.begin(), peer.addresses.end(), [] (const QHostAddress &a, const QHostAddress &b) {
            return a.toIPv4Address() < b.toIPv4Address();
        });
        list.append(peer);
    }
    std :: sort(list.begin(), list.end(), [] (const MdnsPeer &a, const MdnsPeer &b) {
        return a.name != b.name ? a.name.toLower() < b.name.toLower() : a.fingerprint < b.fingerprint;
    });

    // only tell the screen when something it shows really changed
    QString signature;
    for (const MdnsPeer &peer : list) {
        signature += peer.fingerprint + '|' + peer.name + '|' + QString :: number(peer.port) + '|' + (peer.pairing ? "1" : "0");
        for (const QHostAddress &address : peer.addresses) {
            signature += '|' + address.toString();
        }
        signature += '\n';
    }
    visible = list;
    if (signature != visibleSignature) {
        visibleSignature = signature;
        emit peersChanged();
    }
}
