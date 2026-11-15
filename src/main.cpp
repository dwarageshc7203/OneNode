#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include "corebridge.h"
#include "tlsconfig.h"
#include "transfermodel.h"
#include <QtQml>
#include <QFontDatabase>
#include <QSettings>
#include <QDir>
#include <QSslSocket>
#include <QTemporaryDir>
#include <cstdio>

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

    // Make TLS identical on every operating system (see tlsconfig.h). Must happen before any socket exists.
    // A problem is not fatal here: the window opens and shows it at the top, so the user is not left with nothing.
    const QString tlsProblem = TlsConfig :: selectBackend();

    // "appOneNode --selftest": checks the things a broken package would lack (TLS backend, OpenSSL libraries,
    // creating a device identity), prints the result and exits WITHOUT opening a window. The release workflows
    // run it on the packaged app, so a package that cannot do TLS is never published.
    if (app.arguments().contains("--selftest")) {
        QTemporaryDir scratch;
        CoreBridge probe;
        const QString problem = !tlsProblem.isEmpty() ? tlsProblem : probe.loadIdentity(scratch.path());
        if (!problem.isEmpty()) {
            std::fprintf(stderr, "SELFTEST FAILED: %s\n", qPrintable(problem));
            return 3;
        }
        std::printf("SELFTEST OK: TLS backend %s, %s\n", qPrintable(QSslSocket :: activeBackend()), qPrintable(QSslSocket :: sslLibraryVersionString()));
        return 0;
    }

    // The two C++ objects the screens need. They are created HERE (not in QML) because TransferModel needs the
    // core as an argument, and they live in main(), so they outlive the window.
    CoreBridge core;

    // The device's identity (certificate + key). Created on the first start, loaded on every later one.
    const QString identityError = core.loadIdentity(CoreBridge :: defaultDataFolder());
    core.setStartupProblem(!tlsProblem.isEmpty() ? tlsProblem : identityError);

    // use the receive folder the user chose last time (see CoreBridge::changeReceiveFolder), if it still exists
    QSettings settings("OneNode", "OneNode");
    const QString savedFolder = settings.value("receiveDir").toString();
    if (!savedFolder.isEmpty() && QDir(savedFolder).exists()) {
        core.setReceiveDir(savedFolder);
    }

    // and the device name the user chose last time (see CoreBridge::changeDeviceName)
    const QString savedName = settings.value("deviceName").toString();
    if (!savedName.trimmed().isEmpty()) {
        core.setDeviceName(savedName);
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

    const int result = app.exec();
    core.stopListening(); // says goodbye to the nearby list and closes the port
    return result;
}
