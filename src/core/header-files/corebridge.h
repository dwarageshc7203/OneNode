#ifndef COREBRIDGE_H
#define COREBRIDGE_H

#include "protocol.h"

#include <QObject>
#include <QSslConfiguration>
#include <QSslServer>
#include <QString>
#include <QSet>
#include <QList>
#include <QUrl>

class CoreBridge : public QObject {

    Q_OBJECT

public:
    enum class Direction {Send, Receive}; // to find flow of the data
    Q_ENUM(Direction);

    explicit CoreBridge (QObject *parent = nullptr);
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

    // Turns what the user typed ("192.168.1.23" or "192.168.1.23:45454") into a host and a port.
    // Returns an empty string when it is fine, otherwise a message that can be shown to the user as it is.
    static QString parseAddress(const QString &text, QString &host, quint16 &port);

    // What the Settings page calls after the user picked a folder: checks it, uses it from now on,
    // and remembers it for the next start. Returns "" on success, or a message for the user (nothing changed then).
    Q_INVOKABLE QString changeReceiveFolder(const QUrl &folder);

    // What the Send page calls: one address, one or more files (as urls from the file dialog or a drop).
    // Starts one sendFile for every file. Returns "" on success, or a message for the user (nothing was started then).
    Q_INVOKABLE QString sendTo(const QString &address, const QList<QUrl> &files);



signals:
    void transferStarted (quint64 id, const QString &fileName, CoreBridge :: Direction direction, qint64 total, const QString &peer); // peer = the other device's address
    void transferProgress (quint64 id, const QString &fileName, qint64 done, qint64 total);
    void transferFinished (quint64 id, const QString &fileName);
    void transferFailed (quint64 id, const QString &fileName, const QString &reason);

private:

    // Objects and variables
    QSslServer *server; // for the receiver side (every connection is TLS)
    QSslConfiguration tls; // our certificate, key and TLS rules
    bool haveIdentity = false; // true after loadIdentity succeeded
    QString fingerprint; // our own fingerprint
    QString receiveDir; // the save directory on the receiver end
    quint64 nextId = 1; // id of the next process
    QSet <quint64> announced; // IDs whose transferStarted has been emitted

    // Methods
    void handleNewConnection(); // This method creates a new session using ReceiveSession
    void announce (quint64 id, const QString &fileName, Direction direction, qint64 total, const QString &peer);
};

#endif // COREBRIDGE_H
