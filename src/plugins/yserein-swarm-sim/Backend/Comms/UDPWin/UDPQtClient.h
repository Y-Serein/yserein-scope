#pragma once

#include <QtCore/QtTypes>
#include <QtCore/QMetaType>
#include <QHostAddress>

#include "NetWorkUtilities.h"

struct UDPQtClient
{
    UDPQtClient()
        : address(QHostAddress::Null)
        , port(0)
    {
    }

    UDPQtClient(const QHostAddress& address_, quint16 port_)
        : address(address_)
        , port(port_)
    {
    }

    // inet_ntoa 比较耗时
    UDPQtClient(const IN_ADDR& address_, const USHORT& port_)
        : address(QHostAddress(ntohl(address_.s_addr)))  // 直接构建 IP 地址，避免字符串转换
        , port(ntohs(port_))
    {
    }

    SOCKADDR_IN toWinAddr() const
    {
        SOCKADDR_IN addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = htonl(address.toIPv4Address());
        return addr;
    }


    bool operator==(const UDPQtClient& other) const
    {
        return ((address == other.address) && (port == other.port));
    }

    bool isNull() const
    {
        return address == QHostAddress::Null && port == 0;
    }

    bool isLocal() const
    {
        return address.isLoopback();
    }

    QHostAddress address = QHostAddress::Null;
    quint16 port = 0;
};
Q_DECLARE_METATYPE(UDPQtClient)

inline QDebug operator << (QDebug dbg, const UDPQtClient& client)
{
    dbg.nospace() << "UDPQtClient(" << client.address.toString() << ":" << client.port << ")";
    return dbg.space();
}

// 为 UDPQtClient 提供自定义的 qHash 函数
inline uint qHash(const UDPQtClient& client, uint seed = 0)
{
    // 使用 address 和 port 来生成哈希值
    return qHash(client.address, seed) ^ qHash(client.port, seed);
}