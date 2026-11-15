#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <QJsonObject>
#include <QByteArray>

namespace Protocol {

constexpr int version = 2; // protocol version
constexpr quint32 maxHeaderSize = 16384; // 16 KiB header size (limitting attacks where they pass large headers - prevents excessive memory usage)
constexpr quint16 defaultPort = 45454; // Port number for both the sender and receiver

// How long each phase may take (PROTOCOL.md section 7). Every limit exists so that a stalled or hostile peer
// cannot keep a connection (and its memory) alive forever. A struct, not constants, so tests can use tiny values.
struct Timeouts {
    int helloMs = 10000;            // first message (hello) after the TLS handshake
    int firstPairMessageMs = 60000; // after "pairing_required": the user is typing the PIN, so this is generous
    int pairMessageMs = 5000;       // each pairing message once the exchange has started
    int pairTotalMs = 30000;        // the whole 20-round exchange
    int idleMs = 30000;             // a file is moving but nothing arrives for this long
    int pinLifetimeMs = 120000;     // how long a PIN stays valid (pairing mode)
};

enum class decodeStatus {Ok, NeedMoreData, Invalid};
// Ok -> header filling operation done and those filled bytes are removed from the buffer
// NeedMoreData -> header filling operation incomplete and need more data
// Invalid -> invalid header formed; should follow disconnect operation

// The logic goes like this - bear with me for taking too much lines
// when the buffer gets filled with with bytes, out first check is gonna be whether it is >= 4 for prefix (Ok - next step, NeedMoreData - waiting)
// in the next step, we check if the buffer has the value of header size S within 0 < S <= 16 KiB (Ok - next step, Invalid - the header totally is invalid, we disconnect)
// in the last step, if the entire buffer size is greater than header size S + the prefix length which is 4 => buffer.size() >= 4 + header.size() (Ok - validated to next process, NeedMoreData - waiting)

QByteArray encodeFrame(const QJsonObject &header); // builds the byte array for the prefix and the JSON header
decodeStatus tryDecodeFrame(QByteArray &buffer, QJsonObject &header);
}

#endif // PROTOCOL_H
