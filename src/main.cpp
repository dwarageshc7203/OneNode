#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include "corebridge.h"
#include <QtQml>
#include <QFontDatabase>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    // Exposing the class CoreBridge to QML
    qmlRegisterType<CoreBridge>("OneNode", 1, 0, "CoreBridge");

    const QStringList weights { "Regular", "Medium", "SemiBold", "Bold"};
    for (const QString &weight : weights) {
        if (QFontDatabase :: addApplicationFont(":/fonts/Inter-" + weight + ".otf") == -1) {
            qWarning() << "Could not load the font Inter - " << weight;
        }
    }

    QGuiApplication :: setFont(QFont("Inter"));

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
