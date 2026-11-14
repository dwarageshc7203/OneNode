#include "truststore.h"

#include <QtTest>
#include <QTemporaryDir>

class TestTrustStore : public QObject {

    Q_OBJECT

private:

    static QString fp (char c) { return QString(64, QChar(c)); } // a valid fingerprint like "aaaa...a"

    static QByteArray readAll (const QString &path)
    {
        QFile file(path);
        return file.open(QIODevice :: ReadOnly) ? file.readAll() : QByteArray();
    }

    static void writeFile (const QString &path, const QByteArray &data)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice :: WriteOnly | QIODevice :: Truncate));
        file.write(data);
    }

private slots:

    void startsEmptyWhenThereIsNoFile()
    {
        QTemporaryDir dir;
        TrustStore store(dir.path());
        QVERIFY(store.load().isEmpty());
        QVERIFY(store.devices().isEmpty());
        QVERIFY(!store.isTrusted(fp('a')));
    }

    void addedDevicesSurviveARestart()
    {
        QTemporaryDir dir;
        {
            TrustStore store(dir.path());
            QVERIFY(store.load().isEmpty());
            QVERIFY(store.add(fp('a'), "Laptop").isEmpty());
            QVERIFY(store.add(fp('b'), "Phone").isEmpty());
        }
        TrustStore again(dir.path());
        QVERIFY(again.load().isEmpty());
        QCOMPARE(again.devices().size(), 2);
        QVERIFY(again.isTrusted(fp('a')));
        QCOMPARE(again.nameOf(fp('b')), QString("Phone"));
        QVERIFY(!again.isTrusted(fp('c')));
    }

    void addingTheSameDeviceTwiceKeepsOneEntryAndTheFirstPairingTime()
    {
        QTemporaryDir dir;
        TrustStore store(dir.path());
        QVERIFY(store.load().isEmpty());
        QVERIFY(store.add(fp('a'), "Old name").isEmpty());
        const QString firstTime = store.devices().first().pairedAt;

        QVERIFY(store.add(fp('a'), "New name").isEmpty());
        QCOMPARE(store.devices().size(), 1);
        QCOMPARE(store.nameOf(fp('a')), QString("New name"));
        QCOMPARE(store.devices().first().pairedAt, firstTime);
    }

    void removeForgetsADeviceForGood()
    {
        QTemporaryDir dir;
        {
            TrustStore store(dir.path());
            QVERIFY(store.load().isEmpty());
            QVERIFY(store.add(fp('a'), "Laptop").isEmpty());
            QVERIFY(store.add(fp('b'), "Phone").isEmpty());
            QVERIFY(store.remove(fp('a')).isEmpty());
            QVERIFY(!store.isTrusted(fp('a')));
            QVERIFY(store.remove(fp('c')).isEmpty()); // someone who was never there: not an error
        }
        TrustStore again(dir.path());
        QVERIFY(again.load().isEmpty());
        QCOMPARE(again.devices().size(), 1);
        QVERIFY(again.isTrusted(fp('b')));
    }

    void fingerprintsAreMatchedIgnoringCase()
    {
        QTemporaryDir dir;
        TrustStore store(dir.path());
        QVERIFY(store.load().isEmpty());
        QVERIFY(store.add(fp('a').toUpper(), "Laptop").isEmpty()); // stored as lowercase
        QVERIFY(store.isTrusted(fp('a')));
        QVERIFY(store.isTrusted(fp('a').toUpper()));
    }

    void badFingerprintsAreRefused_data()
    {
        QTest :: addColumn<QString>("fingerprint");
        QTest :: newRow("empty")      << QString();
        QTest :: newRow("too short")  << QString(63, 'a');
        QTest :: newRow("too long")   << QString(65, 'a');
        QTest :: newRow("not hex")    << QString(64, 'g');
        QTest :: newRow("with space") << (QString(63, 'a') + " ");
    }

    void badFingerprintsAreRefused()
    {
        QFETCH(QString, fingerprint);
        QTemporaryDir dir;
        TrustStore store(dir.path());
        QVERIFY(store.load().isEmpty());
        QVERIFY(!store.add(fingerprint, "X").isEmpty());
        QVERIFY(store.devices().isEmpty());
        QVERIFY(!QFile :: exists(dir.filePath("trusted_devices.json"))); // nothing was written
    }

    void cleanNameCleansUntrustedText_data()
    {
        QTest :: addColumn<QString>("input");
        QTest :: addColumn<QString>("expected");
        QTest :: newRow("normal")        << "Dwarag's phone" << "Dwarag's phone";
        QTest :: newRow("empty")         << "" << "Unknown device";
        QTest :: newRow("only spaces")   << "   " << "Unknown device";
        QTest :: newRow("control chars") << QString("Lap\ttop\n\x01\x1b[31m") << "Lap top [31m";
        QTest :: newRow("spaces")        << "  a    b  " << "a b";
        QTest :: newRow("too long")      << QString(100, 'x') << QString(64, 'x');
        QTest :: newRow("only controls") << QString("\x01\x02\x03") << "Unknown device";
    }

    void cleanNameCleansUntrustedText()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(TrustStore :: cleanName(input), expected);
    }

    void aDamagedFileIsAnErrorAndIsNeverOverwritten_data()
    {
        QTest :: addColumn<QByteArray>("content");
        QTest :: newRow("not json")        << QByteArray("this is not json");
        QTest :: newRow("json but a list") << QByteArray("[1,2,3]");
        QTest :: newRow("other version")   << QByteArray("{\"schemaVersion\":2,\"devices\":[]}");
        QTest :: newRow("no version")      << QByteArray("{\"devices\":[]}");
        QTest :: newRow("bad fingerprint") << QByteArray("{\"schemaVersion\":1,\"devices\":[{\"fingerprint\":\"zzz\",\"name\":\"x\",\"pairedAt\":\"\"}]}");
    }

    void aDamagedFileIsAnErrorAndIsNeverOverwritten()
    {
        QFETCH(QByteArray, content);
        QTemporaryDir dir;
        const QString path = dir.filePath("trusted_devices.json");
        writeFile(path, content);

        TrustStore store(dir.path());
        QVERIFY(!store.load().isEmpty());
        QVERIFY(store.devices().isEmpty());
        QVERIFY(!store.isTrusted(fp('a')));

        // read-only now: adding or removing is refused, and the file is exactly as it was
        QVERIFY(!store.add(fp('a'), "Laptop").isEmpty());
        QVERIFY(!store.remove(fp('a')).isEmpty());
        QCOMPARE(readAll(path), content);
    }

    void fileLooksAsDocumentedAndLeavesNoTemporaryFiles()
    {
        QTemporaryDir dir;
        TrustStore store(dir.path());
        QVERIFY(store.load().isEmpty());
        QVERIFY(store.add(fp('a'), "Laptop").isEmpty());

        const QJsonObject root = QJsonDocument :: fromJson(readAll(dir.filePath("trusted_devices.json"))).object();
        QCOMPARE(root.value("schemaVersion").toInt(), 1);
        const QJsonObject entry = root.value("devices").toArray().first().toObject();
        QCOMPARE(entry.value("fingerprint").toString(), fp('a'));
        QCOMPARE(entry.value("name").toString(), QString("Laptop"));
        QVERIFY(QDateTime :: fromString(entry.value("pairedAt").toString(), Qt :: ISODate).isValid());

        QCOMPARE(QDir(dir.path()).entryList(QDir :: Files).size(), 1); // only trusted_devices.json
    }
};

QTEST_GUILESS_MAIN(TestTrustStore)
#include "test_truststore.moc"
