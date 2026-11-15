#pragma once

#include "mdnsservice.h"

#include <QCoreApplication>
#include <QTimer>

// A pretend room for tests: every packet one device sends is heard by all the others (and, like on a real
// network, by the sender itself). Delivery goes through the Qt event loop, like a real socket's would.
class FakeRoom;

class FakeTransport : public MdnsTransport { // (no Q_OBJECT: it adds no signals or slots of its own)
public:
    FakeTransport (FakeRoom *room, const QHostAddress &address, bool failStart = false) : room(room), failStart(failStart)
    {
        MdnsInterface i; i.index = 1; i.address = address; i.prefixLength = 24;
        list << i;
    }
    ~FakeTransport() override;
    bool start (QString &error) override { if (failStart) { error = "no network"; return false; } started = true; return true; }
    void stop() override { started = false; }
    QList<MdnsInterface> interfaces() const override { return list; }
    void send (const QByteArray &packet, int interfaceIndex) override;
    FakeRoom *room;
    bool failStart;
    bool started = false;
    bool reachable = true;           // false: unplugged cable, nothing in or out
    QList<MdnsInterface> list;
    QList<QByteArray> sent;
    void hear (const QByteArray &packet, const QHostAddress &from) { if (reachable && started) { emit received(packet, from, 1); } }
};

class FakeRoom {
public:
    QList<FakeTransport *> nodes;
    bool echoToSender = true;
    int pending = 0;

    void deliver (FakeTransport *from, const QByteArray &packet)
    {
        if (!from -> reachable) { return; }
        for (FakeTransport *node : nodes) {
            if (node == from && !echoToSender) { continue; }
            post(node, packet, from -> list.first().address);
        }
    }
    void inject (const QByteArray &packet, const QHostAddress &from)  // a stranger shouting into the room
    {
        for (FakeTransport *node : nodes) { post(node, packet, from); }
    }
    void pump()                                                       // lets every packet in flight arrive
    {
        for (int guard = 0; pending > 0 && guard < 100000; ++guard) {
            QCoreApplication :: processEvents();
        }
    }

private:
    void post (FakeTransport *to, const QByteArray &packet, const QHostAddress &from)
    {
        ++pending;
        QTimer :: singleShot(0, to, [this, to, packet, from] { --pending; to -> hear(packet, from); });
    }
};

inline void FakeTransport :: send (const QByteArray &packet, int)
{
    if (!started) { return; }
    sent << packet;
    room -> deliver(this, packet);
}

inline FakeTransport :: ~FakeTransport()
{
    room -> nodes.removeAll(this); // a device that is gone no longer hears or gets packets
}
