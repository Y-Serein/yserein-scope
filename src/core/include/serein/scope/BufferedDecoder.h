#pragma once

#include "CoreExport.h"
#include "IDataEngine.h"

#include <QtCore/QByteArray>

#include <memory>

namespace serein::scope {

class SEREIN_SCOPE_CORE_EXPORT BufferedDecoder final {
public:
    explicit BufferedDecoder(std::unique_ptr<IDataEngine> engine,
                             qsizetype maxBuffer = 4 * 1024 * 1024);

    [[nodiscard]] const IDataEngine &engine() const noexcept;
    [[nodiscard]] qsizetype pendingBytes() const noexcept;
    [[nodiscard]] PacketList feed(QByteArrayView chunk);
    void reset();

private:
    std::unique_ptr<IDataEngine> m_engine;
    QByteArray m_buffer;
    qsizetype m_maxBuffer;
};

} // namespace serein::scope
