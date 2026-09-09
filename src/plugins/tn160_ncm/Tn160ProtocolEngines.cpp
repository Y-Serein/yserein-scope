#include "Tn160ProtocolEngines.h"

#include "Tn160Engine.h"

#include <QtCore/QDateTime>
#include <QtCore/QtEndian>

#include <algorithm>
#include <utility>

namespace serein::scope::tn160ncm {
namespace {

constexpr int RawWordsPerRow = FrameWidth + 2;
constexpr int RawFrameSize = 2 + FrameHeight * RawWordsPerRow * 2 + 2;

qint64 nowUsec()
{
    return QDateTime::currentMSecsSinceEpoch() * 1000;
}

qsizetype unmatchedSuffix(const QByteArrayView buffer,
                          const QByteArrayView marker)
{
    const qsizetype maximum = std::min(buffer.size(), marker.size() - 1);
    for (qsizetype length = maximum; length > 0; --length) {
        if (buffer.last(length) == marker.first(length)) return length;
    }
    return 0;
}

} // namespace

QString Tn160RawFrameEngine::id() const
{
    return QString::fromLatin1(Tn160RawFrameSource);
}

QString Tn160RawFrameEngine::displayName() const
{
    return QStringLiteral("TN160 RAW_U16 frame");
}

void Tn160RawFrameEngine::reset()
{
}

bool Tn160RawFrameEngine::plausible(const QByteArrayView frame)
{
    if (frame.size() != RawFrameSize
        || quint8(frame.at(0)) != 0xff || quint8(frame.at(1)) != 0xff) {
        return false;
    }
    for (qsizetype offset = 2; offset + 1 < frame.size(); offset += 2) {
        const quint16 word = qFromBigEndian<quint16>(
            reinterpret_cast<const uchar *>(frame.data() + offset));
        if (word > 0x3fffu) return false;
    }
    return true;
}

ParseResult Tn160RawFrameEngine::feed(const QByteArrayView buffer)
{
    static const QByteArray marker("\xff\xff", 2);
    qsizetype start = buffer.indexOf(QByteArrayView(marker));
    if (start < 0) {
        const qsizetype keep = unmatchedSuffix(buffer, QByteArrayView(marker));
        return {.consumed = buffer.size() - keep,
                .packets = {}, .needMore = keep > 0};
    }
    while (start >= 0) {
        if (buffer.size() - start < RawFrameSize) {
            return {.consumed = start, .packets = {}, .needMore = true};
        }
        const QByteArrayView candidate(buffer.data() + start, RawFrameSize);
        if (plausible(candidate)) {
            const auto *raw = reinterpret_cast<const uchar *>(candidate.data());
            quint64 sum = 0;
            for (int y = 0; y < FrameHeight; ++y) {
                const int rowOffset = 2 + y * RawWordsPerRow * 2;
                for (int x = 0; x < FrameWidth; ++x) {
                    sum += qFromBigEndian<quint16>(raw + rowOffset + x * 2);
                }
            }
            const quint16 vtemp = qFromBigEndian<quint16>(
                raw + RawFrameSize - 2);
            const qint64 timestamp = nowUsec();
            PacketList packets;
            packets.append(ImagePacket{
                timestamp,
                FrameWidth,
                FrameHeight,
                QByteArray(candidate.data(), candidate.size()),
                QStringLiteral("tn160.raw-u16be.v1"),
                id(),
            });
            packets.append(SamplePacket{
                timestamp,
                {double(sum) / (FrameWidth * FrameHeight), double(vtemp)},
                {QStringLiteral("Mean ADU"), QStringLiteral("VTEMP ADU")},
                id(),
            });
            return {.consumed = start + RawFrameSize,
                    .packets = std::move(packets), .needMore = false};
        }
        start = buffer.indexOf(QByteArrayView(marker), start + 1);
    }
    const qsizetype keep = unmatchedSuffix(buffer, QByteArrayView(marker));
    return {.consumed = buffer.size() - keep,
            .packets = {}, .needMore = keep > 0};
}

} // namespace serein::scope::tn160ncm
