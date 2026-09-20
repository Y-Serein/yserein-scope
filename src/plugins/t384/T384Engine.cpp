#include "T384Engine.h"

#include <QtCore/QDateTime>
#include <QtCore/QtEndian>

#include <cmath>
#include <limits>

namespace serein::scope::t384 {
namespace {

constexpr qsizetype HeaderBytes = 36;
constexpr quint32 WireMagic = 0x31523354u; // T3R1, little-endian on the wire.
constexpr quint16 WireVersion = 1;
constexpr quint16 PixelFormatY16Be = 2;
constexpr quint16 FlagFrameStart = 0x0001;
constexpr quint16 FlagFrameEnd = 0x0002;
constexpr quint16 FlagDataModeMask = 0x0030;
constexpr quint16 FlagTpdY16 = 0x0010;
constexpr quint16 Supported256Width = 256;
constexpr quint16 Supported256Height = 192;
constexpr quint16 Supported384Width = 384;
constexpr quint16 Supported384Height = 288;
constexpr quint16 Supported640Width = 640;
constexpr quint16 Supported640Height = 512;

struct ChunkHeader {
    quint32 sequence = 0;
    quint32 offset = 0;
    quint32 frameSize = 0;
    quint32 captureMs = 0;
    quint16 payloadSize = 0;
    quint16 width = 0;
    quint16 height = 0;
    quint16 flags = 0;
    quint16 pixelFormat = 0;
};

template<typename T>
T readLe(const char *data)
{
    return qFromLittleEndian<T>(reinterpret_cast<const uchar *>(data));
}

quint16 crc16Xmodem(const char *data, const qsizetype length)
{
    quint16 crc = 0;
    for (qsizetype i = 0; i < length; ++i) {
        crc ^= quint16(quint8(data[i])) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000u) != 0u
                    ? quint16((crc << 1) ^ 0x1021u)
                    : quint16(crc << 1);
        }
    }
    return crc;
}

bool supportedDimensions(const quint16 width, const quint16 height)
{
    return (width == Supported256Width && height == Supported256Height)
        || (width == Supported384Width && height == Supported384Height)
        || (width == Supported640Width && height == Supported640Height);
}

bool decodeHeader(const char *data, ChunkHeader &header)
{
    if (readLe<quint32>(data) != WireMagic
        || readLe<quint16>(data + 4) != WireVersion
        || readLe<quint16>(data + 6) != HeaderBytes
        || crc16Xmodem(data, HeaderBytes - 2) != readLe<quint16>(data + 34)) {
        return false;
    }
    header.sequence = readLe<quint32>(data + 8);
    header.offset = readLe<quint32>(data + 12);
    header.frameSize = readLe<quint32>(data + 16);
    header.captureMs = readLe<quint32>(data + 20);
    header.payloadSize = readLe<quint16>(data + 24);
    header.width = readLe<quint16>(data + 26);
    header.height = readLe<quint16>(data + 28);
    header.flags = readLe<quint16>(data + 30);
    header.pixelFormat = readLe<quint16>(data + 32);

    const quint32 expectedFrameSize = quint32(header.width) * header.height * 2u;
    return supportedDimensions(header.width, header.height)
        && header.frameSize == expectedFrameSize
        && header.payloadSize != 0u
        && header.offset <= header.frameSize
        && header.payloadSize <= header.frameSize - header.offset
        && header.pixelFormat == PixelFormatY16Be
        && (header.flags & FlagDataModeMask) == FlagTpdY16;
}

qsizetype findMagic(QByteArrayView buffer, const qsizetype from)
{
    for (qsizetype i = from; i + 4 <= buffer.size(); ++i) {
        if (quint8(buffer.at(i)) == 'T' && quint8(buffer.at(i + 1)) == '3'
            && quint8(buffer.at(i + 2)) == 'R' && quint8(buffer.at(i + 3)) == '1') {
            return i;
        }
    }
    return -1;
}

} // namespace

QString T384Engine::id() const
{
    return QString::fromLatin1(T384Raw16Source);
}

QString T384Engine::displayName() const
{
    return QStringLiteral("T256/T384/T640 RAW16 frame chunks");
}

void T384Engine::reset()
{
    discardPendingFrame();
}

void T384Engine::setExperimentalModel(const bool enabled, const double zeroRaw,
                                      const double countsPerC)
{
    m_experimentalModelEnabled = enabled;
    m_zeroRaw = zeroRaw;
    m_countsPerC = countsPerC;
}

void T384Engine::discardPendingFrame()
{
    m_pending = {};
}

bool T384Engine::hasPendingFrame() const
{
    return !m_pending.y16be.isEmpty();
}

