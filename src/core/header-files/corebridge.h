#ifndef COREBRIDGE_H
#define COREBRIDGE_H

#include "protocol.h"

#include <QObject>
#include <QTcpServer>
#include <QString>
#include <QSet>

class CoreBridge : public QObject {

    Q_OBJECT

public:
    enum class Direction {Send, Receive}; // to find flow of the data
    Q_ENUM(Direction);

    explicit CoreBridge (QObject *parent = nullptr);
    Q_INVOKABLE bool startListening(quint16 port = Protocol :: defaultPort); // app startup function
    Q_INVOKABLE void stopListening(); // app quit function
    Q_INVOKABLE void sendFile(const QString &host, const QString &filePath, quint16 port = Protocol :: defaultPort); // core file transfer logic starts here
    QString receiveFolder() const;  // the folder where the receiver gets his files
    quint64 listeningPort() const; // the actual listening port

    void setReceiveDir(const QString &dir); // to set the destination directory



signals:
    void transferStarted (quint64 id, const QString &fileName, CoreBridge :: Direction direction, qint64 total);
    void transferProgress (quint64 id, const QString &fileName, qint64 done, qint64 total);
    void transferFinished (quint64 id, const QString &fileName);
    void transferFailed (quint64 id, const QString &fileName, const QString &reason);

private:

    // Objects and variables
    QTcpServer *server; // for the receiver side
    QString receiveDir; // the save directory on the receiver end
    quint64 nextId = 1; // id of the next process
    QSet <quint64> announced; // IDs whose transferStarted has been emitted

    // Methods
    void handleNewConnection(); // This method creates a new session using ReceiveSession
    void announce (quint64 id, const QString &fileName, Direction direction, qint64 total);
};

#endif // COREBRIDGE_H
