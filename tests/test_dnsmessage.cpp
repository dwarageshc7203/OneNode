#include "dnsmessage.h"

#include <QtTest>
#include <QRandomGenerator>

using namespace Dns;

static Message sampleResponse()
{
    Message m;
    m.response = true;
    Record ptr; ptr.name = "_onenode._tcp.local"; ptr.type = TypePTR; ptr.ttl = 120; ptr.target = "OneNode-aabbccdd._onenode._tcp.local";
    Record srv; srv.name = ptr.target; srv.type = TypeSRV; srv.ttl = 120; srv.cacheFlush = true; srv.port = 45454; srv.target = "onenode-aabbccdd.local";
    Record txt; txt.name = ptr.target; txt.type = TypeTXT; txt.ttl = 120; txt.cacheFlush = true;
    txt.txt.insert("v", "2"); txt.txt.insert("name", "Dwarag's laptop"); txt.txt.insert("fp", "aabbccddeeff0011");
    Record a; a.name = srv.target; a.type = TypeA; a.ttl = 120; a.cacheFlush = true; a.address = QHostAddress("192.168.1.23");
    m.answers << ptr;
    m.additionals << srv << txt << a;
    return m;
}

class TestDnsMessage : public QObject {

    Q_OBJECT

private slots:

    void aResponseSurvivesEncodeAndDecode()
    {
        const Message original = sampleResponse();
        const QByteArray bytes = encode(original);
        QVERIFY(!bytes.isEmpty());

        Message back;
        QVERIFY(decode(bytes, back));
        QVERIFY(back.response);
        QCOMPARE(back.answers.size(), 1);
        QCOMPARE(back.additionals.size(), 3);
        QCOMPARE(back.answers.first().target, QString("OneNode-aabbccdd._onenode._tcp.local"));
        QCOMPARE(back.answers.first().ttl, quint32(120));
        QVERIFY(!back.answers.first().cacheFlush);

        const Record srv = back.additionals.at(0);
        QCOMPARE(srv.type, TypeSRV);
        QCOMPARE(srv.port, quint16(45454));
        QCOMPARE(srv.target, QString("onenode-aabbccdd.local"));
        QVERIFY(srv.cacheFlush);

        const Record txt = back.additionals.at(1);
        QCOMPARE(txt.txt.value("name"), QString("Dwarag's laptop"));
        QCOMPARE(txt.txt.value("fp"), QString("aabbccddeeff0011"));
        QCOMPARE(txt.txt.value("v"), QString("2"));

        QCOMPARE(back.additionals.at(2).address, QHostAddress("192.168.1.23"));
    }

    void aQuerySurvives()
    {
        Message q;
        Question question; question.name = "_onenode._tcp.local"; question.type = TypePTR; question.unicastResponse = true;
        q.questions << question;
        Message back;
        QVERIFY(decode(encode(q), back));
        QVERIFY(!back.response);
        QCOMPARE(back.questions.size(), 1);
        QCOMPARE(back.questions.first().name, QString("_onenode._tcp.local"));
        QCOMPARE(back.questions.first().type, TypePTR);
        QVERIFY(back.questions.first().unicastResponse);
    }

    void ipv6AndEmptyTxtSurvive()
    {
        Message m; m.response = true;
        Record aaaa; aaaa.name = "h.local"; aaaa.type = TypeAAAA; aaaa.ttl = 5; aaaa.address = QHostAddress("fe80::1");
        Record txt; txt.name = "x.local"; txt.type = TypeTXT; txt.ttl = 5;
        m.answers << aaaa << txt;
        Message back;
        QVERIFY(decode(encode(m), back));
        QCOMPARE(back.answers.at(0).address, QHostAddress("fe80::1"));
        QVERIFY(back.answers.at(1).txt.isEmpty());
    }

    void compressedNamesAreRead()
    {
        // header, one question "a.local" type PTR, one answer whose name is a pointer (0xC00C) to that question
        QByteArray p;
        p.append(QByteArray::fromHex("000084000001000100000000"));
        p.append(QByteArray::fromHex("0161056c6f63616c00"));  // 1 'a' 5 'local' 0
        p.append(QByteArray::fromHex("000c0001"));                                  // type PTR, class IN
        p.append(QByteArray::fromHex("c00c"));                                      // name = pointer to offset 12
        p.append(QByteArray::fromHex("000c0001000000780004"));                      // PTR IN ttl 120 rdlength 4
        p.append(QByteArray::fromHex("0162c00c"));                                  // target: 'b' + pointer -> "b.a.local"
        Message m;
        QVERIFY(decode(p, m));
        QCOMPARE(m.answers.size(), 1);
        QCOMPARE(m.answers.first().name, QString("a.local"));
        QCOMPARE(m.answers.first().target, QString("b.a.local"));
    }

