#include <QApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include "mainwindow.h"

#define SOCKET_NAME "dropin-single-instance"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    // Debug — log all arguments
    for (int i = 0; i < argc; i++) {
        qDebug() << "arg" << i << ":" << argv[i];
    }
    app.setQuitOnLastWindowClosed(false);

    QString filePath;
    if (argc > 1) {
        filePath = QString::fromLocal8Bit(argv[1]);
    }

    // Try connecting to an existing instance
    QLocalSocket socket;
    socket.connectToServer(SOCKET_NAME);
    if (socket.waitForConnected(500)) {
        // Another instance is running — send the file path to it
        if (!filePath.isEmpty()) {
            socket.write(filePath.toUtf8());
            socket.flush();
            socket.waitForBytesWritten(1000);
        }
        socket.close();
        return 0; // Exit this instance
    }

    // No existing instance — we are the primary instance
    MainWindow window;

    // If launched with a file argument directly, send it
    if (!filePath.isEmpty()) {
        window.sendFilePath(filePath);
    }

    // Start local server to receive file paths from future instances
    QLocalServer server;
    QLocalServer::removeServer(SOCKET_NAME); // clean up stale socket
    server.listen(SOCKET_NAME);

    QObject::connect(&server, &QLocalServer::newConnection, [&]() {
        QLocalSocket *client = server.nextPendingConnection();
        QObject::connect(client, &QLocalSocket::readyRead, [&, client]() {
            QString path = QString::fromUtf8(client->readAll());
            if (!path.isEmpty()) {
                window.sendFilePath(path);
            }
            client->close();
        });
    });

    window.show();
    return app.exec();
}