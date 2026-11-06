#include "protocol.h"

#include <QJsonParseError>
#include <QJsonDocument>
#include <QtEndian>

namespace Protocol
{

// converting given header into prefix and JSON header
QByteArray encodeFrame(const QJsonObject &header) {

    // converts the header into JSON document and eliminates unnecessary spaces and lines to make it compact
    const QByteArray json = QJsonDocument(header).toJson(QJsonDocument :: Compact);

    QByteArray frame(4, '\0'); // creates a byte array of size 4 all data initialized to 00
    qToBigEndian<quint32>(static_cast<quint32>(json.size()), frame.data()); // adds the value of json.size() into the byte array in big endian fashion
    frame.append(json); // appends the json text into the frame byte array
    return frame;
}
// Ok -> header filling operation done and those filled bytes are removed from the buffer
// NeedMoreData -> header filling operation incomplete and need more data
// Invalid -> invalid header formed; should follow disconnect operation

// The logic goes like this - bear with me for taking too much liness
// when the buffer gets filled with with bytes, out first check is gonna be whether it is >= 4 for prefix (Ok - next step, NeedMoreData - waiting)
// in the next step, we check if the buffer has the value of header size S within 0 < S <= 16 KiB (Ok - next step, Invalid - the header totally is invalid, we disconnect)
// in the last step, if the entire buffer size is greater than header size S + the prefix length which is 4 => buffer.size() >= 4 + header.size() (Ok - validated to next process, NeedMoreData - waiting)

decodeStatus tryDecodeFrame(QByteArray &buffer, QJsonObject &header) {
    if (buffer.size() < 4) {
        return decodeStatus :: NeedMoreData;
    }

    const quint32 length = qFromBigEndian<quint32>(buffer.constData());
    if (length == 0 || length > maxHeaderSize) {
        return decodeStatus :: Invalid;
    }

    if (buffer.size() < static_cast<qsizetype>(length + 4)) {
        return decodeStatus :: NeedMoreData;
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument :: fromJson(buffer.mid(4, length), &error);

    if (error.error != QJsonParseError :: NoError || !document.isObject()) {
        return decodeStatus :: Invalid;
    }

    header = document.object(); // feeding header with the JSON object of document
    buffer.remove(0, length + 4); // removing the used bytes present in the buffer and clearing it
    return decodeStatus :: Ok; // validate the operation with Ok
}

}
