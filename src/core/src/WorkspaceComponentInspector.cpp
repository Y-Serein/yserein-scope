#include <serein/scope/WorkspaceComponentInspector.h>

#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>
#include <utility>

namespace serein::scope {
namespace {

QString bindingKindName(const WorkspaceBindingKind kind)
{
    switch (kind) {
    case WorkspaceBindingKind::SampleChannel: return QStringLiteral("采样通道");
    case WorkspaceBindingKind::ImageChannel: return QStringLiteral("图像通道");
    case WorkspaceBindingKind::EventStream: return QStringLiteral("事件流");
    case WorkspaceBindingKind::TextStream: return QStringLiteral("文本流");
    case WorkspaceBindingKind::Command: return QStringLiteral("命令");
    case WorkspaceBindingKind::None: break;
    }
    return QStringLiteral("未绑定");
}

QList<WorkspaceBindingKind> bindingKinds(const WorkspaceBindingKinds kinds)
{
    QList<WorkspaceBindingKind> result;
    for (const WorkspaceBindingKind kind : {WorkspaceBindingKind::SampleChannel,
                                             WorkspaceBindingKind::ImageChannel,
                                             WorkspaceBindingKind::EventStream,
                                             WorkspaceBindingKind::TextStream,
                                             WorkspaceBindingKind::Command}) {
        if (kinds.testFlag(kind)) result.append(kind);
    }
    return result;
}

double constraintDouble(const QVariantMap &constraints, const QString &key,
                        const double fallback)
{
    const QVariant value = constraints.value(key);
    return value.isValid() ? value.toDouble() : fallback;
}

int constraintInt(const QVariantMap &constraints, const QString &key, const int fallback)
{
    const QVariant value = constraints.value(key);
    return value.isValid() ? value.toInt() : fallback;
}

} // namespace

WorkspaceComponentInspector::WorkspaceComponentInspector(QWidget *parent)
    : QWidget(parent)
{
    qRegisterMetaType<QList<WorkspaceBinding>>();
    qRegisterMetaType<WorkspaceComponentInstance>();
    setObjectName(QStringLiteral("workspaceComponentInspector"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(8);
    m_body = new QWidget(this);
    auto *bodyLayout = new QVBoxLayout(m_body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(8);
    m_propertyLayout = new QFormLayout;
    m_propertyLayout->setContentsMargins(0, 0, 0, 0);
    m_propertyLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    bodyLayout->addLayout(m_propertyLayout);
    m_bindingLayout = new QVBoxLayout;
    m_bindingLayout->setContentsMargins(0, 0, 0, 0);
    m_bindingLayout->setSpacing(6);
    bodyLayout->addLayout(m_bindingLayout);
    root->addWidget(m_body);
    clear();
}

void WorkspaceComponentInspector::setDefinition(WorkspaceComponentDefinition definition)
{
    m_definition = std::move(definition);
    rebuild();
}

void WorkspaceComponentInspector::setInstance(WorkspaceComponentInstance instance)
{
    m_instance = std::move(instance);
    rebuild();
}

void WorkspaceComponentInspector::setState(WorkspaceComponentDefinition definition,
                                            WorkspaceComponentInstance instance)
{
    m_definition = std::move(definition);
    m_instance = std::move(instance);
    rebuild();
}

void WorkspaceComponentInspector::clear()
{
    m_definition = {};
    m_instance = {};
    m_instance.instanceId = QUuid();
    rebuild();
}

const WorkspaceComponentDefinition &WorkspaceComponentInspector::definition() const noexcept
{
    return m_definition;
}

const WorkspaceComponentInstance &WorkspaceComponentInspector::instance() const noexcept
{
    return m_instance;
}

void WorkspaceComponentInspector::rebuild()
{
    if (!m_propertyLayout || !m_bindingLayout) return;
    m_rebuilding = true;
    while (QLayoutItem *item = m_propertyLayout->takeAt(0)) {
        if (QWidget *widget = item->widget()) delete widget;
        delete item;
    }
    while (QLayoutItem *item = m_bindingLayout->takeAt(0)) {
        if (QWidget *widget = item->widget()) delete widget;
        delete item;
    }

    if (m_definition.definitionId.trimmed().isEmpty()) {
        auto *empty = new QLabel(tr("选择画布组件后可编辑属性和绑定"), m_body);
        empty->setObjectName(QStringLiteral("workspaceInspectorEmpty"));
        m_propertyLayout->addRow(empty);
        m_rebuilding = false;
        return;
    }

    auto *title = new QLabel(m_definition.displayName, m_body);
    title->setObjectName(QStringLiteral("workspaceInspectorComponentTitle"));
    m_propertyLayout->addRow(title);
    for (const WorkspaceComponentProperty &schema : m_definition.properties) {
        const QVariant current = m_instance.properties.value(schema.key, schema.defaultValue);
        const QString type = schema.type.trimmed().toCaseFolded();
        if (type == QStringLiteral("bool")) {
            auto *editor = new QCheckBox(m_body);
            editor->setChecked(current.toBool());
            connect(editor, &QCheckBox::toggled, this,
                    [this, key = schema.key](const bool value) {
                        updateProperty(key, value);
                    });
            m_propertyLayout->addRow(schema.title, editor);
        } else if (type == QStringLiteral("int") || type == QStringLiteral("integer")) {
            auto *editor = new QSpinBox(m_body);
            editor->setRange(constraintInt(schema.constraints, QStringLiteral("min"), -1000000),
                             constraintInt(schema.constraints, QStringLiteral("max"), 1000000));
            editor->setValue(current.toInt());
            connect(editor, qOverload<int>(&QSpinBox::valueChanged), this,
                    [this, key = schema.key](const int value) {
                        updateProperty(key, value);
                    });
            m_propertyLayout->addRow(schema.title, editor);
        } else if (type == QStringLiteral("double") || type == QStringLiteral("float")
                   || type == QStringLiteral("number")) {
            auto *editor = new QDoubleSpinBox(m_body);
            editor->setRange(constraintDouble(schema.constraints, QStringLiteral("min"), -1e9),
                             constraintDouble(schema.constraints, QStringLiteral("max"), 1e9));
            editor->setDecimals(constraintInt(schema.constraints, QStringLiteral("decimals"), 3));
            editor->setValue(current.toDouble());
            connect(editor, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                    [this, key = schema.key](const double value) {
                        updateProperty(key, value);
                    });
            m_propertyLayout->addRow(schema.title, editor);
        } else if (type == QStringLiteral("enum")) {
            auto *editor = new QComboBox(m_body);
            const QVariantList options = schema.constraints.value(QStringLiteral("options"))
                                             .toList();
            for (const QVariant &option : options) editor->addItem(option.toString(), option);
            const int index = editor->findData(current);
            if (index >= 0) editor->setCurrentIndex(index);
            connect(editor, qOverload<int>(&QComboBox::currentIndexChanged), this,
                    [this, key = schema.key, editor](const int index) {
                        if (index >= 0) updateProperty(key, editor->itemData(index));
                    });
            m_propertyLayout->addRow(schema.title, editor);
        } else {
            auto *editor = new QLineEdit(current.toString(), m_body);
            connect(editor, &QLineEdit::textChanged, this,
                    [this, key = schema.key](const QString &value) {
                        updateProperty(key, value);
                    });
            m_propertyLayout->addRow(schema.title, editor);
        }
    }

    const QList<WorkspaceBindingKind> allowedKinds = bindingKinds(m_definition.bindingKinds);
    if (!allowedKinds.isEmpty()) {
        auto *group = new QGroupBox(tr("数据绑定"), m_body);
        auto *groupLayout = new QVBoxLayout(group);
        groupLayout->setContentsMargins(8, 8, 8, 8);
        groupLayout->setSpacing(6);
        for (int index = 0; index < m_instance.bindings.size(); ++index) {
            const WorkspaceBinding binding = m_instance.bindings.at(index);
            auto *row = new QWidget(group);
            auto *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 0, 0, 0);
            auto *kind = new QComboBox(row);
            for (const WorkspaceBindingKind candidate : allowedKinds) {
                kind->addItem(bindingKindName(candidate), static_cast<quint32>(candidate));
            }
            const int kindIndex = kind->findData(static_cast<quint32>(binding.kind));
            if (kindIndex >= 0) kind->setCurrentIndex(kindIndex);
            auto *channel = new QLineEdit(binding.channelId, row);
            channel->setPlaceholderText(tr("通道 ID"));
            auto *parameter = new QLineEdit(binding.parameter, row);
            parameter->setPlaceholderText(tr("参数（可选）"));
            rowLayout->addWidget(kind);
            rowLayout->addWidget(channel, 1);
            rowLayout->addWidget(parameter, 1);
            groupLayout->addWidget(row);
            const auto updateBindingFromEditors = [this, index, kind, channel, parameter] {
                WorkspaceBinding next;
                next.kind = static_cast<WorkspaceBindingKind>(kind->currentData().toUInt());
                next.channelId = channel->text();
                next.parameter = parameter->text();
                updateBinding(index, std::move(next));
            };
            connect(kind, qOverload<int>(&QComboBox::currentIndexChanged), this,
                    updateBindingFromEditors);
            connect(channel, &QLineEdit::textChanged, this, updateBindingFromEditors);
            connect(parameter, &QLineEdit::textChanged, this, updateBindingFromEditors);
        }
        auto *add = new QPushButton(tr("添加绑定"), group);
        add->setProperty("kind", QStringLiteral("quiet"));
        connect(add, &QPushButton::clicked, this, &WorkspaceComponentInspector::addBinding);
        groupLayout->addWidget(add);
        m_bindingLayout->addWidget(group);
    }
    m_rebuilding = false;
}

void WorkspaceComponentInspector::updateProperty(const QString &key,
                                                 const QVariant &value)
{
    if (m_rebuilding || key.trimmed().isEmpty()) return;
    m_instance.properties.insert(key, value);
    emit propertyEdited(key, value);
    emitStateChanged();
}

void WorkspaceComponentInspector::updateBinding(const int index, WorkspaceBinding binding)
{
    if (m_rebuilding || index < 0 || index >= m_instance.bindings.size()) return;
    m_instance.bindings[index] = std::move(binding);
    emit bindingEdited(m_instance.bindings);
    emitStateChanged();
}

void WorkspaceComponentInspector::addBinding()
{
    const QList<WorkspaceBindingKind> allowedKinds = bindingKinds(m_definition.bindingKinds);
    if (allowedKinds.isEmpty()) return;
    m_instance.bindings.append(WorkspaceBinding{allowedKinds.first(), {}, {}});
    rebuild();
    emit bindingEdited(m_instance.bindings);
    emitStateChanged();
}

void WorkspaceComponentInspector::emitStateChanged()
{
    if (!m_rebuilding) emit stateChanged(m_instance);
}

} // namespace serein::scope
