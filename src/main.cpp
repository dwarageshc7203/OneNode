#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include "core/corebridge.h"
#include <QtQml>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    // Exposing the class CoreBridge to QML
    qmlRegisterType<CoreBridge>("OneNode", 1, 0, "CoreBridge");

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("OneNode", "Main");

    return app.exec();
}
