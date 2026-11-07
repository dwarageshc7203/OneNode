#ifndef NETUTIL_H
#define NETUTIL_H

#include <QHostAddress>
#include <QString>

// shows "127.0.0.1" instead of "::ffff:127.0.0.1" for IPv4 peers on a dual-stack socket
inline QString prettyNetworkAddress(const QHostAddress &address)
{
    bool isIPv4 = false;
    const quint32 v4 = address.toIPv4Address(&isIPv4); // turns to address into a 32 bit number and sets isIPv4 flag to true
    return isIPv4 ? QHostAddress(v4).toString() : address.toString(); // if the flag is true, print out the readable IPv4 address, else, present the argument address as it is
}

#endif // NETUTIL_H
