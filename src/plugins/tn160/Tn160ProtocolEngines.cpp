#include "Tn160ProtocolEngines.h"

#include "Tn160Engine.h"

#include <QtCore/QDateTime>
#include <QtCore/QtEndian>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace serein::scope::tn160 {
namespace {

constexpr int RawWordsPerRow = FrameWidth + 2;
constexpr int RawFrameSize = 2 + FrameHeight * RawWordsPerRow * 2 + 2;
constexpr int WriteReplyBytes = 20;

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

quint32 readLe32(const QByteArrayView bytes, const qsizetype offset)
{
    return qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar *>(bytes.data() + offset));
}

} // namespace

QString Tn160DiscardEngine::id() const
{
    return QStringLiteral("tn160.discard.v1");
}

QString Tn160DiscardEngine::displayName() const
{
    return QStringLiteral("TN160 discard stream");
}

void Tn160DiscardEngine::reset()
{
}

ParseResult Tn160DiscardEngine::feed(const QByteArrayView buffer)
{
    return {.consumed = buffer.size(), .packets = {}, .needMore = false};
}

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

QString Tn160WriteReplyEngine::id() const
{
    return QString::fromLatin1(Tn160WriteReplySource);
}

QString Tn160WriteReplyEngine::displayName() const
{
    return QStringLiteral("TN160 0xEC write reply");
}

void Tn160WriteReplyEngine::reset()
{
}

ParseResult Tn160WriteReplyEngine::feed(const QByteArrayView buffer)
{
    static const QByteArray marker("TNCW", 4);
    const qsizetype start = buffer.indexOf(QByteArrayView(marker));
    if (start < 0) {
        const qsizetype keep = unmatchedSuffix(buffer, QByteArrayView(marker));
        return {.consumed = buffer.size() - keep,
                .packets = {}, .needMore = keep > 0};
    }
    if (buffer.size() - start < WriteReplyBytes) {
        return {.consumed = start, .packets = {}, .needMore = true};
    }
    const QByteArrayView reply(buffer.data() + start, WriteReplyBytes);
    EventPacket event;
    event.timestampUs = nowUsec();
    event.category = id();
    event.message = QStringLiteral("TN160 write reply");
    event.fields.insert(QStringLiteral("version"),
                        qFromLittleEndian<quint16>(
                            reinterpret_cast<const uchar *>(reply.data() + 4)));
    event.fields.insert(QStringLiteral("status"), quint8(reply.at(6)));
    event.fields.insert(QStringLiteral("detail"), quint8(reply.at(7)));
    event.fields.insert(QStringLiteral("payload_crc"), readLe32(reply, 8));
    event.fields.insert(QStringLiteral("header_crc"), readLe32(reply, 12));
    event.fields.insert(QStringLiteral("flash_header_crc"), readLe32(reply, 16));
    return {.consumed = start + WriteReplyBytes,
            .packets = PacketList{Packet{std::move(event)}}, .needMore = false};
}

Tn160CalibrationStreamEngine::Tn160CalibrationStreamEngine(
    QList<int> payloadSizes)
    : m_payloadSizes(std::move(payloadSizes))
{
    if (m_payloadSizes.isEmpty()) {
        throw std::invalid_argument("calibration payload sizes cannot be empty");
    }
    for (const int size : std::as_const(m_payloadSizes)) {
        if (size <= 0) {
            throw std::invalid_argument("calibration payload size must be positive");
        }
    }
}

QString Tn160CalibrationStreamEngine::id() const
{
    return QString::fromLatin1(Tn160CalibrationStreamSource);
}

QString Tn160CalibrationStreamEngine::displayName() const
{
    return QStringLiteral("TN160 calibration payload stream");
}

void Tn160CalibrationStreamEngine::reset()
{
    m_payloadIndex = 0;
}

ParseResult Tn160CalibrationStreamEngine::feed(const QByteArrayView buffer)
{
    static const QByteArray marker("\xff\xff", 2);
    const qsizetype start = buffer.indexOf(QByteArrayView(marker));
    if (start < 0) {
        const qsizetype keep = unmatchedSuffix(buffer, QByteArrayView(marker));
        return {.consumed = buffer.size() - keep,
                .packets = {}, .needMore = keep > 0};
    }
    if (buffer.size() - start < 3) {
        return {.consumed = start, .packets = {}, .needMore = true};
    }

    const quint8 type = quint8(buffer.at(start + 2));
    if (type == 0xdd) {
        if (m_payloadIndex >= m_payloadSizes.size()) {
            EventPacket event;
            event.timestampUs = nowUsec();
            event.level = QStringLiteral("error");
            event.category = id();
            event.message = QStringLiteral("extra calibration payload");
            event.fields.insert(QStringLiteral("kind"), QStringLiteral("protocol-error"));
            return {.consumed = start + 3,
                    .packets = PacketList{Packet{std::move(event)}},
                    .needMore = false};
        }
        const int payloadSize = m_payloadSizes.at(m_payloadIndex);
        if (buffer.size() - start < 3 + payloadSize) {
            return {.consumed = start, .packets = {}, .needMore = true};
        }
        const int index = m_payloadIndex++;
        ImagePacket packet;
        packet.timestampUs = nowUsec();
        packet.width = index + 1;
        packet.height = 1;
        packet.pixels = QByteArray(buffer.data() + start + 3, payloadSize);
        packet.pixelFormat = QStringLiteral("tn160.calibration-payload.v1");
        packet.source = id();
        return {.consumed = start + 3 + payloadSize,
                .packets = PacketList{Packet{std::move(packet)}},
                .needMore = false};
    }
    if (type == 0xee) {
        if (buffer.size() - start < 5) {
            return {.consumed = start, .packets = {}, .needMore = true};
        }
        const quint8 code = quint8(buffer.at(start + 3));
        const quint8 length = quint8(buffer.at(start + 4));
        if (buffer.size() - start < 5 + length) {
            return {.consumed = start, .packets = {}, .needMore = true};
        }
        EventPacket event;
        event.timestampUs = nowUsec();
        event.category = id();
        event.message = QString::fromLatin1(buffer.data() + start + 5, length);
        event.fields.insert(QStringLiteral("kind"), QStringLiteral("completed"));
        event.fields.insert(QStringLiteral("code"), code);
        event.fields.insert(QStringLiteral("payload_count"), m_payloadIndex);
        event.fields.insert(QStringLiteral("expected_count"), m_payloadSizes.size());
        return {.consumed = start + 5 + length,
                .packets = PacketList{Packet{std::move(event)}},
                .needMore = false};
    }
    return {.consumed = start + 1, .packets = {}, .needMore = false};
}

} // namespace serein::scope::tn160
