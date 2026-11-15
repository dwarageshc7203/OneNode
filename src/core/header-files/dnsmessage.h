#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QList>
#include <QMap>
#include <QString>

// Just enough of DNS (RFC 1035) for mDNS service discovery (RFC 6762 / 6763): build and read packets.
//
// Analogy: a postal clerk who can write the few kinds of forms we need and who reads every incoming form
// with suspicion. Everything arriving from the network is untrusted, so reading NEVER trusts a length,
// a pointer or a count: any mistake means "this packet is rejected", never a crash and never a huge allocation.
namespace Dns {

constexpr quint16 TypeA = 1;
constexpr quint16 TypePTR = 12;
constexpr quint16 TypeTXT = 16;
constexpr quint16 TypeAAAA = 28;
constexpr quint16 TypeSRV = 33;
constexpr quint16 TypeANY = 255;

constexpr int maxPacketSize = 9000;   // mDNS packets are far smaller; refuse anything bigger
constexpr int maxRecordsPerSection = 100;

struct Question {
    QString name;               // "_onenode._tcp.local" (labels joined by dots, no trailing dot)
    quint16 type = 0;
    bool unicastResponse = false; // the "QU" bit: please answer me directly (we always answer by multicast anyway)
};

struct Record {
    QString name;
    quint16 type = 0;
    bool cacheFlush = false;    // "this record replaces older ones with the same name and type"
    quint32 ttl = 0;            // seconds; 0 means "goodbye, forget this"

    // what the record says (only the field that fits `type` is used)
    QString target;                       // PTR: the instance name; SRV: the host name
    quint16 port = 0, priority = 0, weight = 0; // SRV
    QHostAddress address;                 // A / AAAA
    QMap<QString, QString> txt;           // TXT: key=value pairs, keys lower-cased
};

struct Message {
    quint16 id = 0;
    bool response = false;
    QList<Question> questions;
    QList<Record> answers;
    QList<Record> authorities;
    QList<Record> additionals;
};

// Builds the bytes of a message. Returns an empty array when something cannot be encoded
// (a label over 63 characters, a name over 255, a TXT entry over 255 bytes, ...).
QByteArray encode (const Message &message);

// Reads a message. Returns false for ANY malformation, truncation, loop, over-long name or over-large count.
bool decode (const QByteArray &data, Message &message);

// DNS names are case-insensitive
inline QString normalized (const QString &name) { return name.toLower(); }

}
