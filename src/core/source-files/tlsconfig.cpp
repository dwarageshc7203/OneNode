#include "tlsconfig.h"

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
    QList<QSslError> forgiven;
    for (const QSslError &error : errors) {
        switch (error.error()) {
        case QSslError :: SelfSignedCertificate:
        case QSslError :: SelfSignedCertificateInChain:
        case QSslError :: HostNameMismatch:
            forgiven.append(error);
            break;
        default:
            break; // anything else stays an error
        }
    }
    // ignoreSslErrors(list) forgives only the listed ones; if something else is left over, TLS aborts
    socket->ignoreSslErrors(forgiven);
}
