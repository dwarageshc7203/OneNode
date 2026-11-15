#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <functional>

// The 20-round passkey exchange of PROTOCOL.md section 5, as a pure state machine.
//
// Analogy: a referee who knows the rules of the envelope game but never touches the network.
// The caller (the session) asks "what is next?", sends or receives the message, and tells the
// referee. Because it has no sockets, every rule can be tested exactly, including the test vectors.
//
// Order inside round i (never changes):
//   sender   : SendCommit  -> ExpectCommit -> SendOpen  -> ExpectOpen
//   receiver : ExpectCommit -> SendCommit  -> ExpectOpen -> SendOpen
class PairingEngine {

public:

    enum class Role { Sender, Receiver };

    enum class Action {
        SendCommit,    // call makeMessage() and send it
        SendOpen,      // call makeMessage() and send it
        ExpectCommit,  // wait for the other side's pair_commit, give it to accept()
        ExpectOpen,    // wait for the other side's pair_open, give it to accept()
        Done,          // all 20 rounds passed: the other side knew the PIN
        Failed         // something was wrong: stop and close the connection
    };

    static constexpr int rounds = 20;           // 2^20 = 1 048 576 > 1 000 000 possible PINs
    static constexpr int maxPin = 999999;

    // fingerprints are the 32 RAW bytes (not hex) of each device's certificate hash, as THIS device saw them in the handshake.
    // nonceSource is only for tests (fixed nonces for the test vectors); by default nonces are random.
    PairingEngine (Role role, int pin, const QByteArray &senderFingerprint, const QByteArray &receiverFingerprint,
                   std::function<QByteArray(int round)> nonceSource = nullptr);

    Action action() const;

    // valid when action() is SendCommit or SendOpen; moves the referee to the next step
    QJsonObject makeMessage();

    // valid when action() is ExpectCommit or ExpectOpen. Returns false (and the engine is Failed) when the
    // message is wrong in any way: wrong type, wrong round, bad format, or a commitment that does not match our PIN.
    bool accept (const QJsonObject &message);

    // PIN helpers (shared by the receiver that makes the PIN and the sender that types it)
    static int generatePin();                              // uniform 0..999999 from a secure random source
    static QString formatPin (int pin);                    // 482913 -> "482 913"
    static bool parsePin (const QString &text, int &pin);  // "482913" or "482 913" -> true; anything else false

    static QByteArray fingerprintBytes (const QString &hex); // 64 hex characters -> 32 bytes (empty if invalid)

private:

    enum class Step { Commit, OtherCommit, Open, OtherOpen };

    Role role;
    int pin;
    QByteArray fpSender;
    QByteArray fpReceiver;
    std::function<QByteArray(int)> nonceSource;

    int round = 0;           // 0..19, then Done
    int index = 0;           // which of the 4 steps of the round we are at (see steps below)
    bool failed = false;
    QByteArray myNonce;      // this round
    QByteArray otherCommit;  // the other side's commitment this round (raw 32 bytes)

    int myBit() const;
    QByteArray commitFor (bool fromSender, const QByteArray &nonce, int bit) const;
    Step current() const;    // the step we are at, from the fixed order of our role
    void advance();          // next step; after the 4th one the next round begins
    bool fail();
};
