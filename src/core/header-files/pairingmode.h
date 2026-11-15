#pragma once

#include "protocol.h"

#include <QObject>
#include <QString>
#include <QTimer>

// "Pairing mode" of the receiver (PROTOCOL.md section 5.1): the short period in which this device shows
// a PIN and accepts exactly one new device.
//
// Analogy: opening the door for ONE expected guest, with a code word. The door closes again when
// the guest got in, when the guest failed, when you change your mind, or when two minutes pass.
// Only the person at this device can open it (start()); nothing from the network can.
class PairingMode : public QObject {

    Q_OBJECT

public:
    explicit PairingMode (const Protocol :: Timeouts &timeouts = Protocol :: Timeouts(), QObject *parent = nullptr);

    void start();   // creates a fresh random PIN and opens the door (restarting if it was already open)
    void cancel();  // the user changed their mind

    bool active() const { return isActive; }
    int pin() const { return currentPin; }

    // --- used by ReceiveSession while a sender is pairing ---
    bool beginAttempt();                       // a sender wants to pair. false: door closed, or someone else is already in.
    void attemptStartedExchange();             // round 0 has begun: from now on a failure burns the PIN
    void attemptAbandoned();                   // the sender left before round 0: just free the place, the PIN lives on
    void attemptFailed();                      // the exchange began and did not succeed: the PIN is burnt, the door closes
    void attemptSucceeded (const QString &fingerprint, const QString &name); // door closes, the device is now paired

signals:
    void started (int pin);                    // show this PIN to the user
    void ended (const QString &reason);        // "paired", "failed", "cancelled" or "expired"
    void paired (const QString &fingerprint, const QString &name);

private:
    Protocol :: Timeouts timeouts;
    QTimer lifetime;
    bool isActive = false;
    bool busy = false;       // one sender is at the door
    bool exchangeBegan = false;
    int currentPin = -1;

    void end (const QString &reason);
};
