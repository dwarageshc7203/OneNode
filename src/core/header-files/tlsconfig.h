#pragma once

#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslError>
#include <QSslKey>
#include <QSslSocket>

// The one place that decides HOW OneNode does TLS (PROTOCOL.md section 1).
// Both the sender and the receiver build their settings from here, so they can never disagree.
namespace TlsConfig {

// TLS 1.3 only, we show our own certificate, and we demand the other side's certificate too
// (mutual TLS: both devices show their ID card, not only the server).
QSslConfiguration make (const QSslCertificate &cert, const QSslKey &key);

// Call this from a socket's sslErrors signal (client) or QSslServer::sslErrors (server).
// It forgives ONLY the complaints we expect from self-signed cards:
//   - "self-signed and untrusted"  (we have no authority, trust is by fingerprint)
//   - "host name mismatch"         (we connect by IP address, the card has no address in it)
// Everything else (for example "the peer did not present any certificate") is NOT forgiven,
// so the handshake fails and the connection is dropped.
void acceptSelfSigned (QSslSocket *socket, const QList<QSslError> &errors);

}
