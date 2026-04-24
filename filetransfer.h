#pragma once
#include <QObject>
#include <QTcpSocket>
#include <QString>

class FileTransfer : public QObject {
    Q_OBJECT

public:
    explicit FileTransfer(QObject *parent = nullptr);
    void sendFile(const QString &filePath, const QString &peerIp, int port = 45679);

signals:
    void transferStarted(const QString &fileName);
    void transferProgress(int percent);
    void transferDone(const QString &fileName);
    void transferFailed(const QString &reason);

private slots:
    void onConnected();
    void onBytesWritten(qint64 bytes);
    void onError(QAbstractSocket::SocketError error);

private:
    QTcpSocket *socket;
    QString     filePath;
    QString     fileName;
    qint64      fileSize;
    qint64      totalWritten;
    bool        transferActive;
    bool        transferCompleted;
};