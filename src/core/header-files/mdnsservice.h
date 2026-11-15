#pragma once

#include "dnsmessage.h"

#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

#include <functional>

class QUdpSocket;

// Finding other OneNode devices on the same Wi-Fi without typing an IP address (mDNS / DNS-SD, RFC 6762 + 6763).
//
// Analogy: every device stands up in a room and says "Hi, I'm a OneNode, my name is X, call me on this number".
// Everybody else hears it and writes it on a "nearby devices" list.
//
// IMPORTANT (security): being on that list gives a device NO trust at all. Anyone in the room can shout anything.
// Trust comes only from pairing (PIN) and the TLS fingerprint check. The list is a convenience, nothing more.

struct MdnsInterface {
    int index = 0;               // the operating system's number for this network connection
    QHostAddress address;        // our IPv4 address on it
    int prefixLength = 24;       // how big that local network is (to recognise "same network" senders)
};

// How packets get in and out. The real one is UdpMulticastTransport (below); tests use a fake room.
class MdnsTransport : public QObject {
    Q_OBJECT
public:
    using QObject :: QObject;
    virtual bool start (QString &error) = 0;
    virtual void stop() = 0;
    virtual void refresh() {}                                   // re-read the network connections (Wi-Fi came and went)
    virtual QList<MdnsInterface> interfaces() const = 0;
    virtual void send (const QByteArray &packet, int interfaceIndex) = 0;  // interfaceIndex 0 = every interface
signals:
    void received (const QByteArray &packet, const QHostAddress &from, int interfaceIndex);
};

// The real thing: one UDP socket on 224.0.0.251:5353, joined on every usable network connection.
class UdpMulticastTransport : public MdnsTransport {
    Q_OBJECT
public:
    explicit UdpMulticastTransport (QObject *parent = nullptr);
    ~UdpMulticastTransport() override;
    bool start (QString &error) override;
    void stop() override;
    void refresh() override;
    QList<MdnsInterface> interfaces() const override { return ifaces; }
    void send (const QByteArray &packet, int interfaceIndex) override;

    // which connections are worth using (up, running, multicast, not loopback, has an IPv4 address)
    static QList<MdnsInterface> usableInterfaces();
    // is `from` on the same local network as the connection? (RFC 6762: ignore everything from elsewhere)
    static bool onLink (const QHostAddress &from, const MdnsInterface &iface);

private:
    QUdpSocket *socket = nullptr;
    QList<MdnsInterface> ifaces;
    QList<int> joined;
    void joinAll();
    void readPending();
};

// What the "nearby devices" list shows for one device.
struct MdnsPeer {
    QString name;                    // what the device calls itself (cleaned, untrusted text)
    QString fingerprint;             // what it CLAIMS its fingerprint is (TLS will check the truth)
    QList<QHostAddress> addresses;   // where to connect
    quint16 port = 0;
    bool pairing = false;            // it says it is in "Pair a new device" mode
    QHostAddress address() const { return addresses.isEmpty() ? QHostAddress() : addresses.first(); }
};

class MdnsService : public QObject {
    Q_OBJECT
public:
    static constexpr const char *serviceName = "_onenode._tcp.local";
    static constexpr quint32 recordTtl = 120;           // seconds a record lives without a refresh
    static constexpr int maxPeers = 64;                 // a flood of fake devices cannot grow memory
    static constexpr int maxPacketsPerSecond = 500;     // ... or burn CPU

    // `clock` returns milliseconds and is only for tests; the default is the real clock.
    explicit MdnsService (MdnsTransport *transport, std :: function<qint64()> clock = nullptr, QObject *parent = nullptr);
    ~MdnsService() override;

    // Starts announcing this device and looking for others. `port` is where our TLS server listens.
    bool start (const QString &deviceName, const QString &fingerprint, quint16 port, bool pairing, QString &error);
    void stop();                                    // says goodbye and forgets everybody
    bool running() const { return isRunning; }
    void setPairing (bool pairing);                 // "Pair a new device" mode on/off: announced to the others
    void setDeviceName (const QString &deviceName);

    QList<MdnsPeer> peers() const { return visible; }

    // One step of the clock: sends what is due, forgets what expired. A 1 second timer calls it; tests call it by hand.
    void tick();

signals:
    void peersChanged();

private:
    struct Entry {                                   // everything we learned about one instance name
        QString instance;                            // "OneNode-1a2b3c4d._onenode._tcp.local"
        QString host;                                // from SRV
        quint16 port = 0;
        QMap<QString, QString> txt;
        qint64 srvExpires = 0, txtExpires = 0;       // 0 = not known
    };
    MdnsTransport *transport;
    std :: function<qint64()> clock;
    QTimer timer;

    bool isRunning = false;
    QString name, fingerprint;
    quint16 port = 0;
    bool pairing = false;
    QString instanceName, hostName;                  // ours

    QHash<QString, Entry> entries;                   // by normalized instance name
    QHash<QString, QHash<QHostAddress, qint64>> hostAddresses; // normalized host -> address -> expiry
    QList<MdnsPeer> visible;
    QString visibleSignature;

    qint64 nextAnnounceAt = 0, nextQueryAt = 0;
    int announcesSent = 0;
    qint64 queryInterval = 1000;
    QHash<int, qint64> lastResponseAt;               // per interface: at most one answer a second
    qint64 windowStart = 0;
    int packetsInWindow = 0;

    void onPacket (const QByteArray &packet, const QHostAddress &from, int interfaceIndex);
    void handleQuery (const Dns :: Message &message, int interfaceIndex);
    void handleResponse (const Dns :: Message &message, const QHostAddress &from);
    void announce (quint32 ttl, int interfaceIndex);  // our own records, as a response
    void sendQuery();
    Dns :: Message ownRecords (const MdnsInterface &iface, quint32 ttl) const;
    void rebuildVisible();
    void forgetExpired (qint64 now);
};
