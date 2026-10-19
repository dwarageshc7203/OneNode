#ifndef COREBRIDGE_H
#define COREBRIDGE_H

#include <QObject>
#include <QTcpServer>

class CoreBridge : public QObject {

    Q_OBJECT

public:
    explicit CoreBridge (QObject *parent = nullptr);
    Q_INVOKABLE bool startListening();
    Q_INVOKABLE void stopListening();

private:
    QTcpServer *server;
    void handleNewConnection();
    void readClientData();
};

#endif // COREBRIDGE_H
