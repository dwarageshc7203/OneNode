#ifndef COREBRIDGE_H
#define COREBRIDGE_H

#include <QObject>
#include <QTcpServer>
#include <QString>

class CoreBridge : public QObject {

    Q_OBJECT

public:
    explicit CoreBridge (QObject *parent = nullptr);
    Q_INVOKABLE bool startListening(); // app startup function
    Q_INVOKABLE void stopListening(); // app quit function
    Q_INVOKABLE void sendFile(const QString &host, const QString &filePath); // core file transfer logic starts here

signals:
    void transferProgress(const QString &fileName, qint64 done, qint64 total);
    void transferFinished(const QString &fileName);
    void transferFailed(const QString &fileName, const QString &reason);

private:

    // Objects and variables
    QTcpServer *server; // for the receiver side
    QString receiveDir; // the save directory on the receiver end

    // Methods
    void handleNewConnection(); // This method creates a new session using ReceiveSession
};

#endif // COREBRIDGE_H
