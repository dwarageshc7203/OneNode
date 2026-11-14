#include "corebridge.h"
#include "transfermodel.h"

#include <QtQml>
#include <QFontDatabase>
#include <QDir>
#include <QSettings>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QDebug>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    // Exposing the class CoreBridge to QML (its enum Direction is used by Main.qml)
    qmlRegisterType<CoreBridge>("OneNode", 1, 0, "CoreBridge");

    const QStringList weights { "Regular", "Medium", "SemiBold", "Bold"};
    for (const QString &weight : weights) {
        if (QFontDatabase :: addApplicationFont(":/fonts/Inter-" + weight + ".otf") == -1) {
            qWarning() << "Could not load the font Inter - " << weight;
        }
    }

    QGuiApplication :: setFont(QFont("Inter"));

    // The two C++ objects the screens need. They are created HERE (not in QML) because TransferModel needs the
    // core as an argument, and they live in main(), so they outlive the window.
    CoreBridge core;

    // every device needs its identity (certificate + key) before it can talk TLS; created on the first start
    const QString identityError = core.loadIdentity(CoreBridge :: defaultDataFolder());
    if (!identityError.isEmpty()) {
        qWarning() << identityError; // (the screen will show this properly in a later step)
    }

    // use the receive folder the user chose last time (see CoreBridge::changeReceiveFolder), if it still exists
    const QString savedFolder = QSettings("OneNode", "OneNode").value("receiveDir").toString();
    if (!savedFolder.isEmpty() && QDir(savedFolder).exists()) {
        core.setReceiveDir(savedFolder);
    }

    TransferModel transfers(&core);

    QQmlApplicationEngine engine;

    // Hand them to Main.qml: they arrive as its "required property core" and "required property transfers".
    // This must happen BEFORE loadFromModule.
    engine.setInitialProperties({
        { "core", QVariant :: fromValue(&core) },
        { "transfers", QVariant :: fromValue(&transfers) }
    });

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("OneNode", "Main");

    return app.exec();
}