    void badPacketsAreRejected_data()
    {
        QTest::addColumn<QByteArray>("packet");
        QTest::newRow("empty") << QByteArray();
        QTest::newRow("shorter than a header") << QByteArray(11, '\0');
        QTest::newRow("header promises records that are not there") << QByteArray::fromHex("000084000000000500000000");
        QTest::newRow("a huge count") << QByteArray::fromHex("00008400ffffffffffffffff");
        QTest::newRow("a pointer loop") << (QByteArray::fromHex("000084000001000000000000") + QByteArray::fromHex("c00c00010001"));
        QTest::newRow("a forward pointer") << (QByteArray::fromHex("000084000001000000000000") + QByteArray::fromHex("c01000010001"));
        QTest::newRow("a label running past the end") << (QByteArray::fromHex("000084000001000000000000") + QByteArray::fromHex("3f6162"));
        QTest::newRow("reserved label bits") << (QByteArray::fromHex("000084000001000000000000") + QByteArray::fromHex("800000010001"));
        QTest::newRow("a non-standard opcode") << QByteArray::fromHex("000090000000000000000000");
        QTest::newRow("too big") << QByteArray(Dns::maxPacketSize + 1, 'x');
    }

    void badPacketsAreRejected()
    {
        QFETCH(QByteArray, packet);
        Message m;
        QVERIFY(!decode(packet, m));
    }

    void aRecordWithTheWrongLengthIsRejected()
    {
        Message m = sampleResponse();
        QByteArray bytes = encode(m);
        // find the A record's rdlength (4) at the very end: 2 bytes before the 4 address bytes, and make it 5
        bytes[bytes.size() - 5] = 5;
        Message back;
        QVERIFY(!decode(bytes, back));
    }

    void tooLongLabelsAndNamesCannotBeEncoded()
    {
        Message m; m.response = true;
        Record r; r.name = QString(64, 'a') + ".local"; r.type = TypeA; r.address = QHostAddress("1.2.3.4");
        m.answers << r;
        QVERIFY(encode(m).isEmpty());

        Record tooLong; tooLong.name = QString("a.").repeated(130) + "local"; tooLong.type = TypeA; tooLong.address = QHostAddress("1.2.3.4");
        Message m2; m2.answers << tooLong;
        QVERIFY(encode(m2).isEmpty());
    }

    void unknownRecordTypesAreSkippedNotFatal()
    {
        // an answer of type 47 (NSEC) with 2 bytes of data, then nothing else: skipped, packet still fine
        QByteArray p = QByteArray::fromHex("000084000000000100000000");
        p.append(QByteArray::fromHex("016100002f0001000000780002abcd"));
        Message m;
        QVERIFY(decode(p, m));
        QCOMPARE(m.answers.size(), 0);
    }

    // The important one: whatever bytes arrive, we never crash or hang. 40 000 random damages to a good packet.
    void randomDamageNeverCrashes()
    {
        const QByteArray good = encode(sampleResponse());
        QRandomGenerator rng(12345);
        int accepted = 0;
        for (int i = 0; i < 40000; ++i) {
            QByteArray bytes = good;
            const int changes = 1 + rng.bounded(4);
            for (int c = 0; c < changes; ++c) {
                bytes[rng.bounded(bytes.size())] = static_cast<char>(rng.bounded(256));
            }
            if (rng.bounded(5) == 0) {
                bytes.truncate(rng.bounded(bytes.size() + 1));
            }
            Message m;
            accepted += decode(bytes, m) ? 1 : 0;
        }
        QVERIFY(accepted > 0); // some damage is harmless (a changed ttl), most is not: both are fine, no crash is the point
    }

    void pureRandomBytesNeverCrash()
    {
        QRandomGenerator rng(777);
        for (int i = 0; i < 20000; ++i) {
            QByteArray bytes(rng.bounded(300), '\0');
            for (char &b : bytes) { b = static_cast<char>(rng.bounded(256)); }
            Message m;
            decode(bytes, m);
        }
        QVERIFY(true);
    }

    void namesCompareIgnoringCase()
    {
        QCOMPARE(normalized("_OneNode._TCP.local"), normalized("_onenode._tcp.LOCAL"));
    }
};

QTEST_GUILESS_MAIN(TestDnsMessage)
#include "test_dnsmessage.moc"
