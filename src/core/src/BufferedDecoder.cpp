#include <serein/scope/BufferedDecoder.h>

#include <QtCore/QCoreApplication>

#include <stdexcept>

namespace serein::scope {

BufferedDecoder::BufferedDecoder(std::unique_ptr<IDataEngine> engine,
                                 const qsizetype maxBuffer)
    : m_engine(std::move(engine)), m_maxBuffer(maxBuffer)
{
    if (!m_engine) {
        throw std::invalid_argument("BufferedDecoder requires an engine");
    }
    if (m_maxBuffer <= 0) {
        throw std::invalid_argument("maxBuffer must be positive");
    }
}

const IDataEngine &BufferedDecoder::engine() const noexcept
{
    return *m_engine;
}

qsizetype BufferedDecoder::pendingBytes() const noexcept
{
    return m_buffer.size();
}

PacketList BufferedDecoder::feed(const QByteArrayView chunk)
{
    if (!chunk.isEmpty()) {
        m_buffer.append(chunk.data(), chunk.size());
    }
    if (m_buffer.size() > m_maxBuffer) {
        reset();
        throw std::overflow_error("data engine buffer limit exceeded");
    }

    PacketList packets;
    while (!m_buffer.isEmpty()) {
        const ParseResult result = m_engine->feed(QByteArrayView(m_buffer));
        if (result.consumed < 0 || result.consumed > m_buffer.size()) {
            throw std::runtime_error("data engine returned invalid consumed count");
        }
        packets.append(result.packets);
        if (result.consumed > 0) {
            m_buffer.remove(0, result.consumed);
        }
        if (result.needMore || result.consumed == 0) {
            break;
        }
    }
    return packets;
}

void BufferedDecoder::reset()
{
    m_buffer.clear();
    m_engine->reset();
}

} // namespace serein::scope
