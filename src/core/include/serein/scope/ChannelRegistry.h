#pragma once

#include "CoreExport.h"
#include "Packet.h"
#include "WorkspaceComponents.h"

#include <QtCore/QHash>
#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVariant>

namespace serein::scope {

struct SEREIN_SCOPE_CORE_EXPORT WorkspaceChannelDescriptor {
    QString channelId;
    QString title;
    WorkspaceBindingKind kind = WorkspaceBindingKind::None;
    QString valueType;
    QString source;
    QVariantMap metadata;
};

struct SEREIN_SCOPE_CORE_EXPORT WorkspaceChannelSnapshot {
    WorkspaceChannelDescriptor descriptor;
    qint64 timestampUs = 0;
    QVariant value;
    bool valid = false;
};

// Protocol-neutral projection of decoded packets.  The registry deliberately
// keeps only the latest value per channel; it is not a recording buffer and
// therefore cannot grow with session duration.
class SEREIN_SCOPE_CORE_EXPORT ChannelRegistry final : public QObject {
    Q_OBJECT

public:
    explicit ChannelRegistry(QObject *parent = nullptr);

    [[nodiscard]] QList<WorkspaceChannelDescriptor> channels() const;
    [[nodiscard]] WorkspaceChannelDescriptor channel(const QString &channelId) const;
    [[nodiscard]] WorkspaceChannelSnapshot snapshot(const QString &channelId) const;
    [[nodiscard]] QList<WorkspaceChannelSnapshot> snapshots() const;
    [[nodiscard]] bool contains(const QString &channelId) const;

    // Publishes one decoded packet and updates the corresponding latest-value
    // snapshots.  Packet ownership remains with the caller.
    bool publish(const Packet &packet, QString *error = nullptr);
    bool publishSample(const SamplePacket &packet, QString *error = nullptr);
    bool publishImage(const ImagePacket &packet, QString *error = nullptr);
    bool publishEvent(const EventPacket &packet, QString *error = nullptr);
    bool publishText(const TextPacket &packet, QString *error = nullptr);
    void clear();

signals:
    void channelsChanged();
    void samplePublished(const serein::scope::SamplePacket &packet);
    void imagePublished(const serein::scope::ImagePacket &packet);
    void eventPublished(const serein::scope::EventPacket &packet);
    void textPublished(const serein::scope::TextPacket &packet);
    void valuesChanged(const QStringList &channelIds);

private:
    static QString sampleChannelId(const QString &source, qsizetype index);
    static QString sourceId(const QString &source, const QString &fallback);
    void upsert(WorkspaceChannelSnapshot snapshot, bool *channelsChanged);

    QStringList m_order;
    QHash<QString, WorkspaceChannelSnapshot> m_values;
};

} // namespace serein::scope

Q_DECLARE_METATYPE(serein::scope::WorkspaceChannelDescriptor)
Q_DECLARE_METATYPE(serein::scope::WorkspaceChannelSnapshot)

