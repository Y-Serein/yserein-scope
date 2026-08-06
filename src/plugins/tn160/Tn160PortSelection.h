#pragma once

#include <serein/scope/SerialTransport.h>

#include <QtCore/QString>

namespace serein::scope::tn160 {

// CH346 exposes several UART interfaces.  The original TN160 tool identifies
// the monitor interface as the A2 channel.  Select it only when that metadata
// points to one unambiguous port; an arbitrary first COM port is never a TN160
// fallback.
inline int preferredTn160PortIndex(const QList<SerialPortInfo> &ports)
{
    int result = -1;
    for (qsizetype index = 0; index < ports.size(); ++index) {
        const QString description = ports.at(index).description.toUpper();
        const bool isCh346A2 = description.contains(QStringLiteral("CH346"))
            && (description.contains(QStringLiteral("SERIAL-A2"))
                || description.contains(QStringLiteral("SERIAL A2")));
        if (!isCh346A2) {
            continue;
        }
        if (result >= 0) {
            return -1;
        }
        result = int(index);
    }
    return result;
}

} // namespace serein::scope::tn160