ParseResult T384Engine::feed(const QByteArrayView buffer)
{
    qsizetype start = findMagic(buffer, 0);
    if (start < 0) {
        // Retain at most three bytes so a split wire magic can be completed next time.
        return {.consumed = qMax<qsizetype>(0, buffer.size() - 3), .packets = {},
                .needMore = buffer.size() > 0};
    }

    while (start >= 0) {
        if (buffer.size() - start < HeaderBytes) {
            return {.consumed = start, .packets = {}, .needMore = true};
        }

        ChunkHeader header;
        if (!decodeHeader(buffer.data() + start, header)) {
            discardPendingFrame();
            start = findMagic(buffer, start + 1);
            continue;
        }
        const qsizetype chunkSize = HeaderBytes + header.payloadSize;
        if (buffer.size() - start < chunkSize) {
            return {.consumed = start, .packets = {}, .needMore = true};
        }

        const bool isStart = (header.flags & FlagFrameStart) != 0;
        const bool isEnd = (header.flags & FlagFrameEnd) != 0;
        bool accepted = false;
        if (isStart && header.offset == 0u) {
            discardPendingFrame();
            m_pending.sequence = header.sequence;
            m_pending.captureMs = header.captureMs;
            m_pending.frameSize = header.frameSize;
            m_pending.width = header.width;
            m_pending.height = header.height;
            m_pending.flags = header.flags;
            m_pending.pixelFormat = header.pixelFormat;
            m_pending.nextOffset = 0;
            m_pending.y16be.reserve(qsizetype(header.frameSize));
            accepted = true;
        } else if (hasPendingFrame()
                   && header.sequence == m_pending.sequence
                   && header.captureMs == m_pending.captureMs
                   && header.frameSize == m_pending.frameSize
                   && header.width == m_pending.width
                   && header.height == m_pending.height
                   && header.pixelFormat == m_pending.pixelFormat
                   && header.offset == m_pending.nextOffset) {
            accepted = true;
        }

        if (!accepted) {
            // A malformed, duplicate, or out-of-order chunk invalidates the whole frame.
            discardPendingFrame();
            start = findMagic(buffer, start + 1);
            continue;
        }

        m_pending.y16be.append(buffer.data() + start + HeaderBytes, header.payloadSize);
        m_pending.nextOffset += header.payloadSize;
        if (!isEnd) {
            start += chunkSize;
            continue;
        }

        if (m_pending.nextOffset != m_pending.frameSize) {
            discardPendingFrame();
            start = findMagic(buffer, start + 1);
            continue;
        }

        const qint64 timestampUs = QDateTime::currentMSecsSinceEpoch() * 1000;
        QByteArray gray8;
        gray8.resize(m_pending.width * m_pending.height);
        quint16 minimum = 0xffffu;
        quint16 maximum = 0u;
        for (int pixel = 0; pixel < m_pending.width * m_pending.height; ++pixel) {
            const quint16 raw = (quint16(quint8(m_pending.y16be[pixel * 2])) << 8)
                | quint8(m_pending.y16be[pixel * 2 + 1]);
            minimum = qMin(minimum, raw);
            maximum = qMax(maximum, raw);
        }
        const quint32 span = qMax<quint32>(1u, quint32(maximum) - minimum);
        quint64 roiSum = 0;
        const int roiX = (m_pending.width - 16) / 2;
        const int roiY = (m_pending.height - 16) / 2;
        for (int y = 0; y < m_pending.height; ++y) {
            for (int x = 0; x < m_pending.width; ++x) {
                const int pixel = y * m_pending.width + x;
                const quint16 raw = (quint16(quint8(m_pending.y16be[pixel * 2])) << 8)
                    | quint8(m_pending.y16be[pixel * 2 + 1]);
                const quint32 level = (quint32(raw - minimum) * 255u
                                       + span / 2u) / span;
                gray8[pixel] = char(level);
                if (x >= roiX && x < roiX + 16 && y >= roiY && y < roiY + 16) {
                    roiSum += raw;
                }
            }
        }
        const double rawMean = double(roiSum) / 256.0;
        const double experimentalC = m_experimentalModelEnabled
                && std::isfinite(m_zeroRaw) && std::isfinite(m_countsPerC)
                && m_countsPerC > 0.0
            ? (rawMean - m_zeroRaw) / m_countsPerC
            : std::numeric_limits<double>::quiet_NaN();
        PacketList packets;
        packets.append(ImagePacket{timestampUs, m_pending.width, m_pending.height,
                                   m_pending.y16be, QStringLiteral("y16be"),
                                   QString::fromLatin1(T384Raw16Source)});
        packets.append(ImagePacket{timestampUs, m_pending.width, m_pending.height,
                                   gray8, QStringLiteral("gray8"),
                                   QString::fromLatin1(T384MonitorSource)});
        packets.append(SamplePacket{timestampUs,
                                    {experimentalC, rawMean, double(m_pending.sequence),
                                     double(m_pending.captureMs)},
                                    {QStringLiteral("Center 16×16 experimental °C"),
                                     QStringLiteral("Center 16×16 Y16 mean"),
                                     QStringLiteral("Frame sequence"),
                                     QStringLiteral("Capture ms")},
                                    QString::fromLatin1(T384MonitorSource)});
        discardPendingFrame();
        return {.consumed = start + chunkSize, .packets = packets, .needMore = false};
    }

    return {.consumed = qMax<qsizetype>(0, buffer.size() - 3), .packets = {},
            .needMore = buffer.size() > 0};
}

} // namespace serein::scope::t384
