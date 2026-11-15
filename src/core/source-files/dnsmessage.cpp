#include "dnsmessage.h"

#include <QtEndian>

namespace Dns {

namespace {

// ---------- writing ----------

void put16 (QByteArray &out, quint16 value)
{
    char bytes[2];
    qToBigEndian<quint16>(value, bytes);
    out.append(bytes, 2);
}

void put32 (QByteArray &out, quint32 value)
{
    char bytes[4];
    qToBigEndian<quint32>(value, bytes);
    out.append(bytes, 4);
}

// "a.b.c" -> 1 'a' 1 'b' 1 'c' 0   (no compression: always valid, a few bytes longer)
bool putName (QByteArray &out, const QString &name)
{
    const QByteArray utf8 = name.toUtf8();
    if (utf8.size() > 253) {
        return false;
    }
    if (!utf8.isEmpty()) {
        for (const QByteArray &label : utf8.split('.')) {
            if (label.isEmpty() || label.size() > 63) {
                return false;
            }
            out.append(static_cast<char>(label.size()));
            out.append(label);
        }
    }
    out.append('\0');
    return true;
}

bool putRecord (QByteArray &out, const Record &record)
{
    if (!putName(out, record.name)) {
        return false;
    }
    put16(out, record.type);
    put16(out, static_cast<quint16>(1 | (record.cacheFlush ? 0x8000 : 0))); // class IN
    put32(out, record.ttl);

    QByteArray rdata;
    switch (record.type) {
    case TypePTR:
        if (!putName(rdata, record.target)) {
            return false;
        }
        break;
    case TypeSRV:
        put16(rdata, record.priority);
        put16(rdata, record.weight);
        put16(rdata, record.port);
        if (!putName(rdata, record.target)) {
            return false;
        }
        break;
    case TypeA:
        if (record.address.protocol() != QAbstractSocket :: IPv4Protocol) {
            return false;
        }
        put32(rdata, record.address.toIPv4Address());
        break;
    case TypeAAAA: {
        if (record.address.protocol() != QAbstractSocket :: IPv6Protocol) {
            return false;
        }
        const Q_IPV6ADDR bytes = record.address.toIPv6Address();
        rdata.append(reinterpret_cast<const char *>(bytes.c), 16);
        break;
    }
    case TypeTXT:
        if (record.txt.isEmpty()) {
            rdata.append('\0'); // an empty TXT record is one zero-length string
        }
        for (auto it = record.txt.constBegin(); it != record.txt.constEnd(); ++it) {
            const QByteArray entry = it.key().toUtf8() + (it.value().isNull() ? QByteArray() : "=" + it.value().toUtf8());
            if (entry.isEmpty() || entry.size() > 255) {
                return false;
            }
            rdata.append(static_cast<char>(entry.size()));
            rdata.append(entry);
        }
        break;
    default:
        return false; // we only ever write the five types above
    }

    if (rdata.size() > 65535) {
        return false;
    }
    put16(out, static_cast<quint16>(rdata.size()));
    out.append(rdata);
    return true;
}

// ---------- reading ----------

// A cursor over the packet that refuses to read past the end.
class Reader {
public:
    explicit Reader (const QByteArray &data) : data(data) {}

    int position() const { return pos; }
    void seek (int p) { pos = p; }
    bool has (int n) const { return n >= 0 && pos + n <= data.size(); }

    bool read16 (quint16 &value)
    {
        if (!has(2)) { return false; }
        value = qFromBigEndian<quint16>(data.constData() + pos);
        pos += 2;
        return true;
    }

    bool read32 (quint32 &value)
    {
        if (!has(4)) { return false; }
        value = qFromBigEndian<quint32>(data.constData() + pos);
        pos += 4;
        return true;
    }

    // Reads a (possibly compressed) name starting at the cursor and leaves the cursor after it.
    // Compression pointers must point BACKWARDS and are followed at most 16 times: no loops, no tricks.
    bool readName (QString &name) { return readNameAt(pos, name, true); }

    // reads a name inside rdata whose end is known: the name must fit inside [pos, end)
    const QByteArray &bytes() const { return data; }

private:
    const QByteArray &data;
    int pos = 0;

