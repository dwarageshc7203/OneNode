#include "pairingserver.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

PairingServer::PairingServer(QObject *parent)
    : QObject(parent), server(new QTcpServer(this)), pendingClient(nullptr)
{
    connect(server, &QTcpServer::newConnection,
            this, &PairingServer::onNewConnection);
}

void PairingServer::start(const QString &code) {
    activeCode = code;
    if (!server->isListening()) {
        server->listen(QHostAddress::Any, port());
    }
}

void PairingServer::stop() {
    if (pendingClient) {
        pendingClient->disconnectFromHost();
        pendingClient = nullptr;
    }
    server->close();
}

void PairingServer::onNewConnection() {
    if (pendingClient) {
        QTcpSocket *extra = server->nextPendingConnection();
        extra->disconnectFromHost();
        return;
    }
    pendingClient = server->nextPendingConnection();
    connect(pendingClient, &QTcpSocket::readyRead,
            this, &PairingServer::onDataReceived);
    connect(pendingClient, &QTcpSocket::disconnected,
            this, &PairingServer::onClientDisconnected);
}

void PairingServer::onDataReceived() {
    QByteArray data = pendingClient->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);

    if (doc.isNull()) {
        QJsonObject resp;
        resp["status"] = "error";
        resp["message"] = "Invalid data";
        pendingClient->write(QJsonDocument(resp).toJson(QJsonDocument::Compact) + "\n");
        pendingClient->flush();
        pendingClient->disconnectFromHost();
        return;
    }

    QJsonObject obj = doc.object();
    QString receivedCode = obj["code"].toString();
    QString deviceName   = obj["device"].toString("Unknown device");

    if (receivedCode != activeCode) {
        QJsonObject resp;
        resp["status"] = "error";
        resp["message"] = "Invalid code";
        pendingClient->write(QJsonDocument(resp).toJson(QJsonDocument::Compact) + "\n");
        pendingClient->flush();
        pendingClient->disconnectFromHost();
        emit pairingFailed("Wrong code entered on device");
        return;
    }

    QString token    = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString clientIp = pendingClient->peerAddress().toString();

    QJsonObject resp;
    resp["status"] = "ok";
    resp["token"]  = token;
    pendingClient->write(QJsonDocument(resp).toJson(QJsonDocument::Compact) + "\n");
    pendingClient->flush();

    emit devicePaired(deviceName, token, clientIp);
    stop();
}

void PairingServer::onClientDisconnected() {
    pendingClient = nullptr;
}
