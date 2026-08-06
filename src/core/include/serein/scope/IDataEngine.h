#pragma once

#include "CoreExport.h"
#include "Packet.h"

#include <QtCore/QByteArrayView>
#include <QtCore/QString>

namespace serein::scope {

struct ParseResult {
    qsizetype consumed = 0;
    PacketList packets;
    bool needMore = false;
};

class SEREIN_SCOPE_CORE_EXPORT IDataEngine {
public:
    virtual ~IDataEngine();
    [[nodiscard]] virtual QString id() const = 0;
    [[nodiscard]] virtual QString displayName() const = 0;
    virtual void reset() = 0;
    [[nodiscard]] virtual ParseResult feed(QByteArrayView buffer) = 0;
};

} // namespace serein::scope
