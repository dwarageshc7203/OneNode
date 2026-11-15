#include "pairingmode.h"
#include "pairingengine.h"

PairingMode :: PairingMode (const Protocol :: Timeouts &timeouts, QObject *parent) : QObject(parent), timeouts(timeouts)
{
    lifetime.setSingleShot(true);
    connect(&lifetime, &QTimer :: timeout, this, [this] { end("expired"); });
}

void PairingMode :: start()
{
    busy = false;
    exchangeBegan = false;
    currentPin = PairingEngine :: generatePin(); // a NEW pin every time, never reused
    isActive = true;
    lifetime.start(timeouts.pinLifetimeMs);
    emit started(currentPin);
}

void PairingMode :: cancel()
{
    if (isActive) {
        end("cancelled");
    }
}

void PairingMode :: end (const QString &reason)
{
    lifetime.stop();
    isActive = false;
    busy = false;
    exchangeBegan = false;
    currentPin = -1; // the PIN is gone: it exists nowhere any more
    emit ended(reason);
}

bool PairingMode :: beginAttempt()
{
    if (!isActive || busy) {
        return false;
    }
    busy = true;
    exchangeBegan = false;
    return true;
}

void PairingMode :: attemptStartedExchange()
{
    if (isActive && busy) {
        exchangeBegan = true;
    }
}

void PairingMode :: attemptAbandoned()
{
    if (isActive && busy && !exchangeBegan) {
        busy = false;
    } else if (isActive && busy && exchangeBegan) {
        attemptFailed(); // left in the middle of the exchange = failed
    }
}

void PairingMode :: attemptFailed()
{
    if (isActive && busy) {
        end("failed"); // single use: a failed exchange burns the PIN (see PROTOCOL.md 5.1 for why)
    }
}

void PairingMode :: attemptSucceeded (const QString &fingerprint, const QString &name)
{
    if (isActive && busy) {
        end("paired");
        emit paired(fingerprint, name);
    }
}
