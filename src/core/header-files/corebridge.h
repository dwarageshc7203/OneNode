#ifndef COREBRIDGE_H
#define COREBRIDGE_H

#include "protocol.h"
#include "sendsession.h"

#include <QObject>
#include <QSslConfiguration>
#include <QSslServer>
#include <QString>
#include <QSet>
#include <QList>
#include <QUrl>
#include <QHash>
#include <QPointer>
#include <QVariantList>

#include <memory>

class MdnsService;
class MdnsTransport;
class PairingMode;
class TrustStore;

class CoreBridge : public QObject {

    Q_OBJECT

public:
    enum class Direction {Send, Receive}; // to find flow of the data
    Q_ENUM(Direction);

    explicit CoreBridge (QObject *parent = nullptr);
    explicit CoreBridge (const Protocol :: Timeouts &timeouts, QObject *parent = nullptr); // tests use tiny timeouts
    ~CoreBridge() override;
    Q_INVOKABLE bool startListening(quint16 port = Protocol :: defaultPort); // app startup function
    Q_INVOKABLE void stopListening(); // app quit function
    Q_INVOKABLE void sendFile(const QString &host, const QString &filePath, quint16 port = Protocol :: defaultPort); // core file transfer logic starts here
    Q_INVOKABLE QString receiveFolder() const;  // the folder where the receiver gets his files
    Q_INVOKABLE QUrl receiveFolderUrl() const;  // the same folder as a url, for the folder dialog
    Q_INVOKABLE quint64 listeningPort() const; // the actual listening port (0 when not listening)
    Q_INVOKABLE QString localAddress() const; // this machine's address on the local network, for the other device to type in

    void setReceiveDir(const QString &dir); // to set the destination directory

    // The device's identity (its certificate + key, see identity.h). Must be loaded once at startup,
    // before startListening / sendFile. Returns "" on success, otherwise a message for the user.
    QString loadIdentity(const QString &folder);
    // Where the identity (and later the trusted devices) live. The environment variable ONENODE_DATA_DIR
    // overrides it, so that two copies of OneNode on one computer can be two different devices.
    static QString defaultDataFolder();
    Q_INVOKABLE QString myFingerprint() const; // this device's fingerprint (empty before loadIdentity)

    // A problem found while starting (no OpenSSL, damaged identity ...). main.cpp sets it, the screen shows it.
    // Empty when everything is fine.
    Q_INVOKABLE QString startupProblem() const { return problem; }
    void setStartupProblem (const QString &text) { problem = text; }

    // ---- this device's name (shown to the devices it meets) ----
    Q_INVOKABLE QString deviceName() const;
    void setDeviceName (const QString &name);
    // What the Settings page calls: cleans the name, uses it from now on (also in the nearby list of the other devices)
    // and remembers it for the next start. Returns "" or a message for the user.
    Q_INVOKABLE QString changeDeviceName (const QString &newName);

    // ---- pairing: the receiver side ("Pair a new device") ----
    Q_INVOKABLE void startPairing();      // opens pairing mode: creates a PIN, emits pairingStarted
    Q_INVOKABLE void cancelPairing();     // closes pairing mode
    Q_INVOKABLE bool pairingActive() const;
    Q_INVOKABLE int pairingSeconds() const { return timeouts.pinLifetimeMs / 1000; } // how long a PIN lives (for the countdown)

    // ---- pairing: the sender side (the PIN typed in) ----
    // Answers a pinNeeded signal. Returns "" or a message for the user (for example "The PIN is 6 digits.").
    Q_INVOKABLE QString submitPin (quint64 id, const QString &pin);
    Q_INVOKABLE void cancelPin (quint64 id);

    // ---- the list of paired devices ----
    Q_INVOKABLE QVariantList trustedDevices() const; // each: {name, fingerprint, shortFingerprint, pairedAt}
    Q_INVOKABLE QString removeTrustedDevice (const QString &fingerprint); // "" or a message
    TrustStore *trustStore() const { return trust.get(); } // for tests; the screens use the two functions above

    // Turns what the user typed ("192.168.1.23" or "192.168.1.23:45454") into a host and a port.
    // Returns an empty string when it is fine, otherwise a message that can be shown to the user as it is.
    static QString parseAddress(const QString &text, QString &host, quint16 &port);

