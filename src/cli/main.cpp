#include <QCoreApplication>
#include <QCommandLineParser>
#include <QTextStream>
#include <QSslSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QVariantMap>

#include "corebridge.h"
#include "protocol.h"
#include "tlsconfig.h"

// Command line front end for the OneNode core. No QML anywhere in here: it only uses CoreBridge's signals.
//   onenode-cli send <host> <file> [--port N]
//   onenode-cli receive [--port N] [--dest DIR] [--pair]
//   onenode-cli nearby [seconds]
//   onenode-cli selftest

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication :: setApplicationName("onenode-cli");

    QTextStream out(stdout);
    QTextStream err(stderr);

    QCommandLineParser parser;
    parser.setApplicationDescription("OneNode command line: send a file to another device, or receive files.");
    parser.addHelpOption();
    parser.addPositionalArgument("command", "send <host> <file>  |  receive  |  nearby [seconds]  |  selftest");

    const QCommandLineOption portOption({"p", "port"}, "TCP port (default " + QString :: number(Protocol :: defaultPort) + ").", "port", QString :: number(Protocol :: defaultPort));
    const QCommandLineOption destOption({"d", "dest"}, "Folder to save received files in (receive only).", "dir");
    parser.addOption(portOption);
    parser.addOption(destOption);
    const QCommandLineOption pairOption("pair", "Open pairing mode at start and show the PIN (receive only).");
    parser.addOption(pairOption);
    parser.process(app);

    const QStringList args = parser.positionalArguments();

    if (args.isEmpty()) {
        err << parser.helpText() << "\n";
        return 2;
    }

    bool portOk = false;
    const uint portValue = parser.value(portOption).toUInt(&portOk);
    if (!portOk || portValue == 0 || portValue > 65535) {
        err << "Invalid port: " << parser.value(portOption) << "\n";
        return 2;
    }
    const quint16 port = static_cast<quint16>(portValue);

    // make TLS identical on every operating system (see tlsconfig.h); must happen before any socket exists
    const QString tlsProblem = TlsConfig :: selectBackend();
    if (!tlsProblem.isEmpty()) {
        err << tlsProblem << "\n";
        return 1;
    }

    // "selftest": checks the things a broken package would lack (TLS backend, OpenSSL libraries, creating an identity)
    if (args.first() == "selftest") {
        QTemporaryDir scratch;
        CoreBridge probe;
        const QString problem = probe.loadIdentity(scratch.path());
        if (!problem.isEmpty()) {
            err << "SELFTEST FAILED: " << problem << "\n";
            return 3;
        }
        out << "SELFTEST OK: TLS backend " << QSslSocket :: activeBackend() << ", " << QSslSocket :: sslLibraryVersionString() << "\n";
        return 0;
    }

    CoreBridge bridge;

    // every device needs its identity (certificate + key) before it can talk TLS; it is created on the first start
    const QString identityError = bridge.loadIdentity(CoreBridge :: defaultDataFolder());
    if (!identityError.isEmpty()) {
        err << identityError << "\n";
        return 1;
    }

    QObject :: connect(&bridge, &CoreBridge :: transferStarted, [&out] (quint64 id, const QString &name, CoreBridge :: Direction direction, qint64 total) {
        out << (direction == CoreBridge :: Direction :: Send ? "Sending" : "Receiving") << " #" << id << ": " << name << " (" << total << " bytes)\n" << Qt :: flush;
    });

    // progress is already throttled inside the sessions, so printing every event is fine
    QObject :: connect(&bridge, &CoreBridge :: transferProgress, [&out] (quint64, const QString &name, qint64 done, qint64 total) {
        const int percent = total > 0 ? static_cast<int>(done * 100 / total) : 100;
        out << "\r" << name << "  " << percent << "%  (" << done << " / " << total << " bytes)" << Qt :: flush;
    });

    // paired devices are announced on both sides
    QObject :: connect(&bridge, &CoreBridge :: devicePaired, [&out] (const QString &name, const QString &fingerprint) {
        out << "\nPaired with \"" << name << "\" (" << fingerprint.left(16) << "...)\n" << Qt :: flush;
    });

    if (args.first() == "send") {
        if (args.size() != 3) {
            err << "Usage: onenode-cli send <host> <file> [--port N]\n";
            return 2;
        }

        QObject :: connect(&bridge, &CoreBridge :: transferFinished, [&out] (quint64, const QString &name) {
            out << "\nSent: " << name << "\n" << Qt :: flush;
            QCoreApplication :: exit(0);
        });
        QObject :: connect(&bridge, &CoreBridge :: transferFailed, [&err] (quint64, const QString &name, const QString &reason) {
            err << "\nFailed: " << name << " | " << reason << "\n" << Qt :: flush;
            QCoreApplication :: exit(1);
        });

        // first contact: the other device shows a 6-digit PIN, the user types it here
        QObject :: connect(&bridge, &CoreBridge :: pinNeeded, [&bridge, &out] (quint64 id, const QString &peerName) {
            QTextStream in(stdin);
            while (true) {
                out << "\nNot paired yet. Type the 6-digit PIN shown on " << peerName << ": " << Qt :: flush;
                const QString answer = in.readLine();
                if (answer.isNull()) { // no more input: give up
                    bridge.cancelPin(id);
                    return;
                }
                const QString problem = bridge.submitPin(id, answer.trimmed());
                if (problem.isEmpty()) {
                    return;
                }
                out << problem << "\n";
            }
        });

        bridge.sendFile(args.at(1), args.at(2), port);
        return app.exec();
    }

    if (args.first() == "receive") {
        if (parser.isSet(destOption)) {
            bridge.setReceiveDir(parser.value(destOption));
        }

        QObject :: connect(&bridge, &CoreBridge :: transferFinished, [&out] (quint64, const QString &name) {
            out << "\nReceived: " << name << "\n" << Qt :: flush;
        });
        QObject :: connect(&bridge, &CoreBridge :: transferFailed, [&err] (quint64, const QString &name, const QString &reason) {
            err << "\nFailed: " << name << " | " << reason << "\n" << Qt :: flush;
        });

        if (!bridge.startListening(port)) {
            err << "Could not listen on port " << port << " (already in use?)\n";
            return 1;
        }

        out << "Listening on port " << port << ", saving to " << bridge.receiveFolder() << "\nPress Ctrl+C to stop.\n" << Qt :: flush;

        // let the other devices on this network find us (a problem here is not fatal: the address still works)
        const QString discoveryProblem = bridge.startDiscovery();
        if (!discoveryProblem.isEmpty()) {
            err << "Nearby discovery is off: " << discoveryProblem << "\n" << Qt :: flush;
        }

        if (parser.isSet(pairOption)) {
            QObject :: connect(&bridge, &CoreBridge :: pairingStarted, [&out] (const QString &pin) {
                out << "Pairing mode: PIN " << pin << "\n" << Qt :: flush;
            });
            QObject :: connect(&bridge, &CoreBridge :: pairingEnded, [&out] (const QString &reason) {
                out << "Pairing mode ended: " << reason << "\n" << Qt :: flush;
            });
            bridge.startPairing();
        }
        return app.exec();
    }

    if (args.first() == "nearby") {
        bool secondsOk = true;
        const int seconds = args.size() > 1 ? args.at(1).toInt(&secondsOk) : 4;
        if (!secondsOk || seconds < 1 || seconds > 120) {
            err << "Usage: onenode-cli nearby [seconds 1-120]\n";
            return 2;
        }
        // we listen on any free port only so that we can announce ourselves as well
        if (!bridge.startListening(0)) {
            err << "Could not start.\n";
            return 1;
        }
        const QString problem = bridge.startDiscovery();
        if (!problem.isEmpty()) {
            err << problem << "\n";
            return 1;
        }
        out << "Looking for nearby devices for " << seconds << " seconds ...\n" << Qt :: flush;
        QTimer :: singleShot(seconds * 1000, &app, [&bridge, &out] {
            const QVariantList list = bridge.nearbyDevices();
            if (list.isEmpty()) {
                out << "No other OneNode device found.\n";
            }
            for (const QVariant &item : list) {
                const QVariantMap device = item.toMap();
                out << device["name"].toString() << "  " << device["address"].toString() << ":" << device["port"].toInt()
                    << "  " << device["shortFingerprint"].toString()
                    << (device["trusted"].toBool() ? "  [paired]" : "  [not paired]")
                    << (device["pairing"].toBool() ? "  [pairing mode]" : "") << "\n";
            }
            out << Qt :: flush;
            QCoreApplication :: exit(0);
        });
        return app.exec();
    }

    err << "Unknown command: " << args.first() << "\n" << parser.helpText() << "\n";
    return 2;
}
