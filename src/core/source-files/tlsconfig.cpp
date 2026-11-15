#include "tlsconfig.h"

#include <QSslSocket>

QString TlsConfig :: selectBackend()
{
    if (!QSslSocket :: supportsSsl()) {
        return "Secure connections are not available: OneNode could not find the OpenSSL libraries it needs. Reinstall OneNode.";
    }

    if (!QSslSocket :: availableBackends().contains("openssl")) {
        return "Secure connections are not available: this build of Qt has no OpenSSL support. Reinstall OneNode.";
    }

    if (QSslSocket :: activeBackend() != "openssl" && !QSslSocket :: setActiveBackend("openssl")) {
        return "Secure connections are not available: OneNode could not switch on the OpenSSL backend. Reinstall OneNode.";
    }

    if (!QSslSocket :: isProtocolSupported(QSsl :: TlsV1_3)) {
        return "Secure connections are not available: the OpenSSL on this computer is too old (TLS 1.3 is required).";
    }

    return QString();
}

QSslConfiguration TlsConfig :: make (const QSslCertificate &cert, const QSslKey &key)
{
    QSslConfiguration config = QSslConfiguration :: defaultConfiguration();

    config.setProtocol(QSsl :: TlsV1_3);         // exactly 1.3, no older fallback
    config.setLocalCertificate(cert);            // our ID card
    config.setPrivateKey(key);                   // proof it is ours

    // VerifyPeer on a server means "the client MUST show a certificate".
    // On a client it means "check the server's certificate", which for a self-signed
    // card produces an error we deliberately handle ourselves (see below).
    config.setPeerVerifyMode(QSslSocket :: VerifyPeer);

    // We do NOT use certificate authorities: trust is by fingerprint (PROTOCOL.md section 1).
    // So the usual "is this card signed by a known authority?" check is replaced by OUR check
    // against the trusted-devices list, done after the handshake. That is why callers
    // ignore the self-signed errors and then compare the fingerprint themselves.
    return config;
}

void TlsConfig :: acceptSelfSigned (QSslSocket *socket, const QList<QSslError> &errors)
{
    // Trust in OneNode is decided by the certificate's fingerprint after the handshake (PROTOCOL.md section 1).
    // The usual checks on the certificate (who signed it, expiry date, host name, purpose flags) are therefore
    // ignored on purpose. Which of those complaints a system reports differs from one OpenSSL build to the next
    // (Windows with a newer OpenSSL reports more than Linux does), so listing the "expected" ones breaks on some systems.
    // The one thing we never forgive is a missing certificate: both sides must show one.
    // This does NOT skip the proof that the peer owns the key: that is part of the TLS handshake itself.
    QList<QSslError> forgiven;
    for (const QSslError &error : errors) {
        if (error.error() != QSslError :: NoPeerCertificate) {
            forgiven.append(error);
        }
    }
    socket->ignoreSslErrors(forgiven);
}
