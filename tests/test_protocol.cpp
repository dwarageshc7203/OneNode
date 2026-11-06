#include <QtTest>
#include <QJsonObject>
#include <QtEndian>
#include "protocol.h"

class TestProtocol : public QObject
{
    Q_OBJECT

private slots:
    void roundTrip();
    void splitData();
    void byteAtATime();
    void twoFramesBackToBack();
    void garbageIsInvalid();
    void headerSizeLimit();
    void zeroLengthIsInvalid();
};

static QJsonObject sampleHeader()
{
    QJsonObject header;
    header["v"] = Protocol :: version;
    header["type"] = "file";
    header["filename"] = "hello.txt";
    header["size"] = 5;
    return header;
}

void TestProtocol :: roundTrip()
{
    QByteArray buffer = Protocol :: encodeFrame(sampleHeader());
    QJsonObject decoded;

    QVERIFY(Protocol :: tryDecodeFrame(buffer, decoded) == Protocol :: decodeStatus :: Ok);
    QCOMPARE(decoded, sampleHeader());
    QVERIFY(buffer.isEmpty());                       // every byte was consumed
}

void TestProtocol :: splitData()
{
    const QByteArray frame = Protocol :: encodeFrame(sampleHeader());
    QJsonObject decoded;

    QByteArray buffer = frame.left(3);               // not even the whole length prefix
    QVERIFY(Protocol :: tryDecodeFrame(buffer, decoded) == Protocol :: decodeStatus :: NeedMoreData);
    QCOMPARE(buffer.size(), qsizetype(3));           // nothing was consumed

    buffer = frame.left(frame.size() - 1);           // everything except the last byte
    QVERIFY(Protocol :: tryDecodeFrame(buffer, decoded) == Protocol :: decodeStatus :: NeedMoreData);

    buffer.append(frame.right(1));                   // the missing byte arrives
    QVERIFY(Protocol :: tryDecodeFrame(buffer, decoded) == Protocol :: decodeStatus :: Ok);
}

void TestProtocol :: byteAtATime()
{
    const QByteArray frame = Protocol :: encodeFrame(sampleHeader());
    QByteArray buffer;
    QJsonObject decoded;
    Protocol :: decodeStatus status = Protocol :: decodeStatus :: NeedMoreData;

    for (qsizetype i = 0; i < frame.size(); ++i) {
        buffer.append(frame.at(i));
        status = Protocol :: tryDecodeFrame(buffer, decoded);
        if (i < frame.size() - 1) {
            QVERIFY(status == Protocol :: decodeStatus :: NeedMoreData);
        }
    }

    QVERIFY(status == Protocol :: decodeStatus :: Ok);
    QCOMPARE(decoded, sampleHeader());
}

void TestProtocol :: twoFramesBackToBack()
{
    QJsonObject a;
    a["type"] = "a";
    QJsonObject b;
    b["type"] = "b";

    QByteArray buffer = Protocol :: encodeFrame(a) + Protocol :: encodeFrame(b);
    QJsonObject decoded;

    QVERIFY(Protocol :: tryDecodeFrame(buffer, decoded) == Protocol :: decodeStatus :: Ok);
    QCOMPARE(decoded, a);
    QVERIFY(Protocol :: tryDecodeFrame(buffer, decoded) == Protocol :: decodeStatus :: Ok);
    QCOMPARE(decoded, b);
    QVERIFY(buffer.isEmpty());
}

void TestProtocol :: garbageIsInvalid()
{
    QJsonObject decoded;

    QByteArray text("Hello, from OneNode\n");        // "Hell" reads as a huge length
    QVERIFY(Protocol :: tryDecodeFrame(text, decoded) == Protocol :: decodeStatus :: Invalid);

    QByteArray notJson(4, '\0');                     // a valid length, but the body is not JSON
    notJson[3] = 8;
    notJson.append("notjson!");
    QVERIFY(Protocol :: tryDecodeFrame(notJson, decoded) == Protocol :: decodeStatus :: Invalid);
}

void TestProtocol :: headerSizeLimit()
{
    QJsonObject decoded;
    QByteArray buffer(4, '\0');

    qToBigEndian<quint32>(Protocol :: maxHeaderSize + 1, buffer.data());   // one over the limit
    QVERIFY(Protocol :: tryDecodeFrame(buffer, decoded) == Protocol :: decodeStatus :: Invalid);

    qToBigEndian<quint32>(Protocol :: maxHeaderSize, buffer.data());       // exactly at the limit
    QVERIFY(Protocol :: tryDecodeFrame(buffer, decoded) == Protocol :: decodeStatus :: NeedMoreData);
}

void TestProtocol :: zeroLengthIsInvalid()
{
    QByteArray buffer(4, '\0');
    QJsonObject decoded;
    QVERIFY(Protocol :: tryDecodeFrame(buffer, decoded) == Protocol :: decodeStatus :: Invalid);
}

QTEST_APPLESS_MAIN(TestProtocol)
#include "test_protocol.moc"
