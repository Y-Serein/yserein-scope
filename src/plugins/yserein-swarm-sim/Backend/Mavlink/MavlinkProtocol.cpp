#include "MavlinkProtocol.h"

#include <QSettings>
#include <atomic>

namespace zsm {

namespace {
std::atomic<int> g_domain{0};      // 0 = Private16, 1 = Standard8
std::atomic<bool> g_domainSet{false};
} // namespace

static ProtocolDomain domainFromInt(int v) {
    return v == 1 ? ProtocolDomain::Standard8 : ProtocolDomain::Private16;
}

ProtocolDomain currentProtocolDomain() {
    if (!g_domainSet.load(std::memory_order_acquire)) {
        // Not explicitly selected on this run: honor the persisted setting.
        QSettings settings;
        ProtocolDomain d;
        if (parseProtocolDomain(settings.value(QStringLiteral("Mavlink/ProtocolDomain")).toString(), d)) {
            g_domain.store(static_cast<int>(d), std::memory_order_release);
        }
        g_domainSet.store(true, std::memory_order_release);
    }
    return domainFromInt(g_domain.load(std::memory_order_acquire));
}

void setProtocolDomain(ProtocolDomain domain) {
    g_domain.store(static_cast<int>(domain), std::memory_order_release);
    g_domainSet.store(true, std::memory_order_release);
    QSettings settings;
    settings.setValue(QStringLiteral("Mavlink/ProtocolDomain"),
                      domain == ProtocolDomain::Standard8 ? QStringLiteral("standard8") : QStringLiteral("private16"));
}

bool parseProtocolDomain(const QString& value, ProtocolDomain& out) {
    const QString v = value.trimmed().toLower();
    if (v == QStringLiteral("standard8") || v == QStringLiteral("standard-8") || v == QStringLiteral("8")) {
        out = ProtocolDomain::Standard8;
        return true;
    }
    if (v == QStringLiteral("private16") || v == QStringLiteral("private-16") || v == QStringLiteral("16")) {
        out = ProtocolDomain::Private16;
        return true;
    }
    return false;
}

} // namespace zsm
