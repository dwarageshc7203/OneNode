#include "protocol.h"

#include <QtTest>
#include <QJsonObject>
#include <QtEndian>

class TestProtocol : public QObject
{
    Q_OBJECT

private slots:
    // methods which are tested one by one, each one has no arguments
    void roundTrip();
    void splitData();
    void byteAtATime();
    void twoFramesBackToBack();
    void garbageIsInvalid();
    void headerSizeLimit();
    void zeroLengthIsInvalid();
};

// prepping a JSON header
static QJsonObject sampleHeader()
{
    QJsonObject header;
    header["v"] = Protocol :: version;
    header["type"] = "file";
    header["filename"] = "hello.txt";
    header["size"] = 5;
    return header;
}

// encoding the header, then checking the decoded value with the original header to see if it's the same
void TestProtocol :: roundTrip()
{
    QByteArray buffer = Protocol :: encodeFrame(sampleHeader());
    QJsonObject decoded;

    QVERIFY(Protocol :: tryDecodeFrame(buffer, decoded) == Protocol :: decodeStatus :: Ok);
    QCOMPARE(decoded, sampleHeader()); // checking if the decoded value of buffer (encoded) is the same as the header
    QVERIFY(buffer.isEmpty()); // make sure the buffer is emptied
}

// encoding header with only incomplete amount of data and checking the statuses
void TestProtocol :: splitData()
{
    const QByteArray frame = Protocol :: encodeFrame(sampleHeader());
    QJsonObject decoded;

    QByteArray buffer = frame.left(3); // Less than 4 bytes (not enough for the prefix)
    QVERIFY(Protocol :: tryDecodeFrame(buffer, decoded) == Protocol :: decodeStatus :: NeedMoreData);
    QCOMPARE(buffer.size(), qsizetype(3));

    buffer = frame.left(frame.size() - 1); // prefix sent, but last one byte of the JSON header not sent (eg: size = 10, but only 9 + 4[of prefix] bytes sent)
    QVERIFY(Protocol :: tryDecodeFrame(buffer, decoded) == Protocol :: decodeStatus :: NeedMoreData);

    buffer.append(frame.right(1)); // sending the last missing byte of the JSON header
    QVERIFY(Protocol :: tryDecodeFrame(buffer, decoded) == Protocol :: decodeStatus :: Ok);
}

// appending 1 byte at a time of the header into the buffer
void TestProtocol :: byteAtATime()
{
    const QByteArray frame = Protocol :: encodeFrame(sampleHeader());
    QByteArray buffer;
    QJsonObject decoded;
    Protocol :: decodeStatus status = Protocol :: decodeStatus :: NeedMoreData;

    for (qsizetype i = 0; i < frame.size(); ++i) {
        buffer.append(frame.at(i)); // appending just 1 byte at a time - filling out the prefix, then the header
        status = Protocol :: tryDecodeFrame(buffer, decoded);
        if (i < frame.size() - 1) {
            QVERIFY(status == Protocol :: decodeStatus :: NeedMoreData);
        }
    }

    QVERIFY(status == Protocol :: decodeStatus :: Ok);
    QCOMPARE(decoded, sampleHeader());
}

// we are checking if we send two frames (two separate JSON header / messages), does it interfere with each other in buffer
void TestProtocol :: twoFramesBackToBack()
{
    QJsonObject a;
    a["type"] = "a";
    QJsonObject b;
    b["type"] = "b";

    QByteArray buffer = Protocol :: encodeFrame(a) + Protocol :: encodeFrame(b);
    QJsonObject decoded;

    // we get the output of a and b separately, since they are different messages
    QVERIFY(Protocol :: tryDecodeFrame(buffer, decoded) == Protocol :: decodeStatus :: Ok);
    QCOMPARE(decoded, a);
    QVERIFY(Protocol :: tryDecodeFrame(buffer, decoded) == Protocol :: decodeStatus :: Ok);
    QCOMPARE(decoded, b);
    QVERIFY(buffer.isEmpty());
}

// testing out invalid inputs
void TestProtocol :: garbageIsInvalid()
{
    QJsonObject decoded;

    QByteArray text("Hello, from OneNode\n"); // remember that the prefix counts the first 4 bytes, and it takes just 'H', 'e', 'l', 'l' as the JSON header size-> becomes completely invalid due to large header size
    QVERIFY(Protocol :: tryDecodeFrame(text, decoded) == Protocol :: decodeStatus :: Invalid);

    QByteArray notJson(4, '\0');
    notJson[3] = 8; // we are saying the JSON header size is 8
    notJson.append("notjson!"); // this is an invalid json syntax
    QVERIFY(Protocol :: tryDecodeFrame(notJson, decoded) == Protocol :: decodeStatus :: Invalid);
}

// testing the maxHeaderSize limits
void TestProtocol :: headerSizeLimit()
{
    QJsonObject decoded;
    QByteArray buffer(4, '\0');

    qToBigEndian<quint32>(Protocol :: maxHeaderSize + 1, buffer.data()); // crossing the maxHeaderSize limit
    QVERIFY(Protocol :: tryDecodeFrame(buffer, decoded) == Protocol :: decodeStatus :: Invalid);

    qToBigEndian<quint32>(Protocol :: maxHeaderSize, buffer.data()); // exactly at the limit -> expects the JSON header
    QVERIFY(Protocol :: tryDecodeFrame(buffer, decoded) == Protocol :: decodeStatus :: NeedMoreData);
}

// testing for a prefix that contains JSON header size is 0
void TestProtocol :: zeroLengthIsInvalid()
{
    QByteArray buffer(4, '\0');
    QJsonObject decoded;
    QVERIFY(Protocol :: tryDecodeFrame(buffer, decoded) == Protocol :: decodeStatus :: Invalid);
}

QTEST_APPLESS_MAIN(TestProtocol)
#include "test_protocol.moc"
