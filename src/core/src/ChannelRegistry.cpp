#include <serein/scope/ChannelRegistry.h>

#include <QtCore/QRegularExpression>

#include <utility>

namespace serein::scope {
namespace {

void setError(QString *error, const QString &message)
{
    if (error) *error = message;
}

QString normalizedSource(const QString &source, const QString &fallback)
{
    static const QRegularExpression invalidCharacters(
        QStringLiteral("[^A-Za-z0-9_.-]+"));
    QString value = source.trimmed();
    if (value.isEmpty()) value = fallback;
    value.replace(invalidCharacters, QStringLiteral("-"));
    return value.isEmpty() ? fallback : value;
}

} // namespace

ChannelRegistry::ChannelRegistry(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<WorkspaceChannelDescriptor>();
    qRegisterMetaType<WorkspaceChannelSnapshot>();
}

QList<WorkspaceChannelDescriptor> ChannelRegistry::channels() const
{
    QList<WorkspaceChannelDescriptor> result;
    result.reserve(m_order.size());
    for (const QString &id : m_order) {
        const auto iterator = m_values.constFind(id);
        if (iterator != m_values.cend()) result.append(iterator->descriptor);
    }
    return result;
}

WorkspaceChannelDescriptor ChannelRegistry::channel(const QString &channelId) const
{
    const auto iterator = m_values.constFind(channelId.trimmed());
    return iterator == m_values.cend() ? WorkspaceChannelDescriptor{}
                                       : iterator->descriptor;
}

WorkspaceChannelSnapshot ChannelRegistry::snapshot(const QString &channelId) const
{
    return m_values.value(channelId.trimmed());
}

QList<WorkspaceChannelSnapshot> ChannelRegistry::snapshots() const
{
    QList<WorkspaceChannelSnapshot> result;
    result.reserve(m_order.size());
    for (const QString &id : m_order) {
        const auto iterator = m_values.constFind(id);
        if (iterator != m_values.cend()) result.append(iterator.value());
    }
    return result;
}

bool ChannelRegistry::contains(const QString &channelId) const
{
    return m_values.contains(channelId.trimmed());
}

bool ChannelRegistry::publish(const Packet &packet, QString *error)
{
    return std::visit([this, error](const auto &value) {
        using Value = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Value, SamplePacket>) {
            return publishSample(value, error);
        } else if constexpr (std::is_same_v<Value, ImagePacket>) {
            return publishImage(value, error);
        } else if constexpr (std::is_same_v<Value, EventPacket>) {
            return publishEvent(value, error);
        } else if constexpr (std::is_same_v<Value, TextPacket>) {
            return publishText(value, error);
        } else {
            return false;
        }
    }, packet);
}

bool ChannelRegistry::publishSample(const SamplePacket &packet, QString *error)
{
    if (packet.values.isEmpty()) {
        setError(error, QStringLiteral("采样包没有可发布的通道"));
        return false;
    }
    bool descriptorsChanged = false;
    QStringList changedIds;
    const QString source = sourceId(packet.source, QStringLiteral("sample"));
    for (qsizetype index = 0; index < packet.values.size(); ++index) {
        const QString id = sampleChannelId(source, index);
        QString title;
        if (index < packet.labels.size()) title = packet.labels.at(index).trimmed();
        if (title.isEmpty()) title = QStringLiteral("CH%1").arg(index + 1);
        WorkspaceChannelSnapshot value;
        value.descriptor = {
            id, title, WorkspaceBindingKind::SampleChannel, QStringLiteral("number"),
            packet.source, {{QStringLiteral("index"), index}, {QStringLiteral("label"), title}},
        };
        value.timestampUs = packet.timestampUs;
        value.value = packet.values.at(index);
        value.valid = true;
        upsert(std::move(value), &descriptorsChanged);
        changedIds.append(id);
    }
    if (descriptorsChanged) emit channelsChanged();
    if (!changedIds.isEmpty()) emit valuesChanged(changedIds);
    emit samplePublished(packet);
    return true;
}

