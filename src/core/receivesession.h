#ifndef RECEIVESESSION_H
#define RECEIVESESSION_H

#include <QObject>
#include <QTcpSocket>
#include <QFile>
#include <QString>

class ReceiveSession : public QObject
{
    Q_OBJECT

public:
    explicit ReceiveSession (QTcpSocket *clientSocket, QObject *parent = nullptr);
    explicit ReceiveSession (QTcpSocket *clientSocket, const QString &destinationDir, QObject *parent = nullptr); // constructor with the destination file path for file saving
    ~ReceiveSession() override; // destructor and with overriding power over CoreBridge

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
    enum class State { AwaitPrefix, AwaitHeader, ReceiveBody}; // enum with 3 states
    State state = State :: AwaitPrefix;
    QString finalPath; // Actual and valid name the file gets after completing all the checks
    QString currentName; // file name currently (.part or complete file)
    bool closing = false; // allows no other action ; much like operation is closed
    const QString destinationDir; // destination folder

    //methods

    void onDisconnected();
    bool handlePrefix();
    bool handleHeader();
    void finishFile();
    bool handleBody();
    bool processBuffer();
    void readData();
    void sendResult(const QString &status, const QString &code = QString());
    void reject(const QString &code, const QString &reason);

};

#endif // RECEIVESESSION_H
