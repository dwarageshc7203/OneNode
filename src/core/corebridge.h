#ifndef COREBRIDGE_H
#define COREBRIDGE_H

#include <QObject>
#include <QTcpServer>
#include <QString>

class CoreBridge : public QObject {

    Q_OBJECT

public:
    explicit CoreBridge (QObject *parent = nullptr);
    Q_INVOKABLE bool startListening();
    Q_INVOKABLE void stopListening();
    Q_INVOKABLE void sendFile(const QString &host, const QString &filePath);

signals:
    void transferProgress(const QString &fileName, qint64 done, qint64 total);
    void transferFinished(const QString &fileName);
    void transferFailed(const QString &fileName, const QString &reason);

private:

    // Objects and variables

    QTcpServer *server; // The engine

    // Methods

    void handleNewConnection(); // This method creates a new session using ReceiveSession
};

#endif // COREBRIDGE_H
