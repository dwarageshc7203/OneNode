#pragma once

#include <QString>
#include <QSslCertificate>
#include <QSslKey>

// A device's identity = its "ID card" (certificate) + the secret that proves
// the card is really yours (private key).
//
// Analogy: the certificate is your passport photo page, anyone may look at it.
// The private key is your signature, only you can produce it.
// The fingerprint is the passport number: a short, stable name for the card.
//
// The core never knows QML. This class only knows files and TLS objects.
class Identity {

public:

    // Looks in `folder` for identity.key and identity.crt.
    //  - both exist and are valid  -> loads them (same device, same fingerprint)
    //  - neither exists            -> creates a new pair (first start)
    //  - only one exists / broken  -> returns false and fills `error`.
    //    We never silently make a new identity: that would make every
    //    paired device stop recognising us without anybody knowing why.
    static bool loadOrCreate (const QString &folder, QSslCertificate &cert, QSslKey &key, QString &error);

    // SHA-256 of the certificate (its DER bytes) as 64 lowercase hex characters.
    static QString fingerprint (const QSslCertificate &cert);

private:

    // Makes a fresh key + self-signed certificate with OpenSSL and returns both as PEM text.
    static bool generate (QByteArray &keyPem, QByteArray &certPem, QString &error);
};
