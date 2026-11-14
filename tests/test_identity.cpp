#include "identity.h"

#include <QtTest>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>

class TestIdentity : public QObject {

    Q_OBJECT

private slots:

    void firstStartCreatesBothFiles()
    {
        QTemporaryDir dir;
        QSslCertificate cert; QSslKey key; QString error;

        QVERIFY2(Identity :: loadOrCreate(dir.path(), cert, key, error), qPrintable(error));
        QVERIFY(QFile :: exists(dir.filePath("identity.key")));
        QVERIFY(QFile :: exists(dir.filePath("identity.crt")));
        QVERIFY(!cert.isNull());
        QVERIFY(!key.isNull());
        QCOMPARE(key.algorithm(), QSsl :: Ec);
    }

    void fingerprintIs64LowercaseHex()
    {
        QTemporaryDir dir;
        QSslCertificate cert; QSslKey key; QString error;
        QVERIFY(Identity :: loadOrCreate(dir.path(), cert, key, error));

        const QString fp = Identity :: fingerprint(cert);
        QVERIFY2(QRegularExpression("^[0-9a-f]{64}$").match(fp).hasMatch(), qPrintable(fp));
    }

    void secondStartKeepsTheSameIdentity()
    {
        QTemporaryDir dir;
        QSslCertificate cert1, cert2; QSslKey key1, key2; QString error;

        QVERIFY(Identity :: loadOrCreate(dir.path(), cert1, key1, error));
        QVERIFY(Identity :: loadOrCreate(dir.path(), cert2, key2, error));
        QCOMPARE(Identity :: fingerprint(cert1), Identity :: fingerprint(cert2));
    }

    void twoDevicesGetDifferentFingerprints()
    {
        QTemporaryDir a, b;
        QSslCertificate certA, certB; QSslKey keyA, keyB; QString error;

        QVERIFY(Identity :: loadOrCreate(a.path(), certA, keyA, error));
        QVERIFY(Identity :: loadOrCreate(b.path(), certB, keyB, error));
        QVERIFY(Identity :: fingerprint(certA) != Identity :: fingerprint(certB));
    }

    void keyFileIsOwnerOnly()
    {
#ifdef Q_OS_UNIX
        QTemporaryDir dir;
        QSslCertificate cert; QSslKey key; QString error;
        QVERIFY(Identity :: loadOrCreate(dir.path(), cert, key, error));

        const auto perms = QFile :: permissions(dir.filePath("identity.key"));
        QVERIFY(perms & QFileDevice :: ReadOwner);
        QVERIFY(!(perms & (QFileDevice :: ReadGroup | QFileDevice :: ReadOther
                           | QFileDevice :: WriteGroup | QFileDevice :: WriteOther)));
#else
        QSKIP("owner-only permissions are checked on Linux/macOS only");
#endif
    }

    void missingHalfIsAnErrorNotANewIdentity()
    {
        QTemporaryDir dir;
        QSslCertificate cert; QSslKey key; QString error;
        QVERIFY(Identity :: loadOrCreate(dir.path(), cert, key, error));
        const QString before = Identity :: fingerprint(cert);

        QVERIFY(QFile :: remove(dir.filePath("identity.key")));

        QSslCertificate cert2; QSslKey key2;
        QVERIFY(!Identity :: loadOrCreate(dir.path(), cert2, key2, error));
        QVERIFY(!error.isEmpty());
        // and the certificate that is left was not touched
        QVERIFY(QFile :: exists(dir.filePath("identity.crt")));
        QVERIFY(!before.isEmpty());
    }

    void damagedFileIsAnErrorNotANewIdentity()
    {
        QTemporaryDir dir;
        QSslCertificate cert; QSslKey key; QString error;
        QVERIFY(Identity :: loadOrCreate(dir.path(), cert, key, error));

        QFile f(dir.filePath("identity.crt"));
        QVERIFY(f.open(QIODevice :: WriteOnly | QIODevice :: Truncate));
        f.write("not a certificate");
        f.close();

        QSslCertificate cert2; QSslKey key2;
        QVERIFY(!Identity :: loadOrCreate(dir.path(), cert2, key2, error));
        QVERIFY(!error.isEmpty());
    }
};

QTEST_MAIN(TestIdentity)
#include "test_identity.moc"