    bool readNameAt (int start, QString &name, bool advance)
    {
        QString result;
        int cursor = start;
        int endAfterFirstJump = -1;
        int jumps = 0;
        int total = 0;

        while (true) {
            if (cursor < 0 || cursor >= data.size()) {
                return false;
            }
            const quint8 length = static_cast<quint8>(data.at(cursor));

            if (length == 0) {
                ++cursor;
                break;
            }
            if ((length & 0xC0) == 0xC0) { // pointer to somewhere else in the packet
                if (cursor + 1 >= data.size()) {
                    return false;
                }
                const int target = ((length & 0x3F) << 8) | static_cast<quint8>(data.at(cursor + 1));
                if (endAfterFirstJump < 0) {
                    endAfterFirstJump = cursor + 2;
                }
                if (target >= cursor || ++jumps > 16) { // only backwards, and not forever
                    return false;
                }
                cursor = target;
                continue;
            }
            if ((length & 0xC0) != 0) { // the two other top-bit patterns are reserved
                return false;
            }
            if (length > 63 || cursor + 1 + length > data.size()) {
                return false;
            }
            total += length + 1;
            if (total > 254) {
                return false;
            }
            if (!result.isEmpty()) {
                result.append('.');
            }
            result.append(QString :: fromUtf8(data.constData() + cursor + 1, length));
            cursor += 1 + length;
        }

        name = result;
        if (advance) {
            pos = endAfterFirstJump >= 0 ? endAfterFirstJump : cursor;
        }
        return true;
    }
};

bool readRecord (Reader &reader, Record &record)
{
    quint16 classField = 0, rdlength = 0;
    if (!reader.readName(record.name) || !reader.read16(record.type) || !reader.read16(classField)
        || !reader.read32(record.ttl) || !reader.read16(rdlength)) {
        return false;
    }
    record.cacheFlush = (classField & 0x8000) != 0;
    if ((classField & 0x7FFF) != 1) { // only class IN
        // not an error: skip records of other classes, but they must still be well formed
        if (!reader.has(rdlength)) { return false; }
        reader.seek(reader.position() + rdlength);
        record.type = 0;
        return true;
    }
    if (!reader.has(rdlength)) {
        return false;
    }
    const int rdataStart = reader.position();
    const int rdataEnd = rdataStart + rdlength;

    switch (record.type) {
    case TypePTR:
        if (!reader.readName(record.target) || reader.position() > rdataEnd) { return false; }
        break;
    case TypeSRV:
        if (rdlength < 7 || !reader.read16(record.priority) || !reader.read16(record.weight) || !reader.read16(record.port)
            || !reader.readName(record.target) || reader.position() > rdataEnd) { return false; }
        break;
    case TypeA:
        if (rdlength != 4) { return false; }
        {
            quint32 address = 0;
            reader.read32(address);
            record.address = QHostAddress(address);
        }
        break;
    case TypeAAAA:
        if (rdlength != 16) { return false; }
        {
            Q_IPV6ADDR bytes;
            for (int i = 0; i < 16; ++i) {
                bytes.c[i] = static_cast<quint8>(reader.bytes().at(rdataStart + i));
            }
            record.address = QHostAddress(bytes);
            reader.seek(rdataEnd);
        }
        break;
    case TypeTXT: {
        int cursor = rdataStart;
        int entries = 0;
        while (cursor < rdataEnd) {
            const int length = static_cast<quint8>(reader.bytes().at(cursor));
            if (cursor + 1 + length > rdataEnd || ++entries > 64) { return false; }
            const QByteArray entry = reader.bytes().mid(cursor + 1, length);
            cursor += 1 + length;
            if (entry.isEmpty()) { continue; }
            const int equals = entry.indexOf('=');
            if (equals < 0) {
                record.txt.insert(QString :: fromUtf8(entry).toLower(), QString(""));
            } else if (equals > 0) { // "=value" without a key is ignored
                record.txt.insert(QString :: fromUtf8(entry.left(equals)).toLower(), QString :: fromUtf8(entry.mid(equals + 1)));
            }
        }
        reader.seek(rdataEnd);
        break;
    }
    default: // a type we do not use (for example NSEC): skip its data
        reader.seek(rdataEnd);
        record.type = 0; // marks "ignore me"
        return true;
    }

    if (reader.position() != rdataEnd) { // the data must be exactly as long as the record says
        return false;
    }
    return true;
}

bool readSection (Reader &reader, int count, QList<Record> &out)
{
    for (int i = 0; i < count; ++i) {
        Record record;
        if (!readRecord(reader, record)) {
            return false;
        }
        if (record.type != 0) {
            out.append(record);
        }
    }
    return true;
}

} // namespace

QByteArray encode (const Message &message)
{
    QByteArray out;
    put16(out, message.id);
    put16(out, message.response ? 0x8400 : 0x0000); // response + authoritative answer, or a plain query
    put16(out, static_cast<quint16>(message.questions.size()));
    put16(out, static_cast<quint16>(message.answers.size()));
    put16(out, static_cast<quint16>(message.authorities.size()));
    put16(out, static_cast<quint16>(message.additionals.size()));

    for (const Question &question : message.questions) {
        if (!putName(out, question.name)) {
            return QByteArray();
        }
        put16(out, question.type);
        put16(out, static_cast<quint16>(1 | (question.unicastResponse ? 0x8000 : 0)));
    }
    for (const QList<Record> *section : { &message.answers, &message.authorities, &message.additionals }) {
        for (const Record &record : *section) {
            if (!putRecord(out, record)) {
                return QByteArray();
            }
        }
    }
    return out.size() <= maxPacketSize ? out : QByteArray();
}

bool decode (const QByteArray &data, Message &message)
{
    message = Message();
    if (data.size() < 12 || data.size() > maxPacketSize) {
        return false;
    }

    Reader reader(data);
    quint16 flags = 0, questions = 0, answers = 0, authorities = 0, additionals = 0;
    reader.read16(message.id);
    reader.read16(flags);
    reader.read16(questions);
    reader.read16(answers);
    reader.read16(authorities);
    reader.read16(additionals);

    message.response = (flags & 0x8000) != 0;
    if ((flags & 0x7800) != 0) { // only the standard opcode 0 is mDNS
        return false;
    }
    if (questions > maxRecordsPerSection || answers > maxRecordsPerSection
        || authorities > maxRecordsPerSection || additionals > maxRecordsPerSection) {
        return false;
    }
    // a record needs at least 11 bytes, a question 5: a header that promises more than the packet can hold is a lie
    if (questions * 5 + (answers + authorities + additionals) * 11 > data.size() - 12) {
        return false;
    }

    for (int i = 0; i < questions; ++i) {
        Question question;
        quint16 classField = 0;
        if (!reader.readName(question.name) || !reader.read16(question.type) || !reader.read16(classField)) {
            return false;
        }
        question.unicastResponse = (classField & 0x8000) != 0;
        message.questions.append(question);
    }
    return readSection(reader, answers, message.answers)
        && readSection(reader, authorities, message.authorities)
        && readSection(reader, additionals, message.additionals);
}

}
