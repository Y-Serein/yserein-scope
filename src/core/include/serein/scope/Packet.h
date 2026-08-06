#pragma once

#include "CoreExport.h"

#include <QtCore/QByteArray>
#include <QtCore/QList>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVariantMap>
#include <QtCore/QVector>

#include <variant>

namespace serein::scope {

struct SamplePacket {
    qint64 timestampUs = 0;
    QVector<double> values;
    QStringList labels;
    QString source;
};

struct ImagePacket {
    qint64 timestampUs = 0;
    int width = 0;
    int height = 0;
    QByteArray pixels;
    QString pixelFormat = QStringLiteral("gray8");
    QString source;
};

struct EventPacket {
    qint64 timestampUs = 0;
    QString level;
    QString message;
    QString category;
    QVariantMap fields;
};

struct TextPacket {
    qint64 timestampUs = 0;
    QString text;
    QString stream = QStringLiteral("rx");
};

using Packet = std::variant<SamplePacket, ImagePacket, EventPacket, TextPacket>;
using PacketList = QList<Packet>;

} // namespace serein::scope
