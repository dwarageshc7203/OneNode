#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <QJsonObject>
#include <QByteArray>

namespace Protocol {

constexpr int version = 1; // protocol version
constexpr quint32 maxHeaderSize = 16384; // 16 KiB header size (limitting attacks where they pass large headers - prevents excessive memory usage)
constexpr quint16 defaultPort = 45454; // Port number for both the sender and receiver

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