    // What the Settings page calls after the user picked a folder: checks it, uses it from now on,
    // and remembers it for the next start. Returns "" on success, or a message for the user (nothing changed then).
    Q_INVOKABLE QString changeReceiveFolder(const QUrl &folder);

    // What the Send page calls: one address, one or more files (as urls from the file dialog or a drop).
    // Starts one sendFile for every file. Returns "" on success, or a message for the user (nothing was started then).
    Q_INVOKABLE QString sendTo(const QString &address, const QList<QUrl> &files);

    // ---- nearby devices (mDNS, see mdnsservice.h) ----
    // Starts announcing this device and looking for others. Needs startListening first (the port is announced).
    // Returns "" or a message for the user ("No network connection ...").
    Q_INVOKABLE QString startDiscovery();
    Q_INVOKABLE void stopDiscovery();
    // Devices seen on the network, each: {name, fingerprint, shortFingerprint, address, port, trusted, pairing}.
    // "trusted" means: it is on OUR paired list. Being nearby alone proves nothing.
    Q_INVOKABLE QVariantList nearbyDevices() const;
    // Like sendTo, but to a device from the nearby list (by fingerprint). The connection is refused
    // when the device behind that address turns out to have a different certificate than it announced.
    Q_INVOKABLE QString sendToNearby(const QString &fingerprint, const QList<QUrl> &files);
    // Tests give a fake network here (before startDiscovery). The bridge takes ownership.
    void setDiscoveryTransport(MdnsTransport *transport);



signals:
    void transferStarted (quint64 id, const QString &fileName, CoreBridge :: Direction direction, qint64 total, const QString &peer); // peer = the other device's address
    void transferProgress (quint64 id, const QString &fileName, qint64 done, qint64 total);
    void transferFinished (quint64 id, const QString &fileName);
    void transferFailed (quint64 id, const QString &fileName, const QString &reason);

    // pairing (see PROTOCOL.md section 5)
    void pairingStarted (const QString &pin);                  // receiver: show this PIN ("482 913") to the user
    void pairingEnded (const QString &reason);                 // receiver: "paired", "failed", "cancelled" or "expired"
    void pinNeeded (quint64 id, const QString &peerName);      // sender: ask the user for the PIN shown on the other device
    void devicePaired (const QString &name, const QString &fingerprint); // either side: a new device was added to the list
    void trustedDevicesChanged();
    void nearbyDevicesChanged();                                // the nearby list (or its "trusted" marks) changed

private:

    // Objects and variables
    Protocol :: Timeouts timeouts;
    QSslServer *server; // for the receiver side (every connection is TLS)
    PairingMode *pairing; // the receiver's "Pair a new device" mode
    std :: unique_ptr<TrustStore> trust; // the paired devices (exists after loadIdentity)
    QString name; // this device's name
    QString problem; // see startupProblem()
    QHash<quint64, QPointer<SendSession>> waitingForPin; // sends that wait for the user to type the PIN
    MdnsTransport *discoveryTransport = nullptr; // owned by this object (a child)
    std :: unique_ptr<MdnsService> discovery;    // exists between startDiscovery and stopDiscovery
    QSslConfiguration tls; // our certificate, key and TLS rules
    bool haveIdentity = false; // true after loadIdentity succeeded
    QString fingerprint; // our own fingerprint
    QString receiveDir; // the save directory on the receiver end
    quint64 nextId = 1; // id of the next process
    QSet <quint64> announced; // IDs whose transferStarted has been emitted

    // Methods
    void handleNewConnection(); // This method creates a new session using ReceiveSession
    void announce (quint64 id, const QString &fileName, Direction direction, qint64 total, const QString &peer);
    SendSession *startSend (const QString &host, const QString &filePath, quint16 port, const QString &expectedFingerprint = QString()); // nullptr when it could not start (the row is reported failed)
    QString sendToAddress (const QString &address, const QList<QUrl> &files, const QString &expectedFingerprint); // sendTo and sendToNearby share this
    void failRow (const QString &filePath, const QString &host, const QString &reason); // a row that never started
    void init(); // common part of the constructors
};

#endif // COREBRIDGE_H
