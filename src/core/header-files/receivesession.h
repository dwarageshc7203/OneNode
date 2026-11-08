#ifndef RECEIVESESSION_H
#define RECEIVESESSION_H

#include "progressthrottle.h"

#include <QObject>
#include <QTcpSocket>
#include <QFile>
#include <QString>
#include <QCryptographicHash>

class ReceiveSession : public QObject
{
    Q_OBJECT

public:
    explicit ReceiveSession (QTcpSocket *clientSocket, QObject *parent = nullptr);
    explicit ReceiveSession (QTcpSocket *clientSocket, const QString &destinationDir, QObject *parent = nullptr); // constructor with the destination file path for file saving
    ~ReceiveSession() override; // destructor

    // helper function for the suffix for the file being written
    static QString partSuffix() {
        return QStringLiteral(".onenode-part"); // the suffix for the file being written
    }

signals:
    void progress(const QString &fileName, const qint64 done, const qint64 total);
    void finished(const QString &fileName);
    void failed(const QString &fileName, const QString &reason);

private:

    QTcpSocket *socket; // this is the connection
    QFile *incomingFile = nullptr; // Points to the file or image sent as an object
    qint64 expectedSize = 0; // Size of the image or file before sending
    qint64 receivedSize = 0; // Size of the image after receiving
    QByteArray receivedBuffer; // Stores the bytes of Header length prefix, JSON header and the body
    quint32 expectedHeaderSize = 0; // Stores the size of the JSON header
    enum class State { AwaitPrefix, AwaitHeader, ReceiveBody, AwaitTrailer}; // enum with 4 states
    QCryptographicHash hash {QCryptographicHash :: Sha256}; // fingerprint of the body bytes
    State state = State :: AwaitPrefix;
    QString finalPath; // Actual and valid name the file gets after completing all the checks
    QString currentName; // file name derived from the header
    bool closing = false; // allows no other action ; much like operation is closed
    const QString destinationDir; // destination folder
    ProgressThrottle progressThrottle; // gate keeper that limits the amount of signal sent to QML

    //methods

    void onDisconnected(); // disconnection handle
    bool handlePrefix(); // prefix handler
    bool handleHeader(); // header handler
    void finishFile(); // checks if the file is received successfully and handles file writing
    bool handleBody(); // handles body bytes and hashing
    bool processBuffer(); // switch case for different states (prefix, header, body, trailer)
    void readData(); // managing and reading data on the socket and appending to buffer with condition
    void sendResult(const QString &status, const QString &code = QString()); // sending acknowledgement to the sender
    void reject(const QString &code, const QString &reason); // handles the failed signal and disconnects from sender
    bool handleTrailer(); // handles the trailer(file_end) and checksum after the body is received

};

#endif // RECEIVESESSION_H
