#include "DelimitedEngine.h"

#include <QtCore/QDateTime>
#include <QtCore/QRegularExpression>

namespace serein::scope::serial {
namespace {

qsizetype completeUtf8Prefix(const QByteArrayView buffer)
{
    if (buffer.isEmpty()) return 0;
    qsizetype start = buffer.size() - 1;
    while (start > 0 && (quint8(buffer.at(start)) & 0xc0u) == 0x80u) {
        --start;
    }
    const quint8 lead = quint8(buffer.at(start));
    int expected = 0;
    if ((lead & 0x80u) == 0) {
        expected = 1;
    } else if (lead >= 0xc2u && lead <= 0xdfu) {
        expected = 2;
    } else if (lead >= 0xe0u && lead <= 0xefu) {
        expected = 3;
    } else if (lead >= 0xf0u && lead <= 0xf4u) {
        expected = 4;
    }
    return expected > 0 && buffer.size() - start < expected ? start : buffer.size();
}

} // namespace

QString DelimitedEngine::id() const
{
    return QStringLiteral("text.delimited");
}

QString DelimitedEngine::displayName() const
{
    return QStringLiteral("Delimited numeric");
}

void DelimitedEngine::reset()
{
}

ParseResult DelimitedEngine::feed(const QByteArrayView buffer)
{
    const QByteArray bytes(buffer.data(), buffer.size());
    const qsizetype end = bytes.lastIndexOf('\n');
    if (end < 0) {
        return {.consumed = 0, .packets = {}, .needMore = true};
    }

    PacketList packets;
    const qsizetype consumed = end + 1;
    const QList<QByteArray> lines = bytes.first(consumed).split('\n');
    static const QRegularExpression separators(QStringLiteral("[,;\\s]+"));
    for (QByteArray raw : lines) {
        raw = raw.trimmed();
        if (raw.isEmpty()) {
            continue;
        }
        const QString text = QString::fromUtf8(raw);
        const QStringList fields = text.split(separators, Qt::SkipEmptyParts);
        QVector<double> values;
        values.reserve(fields.size());
        bool valid = !fields.isEmpty();
        for (const QString &field : fields) {
            bool ok = false;
            const double value = field.toDouble(&ok);
            if (!ok) {
                valid = false;
                break;
            }
            values.append(value);
        }
        const qint64 now = QDateTime::currentMSecsSinceEpoch() * 1000;
        if (valid) {
            QStringList labels;
            for (qsizetype index = 0; index < values.size(); ++index) {
                labels.append(QStringLiteral("CH%1").arg(index + 1));
            }
            packets.append(SamplePacket{now, values, labels, id()});
        } else {
            packets.append(TextPacket{now, text, QStringLiteral("rx")});
            packets.append(EventPacket{
                now,
                QStringLiteral("warning"),
                QStringLiteral("non-numeric line: %1").arg(text.left(120)),
                QStringLiteral("parse"),
                {},
            });
        }
    }
    return {.consumed = consumed, .packets = packets, .needMore = false};
}

QString RawTextEngine::id() const
{
    return QStringLiteral("text.raw");
}

QString RawTextEngine::displayName() const
{
    return QStringLiteral("Raw text");
}

void RawTextEngine::reset()
{
}

ParseResult RawTextEngine::feed(const QByteArrayView buffer)
{
    if (buffer.isEmpty()) {
        return {.consumed = 0, .packets = {}, .needMore = true};
    }
    const qsizetype consumed = completeUtf8Prefix(buffer);
    if (consumed == 0) {
        return {.consumed = 0, .packets = {}, .needMore = true};
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch() * 1000;
    PacketList packets;
    packets.append(TextPacket{
        now,
        QString::fromUtf8(buffer.data(), consumed),
        QStringLiteral("rx"),
    });
    return {.consumed = consumed, .packets = packets, .needMore = consumed < buffer.size()};
}

} // namespace serein::scope::serial
