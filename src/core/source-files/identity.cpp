#include "identity.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSaveFile>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/x509.h>

#include <memory>

namespace {

// OpenSSL hands out raw C pointers that must be freed by hand.
// These tiny wrappers free them automatically, even on early return.
// (Same idea as closing a file in a destructor.)
struct KeyFree  { void operator() (EVP_PKEY *p) const { EVP_PKEY_free(p); } };
struct CertFree { void operator() (X509 *p) const { X509_free(p); } };
struct BioFree  { void operator() (BIO *p) const { BIO_free(p); } };

using KeyPtr  = std :: unique_ptr<EVP_PKEY, KeyFree>;
using CertPtr = std :: unique_ptr<X509, CertFree>;
using BioPtr  = std :: unique_ptr<BIO, BioFree>;

// read everything OpenSSL wrote into a memory BIO
QByteArray drain (BIO *bio)
{
    char *data = nullptr;
    const long size = BIO_get_mem_data(bio, &data);
    return QByteArray(data, static_cast<int>(size));
}

} // namespace

bool Identity :: generate (QByteArray &keyPem, QByteArray &certPem, QString &error)
{
    // 1. the key: elliptic curve P-256 (small, fast, universally supported by TLS 1.3)
    KeyPtr key(EVP_EC_gen("P-256"));
    if (!key) {
        error = "Could not create the device key (OpenSSL).";
        return false;
    }

    // 2. the certificate
    CertPtr cert(X509_new());
    if (!cert) {
        error = "Could not create the device certificate (OpenSSL).";
        return false;
    }
    X509_set_version(cert.get(), 2); // 2 means "X.509 version 3"

    // serial number: random, so two devices never share one
    unsigned char serial[16];
    if (RAND_bytes(serial, sizeof(serial)) != 1) {
        error = "Could not get random numbers (OpenSSL).";
        return false;
    }
    serial[0] &= 0x7f; // keep it positive
    BIGNUM *bn = BN_bin2bn(serial, sizeof(serial), nullptr);
    BN_to_ASN1_INTEGER(bn, X509_get_serialNumber(cert.get()));
    BN_free(bn);

    // Dates are required by the format, but OneNode ignores them
    // (trust is by fingerprint, see PROTOCOL.md section 1). Valid for 100 years
    // so a device never has to "renew" and change its fingerprint.
    X509_gmtime_adj(X509_getm_notBefore(cert.get()), -60 * 60 * 24);
    X509_gmtime_adj(X509_getm_notAfter(cert.get()), 60L * 60 * 24 * 365 * 100);

    // name: the same for every device on purpose, so it reveals nothing about the owner
    X509_NAME *name = X509_get_subject_name(cert.get());
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char *>("OneNode device"), -1, -1, 0);
    X509_set_issuer_name(cert.get(), name); // self-signed: issuer == subject

    X509_set_pubkey(cert.get(), key.get());

    // 3. sign the card with our own key ("I vouch for myself")
    if (X509_sign(cert.get(), key.get(), EVP_sha256()) == 0) {
        error = "Could not sign the device certificate (OpenSSL).";
        return false;
    }

    // 4. turn both into PEM text (what goes into the files)
    BioPtr keyBio(BIO_new(BIO_s_mem()));
    BioPtr certBio(BIO_new(BIO_s_mem()));
    if (!keyBio || !certBio
        || PEM_write_bio_PrivateKey(keyBio.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr) != 1
        || PEM_write_bio_X509(certBio.get(), cert.get()) != 1) {
        error = "Could not write the device identity (OpenSSL).";
        return false;
    }
    keyPem = drain(keyBio.get());
    certPem = drain(certBio.get());
    return true;
}

bool Identity :: loadOrCreate (const QString &folder, QSslCertificate &cert, QSslKey &key, QString &error)
{
    QDir().mkpath(folder);
    const QString keyPath = QDir(folder).filePath("identity.key");
    const QString certPath = QDir(folder).filePath("identity.crt");

    const bool haveKey = QFile::exists(keyPath);
    const bool haveCert = QFile::exists(certPath);

    if (!haveKey && !haveCert) {
        // first start: make a new identity
        QByteArray keyPem, certPem;
        if (!generate(keyPem, certPem, error))
            return false;

        // Key first, and owner-only BEFORE the secret is written into it.
        // QSaveFile writes to a temp file and renames, so a crash never leaves half a key.
        for (const auto &item : { std :: make_pair(keyPath, keyPem), std :: make_pair(certPath, certPem) }) {
            QSaveFile file(item.first);
            if (!file.open(QIODevice :: WriteOnly)) {
                error = "Could not save " + item.first + ".";
                return false;
            }
            if (item.first == keyPath)
                file.setPermissions(QFileDevice :: ReadOwner | QFileDevice :: WriteOwner);
            file.write(item.second);
            if (!file.commit()) {
                error = "Could not save " + item.first + ".";
                return false;
            }
        }
    } else if (haveKey != haveCert) {
        error = "Your device identity is incomplete (only one of identity.key / identity.crt exists in "
                + folder + "). Not creating a new one, because paired devices would stop recognising you.";
        return false;
    }

    // load (also right after creating, so what we use is exactly what is on disk)
    QFile keyFile(keyPath), certFile(certPath);
    if (!keyFile.open(QIODevice :: ReadOnly) || !certFile.open(QIODevice :: ReadOnly)) {
        error = "Could not read the device identity files in " + folder + ".";
        return false;
    }
    key = QSslKey(keyFile.readAll(), QSsl :: Ec, QSsl :: Pem, QSsl :: PrivateKey);
    cert = QSslCertificate(certFile.readAll(), QSsl :: Pem);
    if (key.isNull() || cert.isNull()) {
        error = "The device identity files in " + folder + " are damaged. Not replacing them automatically.";
        return false;
    }
    return true;
}

QString Identity :: fingerprint (const QSslCertificate &cert)
{
    return QString :: fromLatin1(cert.digest(QCryptographicHash :: Sha256).toHex());
}
