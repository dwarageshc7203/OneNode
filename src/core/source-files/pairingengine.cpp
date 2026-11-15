#include "pairingengine.h"
#include "protocol.h"

#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QRegularExpression>

namespace {

QByteArray randomBytes (int n)
{
    QByteArray bytes(n, '\0');
    // system() asks the operating system's secure random source (not a seeded, predictable generator)
    QRandomGenerator :: system() -> fillRange(reinterpret_cast<quint32 *>(bytes.data()), n / 4);
    return bytes;
}

bool isHex (const QString &text, int length)
{
    static const QRegularExpression hex("^[0-9a-f]+$");
    return text.size() == length && hex.match(text).hasMatch();
}

} // namespace

PairingEngine :: PairingEngine (Role role, int pin, const QByteArray &senderFingerprint, const QByteArray &receiverFingerprint,
                                std::function<QByteArray(int)> nonceSource)
    : role(role), pin(pin), fpSender(senderFingerprint), fpReceiver(receiverFingerprint), nonceSource(std :: move(nonceSource))
{
    // a PIN outside 0..999999 or a fingerprint that is not 32 bytes can never work: start failed
    if (pin < 0 || pin > maxPin || fpSender.size() != 32 || fpReceiver.size() != 32) {
        failed = true;
    }
}

PairingEngine :: Step PairingEngine :: current() const
{
    // The order of the 4 steps inside every round, per role (the table from the class comment in the header):
    static const Step senderOrder[4]   = { Step :: Commit, Step :: OtherCommit, Step :: Open, Step :: OtherOpen };
    static const Step receiverOrder[4] = { Step :: OtherCommit, Step :: Commit, Step :: OtherOpen, Step :: Open };
    return (role == Role :: Sender ? senderOrder : receiverOrder)[index];
}

PairingEngine :: Action PairingEngine :: action() const
{
    if (failed) {
        return Action :: Failed;
    }
    if (round >= rounds) {
        return Action :: Done;
    }
    switch (current()) {
    case Step :: Commit:      return Action :: SendCommit;
    case Step :: OtherCommit: return Action :: ExpectCommit;
    case Step :: Open:        return Action :: SendOpen;
    case Step :: OtherOpen:   return Action :: ExpectOpen;
    }
    return Action :: Failed;
}

// bit i of the PIN, most significant of the 20 bits first
int PairingEngine :: myBit() const
{
    return (pin >> (rounds - 1 - round)) & 1;
}

// SHA-256( "OneNode-pair-v2" || role || fpS || fpR || round(1 byte) || nonce(16) || bit(1 byte) )
QByteArray PairingEngine :: commitFor (bool fromSender, const QByteArray &nonce, int bit) const
{
    QCryptographicHash hash(QCryptographicHash :: Sha256);
    hash.addData(QByteArrayView("OneNode-pair-v2"));
    hash.addData(QByteArrayView(fromSender ? "S" : "R", 1));
    hash.addData(fpSender);
    hash.addData(fpReceiver);
    const char roundByte = static_cast<char>(round);
    hash.addData(QByteArrayView(&roundByte, 1));
    hash.addData(nonce);
    const char bitByte = static_cast<char>(bit);
    hash.addData(QByteArrayView(&bitByte, 1));
    return hash.result();
}

bool PairingEngine :: fail()
{
    failed = true;
    return false;
}

void PairingEngine :: advance()
{
    if (++index == 4) { // all 4 steps of this round are done: next round, back to the first step
        index = 0;
        ++round;
    }
}

QJsonObject PairingEngine :: makeMessage()
{
    QJsonObject message;
    if (failed || round >= rounds) {
        return message;
    }

    message["v"] = Protocol :: version;
    message["round"] = round;

    const Step step = current();
    if (step == Step :: Commit) {
        myNonce = nonceSource ? nonceSource(round) : randomBytes(16);
        message["type"] = "pair_commit";
        message["commit"] = QString :: fromLatin1(commitFor(role == Role :: Sender, myNonce, myBit()).toHex());
        advance();
    } else if (step == Step :: Open) {
        message["type"] = "pair_open";
        message["nonce"] = QString :: fromLatin1(myNonce.toHex());
        advance();
    } else {
        return QJsonObject(); // called at the wrong moment
    }

    return message;
}

bool PairingEngine :: accept (const QJsonObject &message)
{
    if (failed || round >= rounds) {
        return false;
    }
    if (message.value("v").toInt(-1) != Protocol :: version) {
        return fail();
    }
    if (message.value("round").toInt(-1) != round || !message.value("round").isDouble()) {
        return fail(); // wrong or repeated round number
    }

    const Step step = current();
    if (step == Step :: OtherCommit) {
        const QString text = message.value("commit").toString();
        if (message.value("type").toString() != "pair_commit" || !isHex(text, 64)) {
            return fail();
        }
        otherCommit = QByteArray :: fromHex(text.toLatin1());
        advance();
        return true;
    }

    if (step == Step :: OtherOpen) {
        const QString text = message.value("nonce").toString();
        if (message.value("type").toString() != "pair_open" || !isHex(text, 32)) {
            return fail();
        }
        const QByteArray nonce = QByteArray :: fromHex(text.toLatin1());

        // The check that makes the whole thing work: rebuild the other side's commitment from the nonce it just revealed,
        // using OUR OWN bit of the PIN and OUR OWN view of both fingerprints. It matches only if the other side
        // used the same bit and saw the same two devices.
        const bool otherIsSender = (role == Role :: Receiver);
        if (commitFor(otherIsSender, nonce, myBit()) != otherCommit) {
            return fail();
        }
        advance();
        return true;
    }

    return fail(); // a message arrived while we should be sending
}

// ---- PIN helpers ----

int PairingEngine :: generatePin()
{
    return static_cast<int>(QRandomGenerator :: system() -> bounded(maxPin + 1)); // bounded() is uniform: no "modulo bias"
}

QString PairingEngine :: formatPin (int pin)
{
    const QString digits = QString("%1").arg(pin, 6, 10, QChar('0'));
    return digits.left(3) + " " + digits.mid(3);
}

bool PairingEngine :: parsePin (const QString &text, int &pin)
{
    QString digits = text;
    digits.remove(' ');
    static const QRegularExpression sixDigits("^[0-9]{6}$");
    if (!sixDigits.match(digits).hasMatch()) {
        return false;
    }
    pin = digits.toInt();
    return true;
}

QByteArray PairingEngine :: fingerprintBytes (const QString &hex)
{
    if (!isHex(hex.toLower(), 64)) {
        return QByteArray();
    }
    return QByteArray :: fromHex(hex.toLatin1());
}
