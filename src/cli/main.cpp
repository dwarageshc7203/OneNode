#include <QCoreApplication>
#include <QCommandLineParser>
#include <QTextStream>

#include "corebridge.h"
#include "protocol.h"

// Command line front end for the OneNode core. No QML anywhere in here: it only uses CoreBridge's signals.
//   onenode-cli send <host> <file> [--port N]
//   onenode-cli receive [--port N] [--dest DIR]

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication :: setApplicationName("onenode-cli");

    QTextStream out(stdout);
    QTextStream err(stderr);

    QCommandLineParser parser;
    parser.setApplicationDescription("OneNode command line: send a file to another device, or receive files.");
    parser.addHelpOption();
    parser.addPositionalArgument("command", "send <host> <file>  |  receive");

    const QCommandLineOption portOption({"p", "port"}, "TCP port (default " + QString :: number(Protocol :: defaultPort) + ").", "port", QString :: number(Protocol :: defaultPort));
    const QCommandLineOption destOption({"d", "dest"}, "Folder to save received files in (receive only).", "dir");
    parser.addOption(portOption);
    parser.addOption(destOption);
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

    CoreBridge bridge;

    // progress is already throttled inside the sessions, so printing every event is fine
    QObject :: connect(&bridge, &CoreBridge :: transferProgress, [&out] (const QString &name, qint64 done, qint64 total) {
        const int percent = total > 0 ? static_cast<int>(done * 100 / total) : 100;
        out << "\r" << name << "  " << percent << "%  (" << done << " / " << total << " bytes)" << Qt :: flush;
    });

    if (args.first() == "send") {
        if (args.size() != 3) {
            err << "Usage: onenode-cli send <host> <file> [--port N]\n";
            return 2;
        }

        QObject :: connect(&bridge, &CoreBridge :: transferFinished, [&out] (const QString &name) {
            out << "\nSent: " << name << "\n" << Qt :: flush;
            QCoreApplication :: exit(0);
        });
        QObject :: connect(&bridge, &CoreBridge :: transferFailed, [&err] (const QString &name, const QString &reason) {
            err << "\nFailed: " << name << " | " << reason << "\n" << Qt :: flush;
            QCoreApplication :: exit(1);
        });

        bridge.sendFile(args.at(1), args.at(2), port);
        return app.exec();
    }

    if (args.first() == "receive") {
        if (parser.isSet(destOption)) {
            bridge.setReceiveDir(parser.value(destOption));
        }

        QObject :: connect(&bridge, &CoreBridge :: transferFinished, [&out] (const QString &name) {
            out << "\nReceived: " << name << "\n" << Qt :: flush;
        });
        QObject :: connect(&bridge, &CoreBridge :: transferFailed, [&err] (const QString &name, const QString &reason) {
            err << "\nFailed: " << name << " | " << reason << "\n" << Qt :: flush;
        });

        if (!bridge.startListening(port)) {
            err << "Could not listen on port " << port << " (already in use?)\n";
            return 1;
        }

        out << "Listening on port " << port << ", saving to " << bridge.receiveFolder() << "\nPress Ctrl+C to stop.\n" << Qt :: flush;
        return app.exec();
    }

    err << "Unknown command: " << args.first() << "\n" << parser.helpText() << "\n";
    return 2;
}