bool ChannelRegistry::publishImage(const ImagePacket &packet, QString *error)
{
    const qint64 pixelCount = qint64(packet.width) * qint64(packet.height);
    if (packet.width <= 0 || packet.height <= 0
        || pixelCount <= 0 || packet.pixels.size() < pixelCount) {
        setError(error, QStringLiteral("图像包尺寸或像素数据无效"));
        return false;
    }
    const QString id = sourceId(packet.source, QStringLiteral("image"))
        + QStringLiteral("/image");
    WorkspaceChannelSnapshot value;
    value.descriptor = {
        id, QStringLiteral("%1 图像").arg(packet.source.trimmed().isEmpty()
                                             ? QStringLiteral("实时") : packet.source),
        WorkspaceBindingKind::ImageChannel, QStringLiteral("image"), packet.source,
        {{QStringLiteral("width"), packet.width}, {QStringLiteral("height"), packet.height},
         {QStringLiteral("pixelFormat"), packet.pixelFormat}},
    };
    value.timestampUs = packet.timestampUs;
    value.value = packet.pixels;
    value.valid = true;
    bool descriptorsChanged = false;
    upsert(std::move(value), &descriptorsChanged);
    if (descriptorsChanged) emit channelsChanged();
    emit valuesChanged({id});
    emit imagePublished(packet);
    return true;
}

bool ChannelRegistry::publishEvent(const EventPacket &packet, QString *error)
{
    Q_UNUSED(error)
    QString category = packet.category.trimmed();
    if (category.isEmpty()) category = QStringLiteral("general");
    const QString id = sourceId(category, QStringLiteral("event")) + QStringLiteral("/event");
    WorkspaceChannelSnapshot value;
    value.descriptor = {
        id, category, WorkspaceBindingKind::EventStream, QStringLiteral("event"),
        category, {{QStringLiteral("level"), packet.level}},
    };
    value.timestampUs = packet.timestampUs;
    value.value = packet.message;
    value.valid = true;
    bool descriptorsChanged = false;
    upsert(std::move(value), &descriptorsChanged);
    if (descriptorsChanged) emit channelsChanged();
    emit valuesChanged({id});
    emit eventPublished(packet);
    return true;
}

bool ChannelRegistry::publishText(const TextPacket &packet, QString *error)
{
    Q_UNUSED(error)
    QString stream = packet.stream.trimmed();
    if (stream.isEmpty()) stream = QStringLiteral("rx");
    const QString id = sourceId(stream, QStringLiteral("text")) + QStringLiteral("/text");
    WorkspaceChannelSnapshot value;
    value.descriptor = {
        id, stream, WorkspaceBindingKind::TextStream, QStringLiteral("text"), stream, {},
    };
    value.timestampUs = packet.timestampUs;
    value.value = packet.text;
    value.valid = true;
    bool descriptorsChanged = false;
    upsert(std::move(value), &descriptorsChanged);
    if (descriptorsChanged) emit channelsChanged();
    emit valuesChanged({id});
    emit textPublished(packet);
    return true;
}

void ChannelRegistry::clear()
{
    if (m_values.isEmpty()) return;
    const QStringList clearedIds = m_order;
    m_values.clear();
    m_order.clear();
    emit channelsChanged();
    emit valuesChanged(clearedIds);
}

QString ChannelRegistry::sampleChannelId(const QString &source, const qsizetype index)
{
    return source + QStringLiteral("/sample/%1").arg(index + 1);
}

QString ChannelRegistry::sourceId(const QString &source, const QString &fallback)
{
    return normalizedSource(source, fallback);
}

void ChannelRegistry::upsert(WorkspaceChannelSnapshot snapshotValue,
                             bool *channelsChangedValue)
{
    const QString id = snapshotValue.descriptor.channelId.trimmed();
    if (id.isEmpty()) return;
    snapshotValue.descriptor.channelId = id;
    const auto iterator = m_values.find(id);
    if (iterator == m_values.end()) {
        m_order.append(id);
        m_values.insert(id, std::move(snapshotValue));
        if (channelsChangedValue) *channelsChangedValue = true;
        return;
    }
    const bool descriptorChanged = iterator->descriptor.title
        != snapshotValue.descriptor.title
        || iterator->descriptor.source != snapshotValue.descriptor.source
        || iterator->descriptor.metadata != snapshotValue.descriptor.metadata;
    *iterator = std::move(snapshotValue);
    if (descriptorChanged && channelsChangedValue) *channelsChangedValue = true;
}

} // namespace serein::scope
