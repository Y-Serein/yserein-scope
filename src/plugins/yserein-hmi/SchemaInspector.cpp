#include "SchemaInspector.h"

#include <QtCore/QSignalBlocker>
#include <QtCore/QVariant>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QColorDialog>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QVBoxLayout>

#include <utility>

namespace serein::scope::yserein_hmi {

namespace {

QString groupTitle(PropertyGroup group)
{
    switch (group) {
    case PropertyGroup::Geometry:
        return QStringLiteral("几何");
    case PropertyGroup::Content:
        return QStringLiteral("内容");
    case PropertyGroup::Appearance:
        return QStringLiteral("外观");
    case PropertyGroup::Binding:
        return QStringLiteral("绑定");
    case PropertyGroup::Event:
        return QStringLiteral("事件");
    }
    return QString();
}

QLabel *makeSectionTitle(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("hmiSectionTitle"));
    return label;
}

} // namespace

SchemaInspector::SchemaInspector(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("hmiSchemaInspector"));
    m_root = new QVBoxLayout(this);
    m_root->setContentsMargins(0, 0, 0, 0);
    m_root->setSpacing(8);

    auto *header = new QHBoxLayout;
    header->addWidget(makeSectionTitle(tr("选中控件"), this));
    m_selection = new QLabel(tr("未选中"), this);
    m_selection->setObjectName(QStringLiteral("hmiSelectionBadge"));
    header->addWidget(m_selection, 0, Qt::AlignRight);
    m_root->addLayout(header);

    m_body = new QFrame(this);
    m_body->setObjectName(QStringLiteral("hmiInspectorBody"));
    auto *bodyLayout = new QVBoxLayout(m_body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(6);
    m_root->addWidget(m_body, 1);

    clear();
}

void SchemaInspector::clear()
{
    auto *bodyLayout = qobject_cast<QVBoxLayout *>(m_body->layout());
    if (!bodyLayout) return;

    // Tear down the previous dynamic form.
    while (QLayoutItem *item = bodyLayout->takeAt(0)) {
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
    m_editors.clear();
    m_swatchButtons.clear();
    m_hexEdits.clear();
    m_schema = nullptr;

    auto *placeholder = new QLabel(tr("画布中未选中控件"), m_body);
    placeholder->setObjectName(QStringLiteral("hmiSubtle"));
    bodyLayout->addWidget(placeholder);
    bodyLayout->addStretch(1);
}

void SchemaInspector::load(const ControlSchema *schema, const QJsonObject &properties,
                           const QString &name)
{
    Q_UNUSED(name);
    auto *bodyLayout = qobject_cast<QVBoxLayout *>(m_body->layout());
    if (!bodyLayout) return;

    if (!schema) {
        clear();
        return;
    }

    m_schema = schema;
    m_selection->setText(schema->displayName);

    // Tear down the previous dynamic form.
    while (QLayoutItem *item = bodyLayout->takeAt(0)) {
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
    m_editors.clear();
    m_swatchButtons.clear();
    m_hexEdits.clear();

    // The workspace keeps control-level rows (name / geometry / visible /
    // locked) and the event rows in its own shared controls; this widget is
    // responsible for the schema-described property surface: content,
    // appearance and binding.
    const int groups[] = {
        static_cast<int>(PropertyGroup::Content),
        static_cast<int>(PropertyGroup::Appearance),
    };

    for (int groupValue : groups) {
        const auto group = static_cast<PropertyGroup>(groupValue);
        QVector<PropertySpec> members;
        for (const PropertySpec &spec : schema->specs) {
            if (spec.group == group) members.append(spec);
        }
        if (members.isEmpty()) continue;

        bodyLayout->addWidget(makeSectionTitle(groupTitle(group), m_body));
        auto *groupFrame = new QFrame(m_body);
        groupFrame->setObjectName(QStringLiteral("hmiFieldGroup"));
        auto *form = new QFormLayout(groupFrame);
        form->setContentsMargins(8, 8, 8, 8);
        form->setSpacing(6);

        for (const PropertySpec &spec : members) {
            QWidget *field = buildField(spec, groupFrame);
            form->addRow(spec.label, field);
            const QJsonValue value = properties.contains(spec.key)
                ? properties.value(spec.key)
                : spec.defaultValue;
            setSpecValue(spec, value);
        }
        bodyLayout->addWidget(groupFrame);
    }

    // Geometry editors update the whole rect rather than a single key.
    connectGeometryEditors();

    bodyLayout->addStretch(1);
}

void SchemaInspector::connectGeometryEditors()
{
    auto *x = qobject_cast<QSpinBox *>(m_editors.value(QStringLiteral("x"), nullptr));
    auto *y = qobject_cast<QSpinBox *>(m_editors.value(QStringLiteral("y"), nullptr));
    auto *width = qobject_cast<QSpinBox *>(m_editors.value(QStringLiteral("width"), nullptr));
    auto *height = qobject_cast<QSpinBox *>(m_editors.value(QStringLiteral("height"), nullptr));
    if (!x || !y || !width || !height) return;

    const auto emitRect = [this, x, y, width, height] {
        if (m_updating) return;
        emit geometryEdited(double(x->value()), double(y->value()),
                            double(width->value()), double(height->value()));
    };
    connect(x, qOverload<int>(&QSpinBox::valueChanged), this, emitRect);
    connect(y, qOverload<int>(&QSpinBox::valueChanged), this, emitRect);
    connect(width, qOverload<int>(&QSpinBox::valueChanged), this, emitRect);
    connect(height, qOverload<int>(&QSpinBox::valueChanged), this, emitRect);
}

QWidget *SchemaInspector::buildField(const PropertySpec &spec, QWidget *parent)
{
    const bool geometry = spec.group == PropertyGroup::Geometry;
    switch (spec.editor) {
    case PropertyEditorKind::IntegerSpinbox: {
        auto *spin = new QSpinBox(parent);
        spin->setObjectName(QStringLiteral("hmiPropertyField"));
        spin->setRange(static_cast<int>(spec.minValue),
                       static_cast<int>(spec.maxValue));
        spin->setSingleStep(static_cast<int>(spec.step));
        if (!geometry) {
            connect(spin, qOverload<int>(&QSpinBox::valueChanged), this,
                    [this, key = spec.key](const int value) {
                        if (!m_updating) emit propertyEdited(key, QJsonValue(value));
                    });
        }
        m_editors.insert(spec.key, spin);
        return spin;
    }
    case PropertyEditorKind::DoubleSpinbox: {
        auto *spin = new QDoubleSpinBox(parent);
        spin->setObjectName(QStringLiteral("hmiPropertyField"));
        spin->setRange(spec.minValue, spec.maxValue);
        spin->setDecimals(spec.decimals);
        spin->setSingleStep(spec.step);
        spin->setKeyboardTracking(false);
        connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [this, key = spec.key](const double value) {
                    if (!m_updating) emit propertyEdited(key, QJsonValue(value));
                });
        m_editors.insert(spec.key, spin);
        return spin;
    }
    case PropertyEditorKind::LineEdit: {
        auto *edit = new QLineEdit(parent);
        edit->setObjectName(QStringLiteral("hmiPropertyField"));
        edit->setMaxLength(spec.maxLength);
        connect(edit, &QLineEdit::editingFinished, this,
                [this, key = spec.key, edit] {
                    if (!m_updating) emit propertyEdited(key, QJsonValue(edit->text()));
                });
        m_editors.insert(spec.key, edit);
        return edit;
    }
    case PropertyEditorKind::ComboBox: {
        auto *combo = new QComboBox(parent);
        combo->setObjectName(QStringLiteral("hmiPropertyField"));
        for (const PropertyChoice &choice : spec.choices) {
            combo->addItem(choice.label, choice.value.toVariant());
        }
        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this, key = spec.key, combo](const int index) {
                    if (m_updating) return;
                    const QVariant data = combo->itemData(index);
                    emit propertyEdited(key, QJsonValue::fromVariant(data));
                });
        m_editors.insert(spec.key, combo);
        return combo;
    }
    case PropertyEditorKind::CheckBox: {
        auto *check = new QCheckBox(parent);
        check->setObjectName(QStringLiteral("hmiPropertyField"));
        connect(check, &QCheckBox::toggled, this, [this, key = spec.key](const bool value) {
            if (!m_updating) emit propertyEdited(key, QJsonValue(value));
        });
        m_editors.insert(spec.key, check);
        return check;
    }
    case PropertyEditorKind::Color: {
        // A color swatch button beside the hex line edit; clicking the swatch
        // opens the platform colour dialog. Both paths emit a normalised
        // #AARRGGBB string so the canvas propColor() lookup stays uniform.
        auto *container = new QWidget(parent);
        auto *swatch = new QPushButton(container);
        swatch->setObjectName(QStringLiteral("hmiColorSwatch"));
        swatch->setFixedSize(30, 22);
        swatch->setCursor(Qt::PointingHandCursor);
        swatch->setToolTip(tr("点击选择颜色"));
        auto *edit = new QLineEdit(container);
        edit->setObjectName(QStringLiteral("hmiPropertyField"));
        edit->setMaxLength(spec.maxLength > 0 ? spec.maxLength : 9);
        auto *row = new QHBoxLayout(container);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(4);
        row->addWidget(swatch);
        row->addWidget(edit, 1);
        m_swatchButtons.insert(spec.key, swatch);
        m_hexEdits.insert(spec.key, edit);

        const auto emitColor = [this, key = spec.key, edit] {
            if (m_updating) return;
            const QColor parsed(edit->text());
            if (parsed.isValid())
                emit propertyEdited(key, QJsonValue(parsed.name(QColor::HexArgb)));
        };
        connect(edit, &QLineEdit::editingFinished, this, emitColor);
        connect(swatch, &QPushButton::clicked, this, [this, key = spec.key, edit, swatch] {
            QColor current(edit->text());
            if (!current.isValid()) current = QColor(Qt::white);
            const QColor chosen = QColorDialog::getColor(current, this, tr("选择颜色"));
            if (!chosen.isValid()) return;
            edit->setText(chosen.name(QColor::HexArgb));
            QPalette pal = swatch->palette();
            pal.setColor(QPalette::Button, chosen);
            swatch->setPalette(pal);
            if (!m_updating) emit propertyEdited(key, QJsonValue(chosen.name(QColor::HexArgb)));
        });
        m_editors.insert(spec.key, container);
        return container;
    }
    }
    auto *fallback = new QLineEdit(parent);
    fallback->setObjectName(QStringLiteral("hmiPropertyField"));
    m_editors.insert(spec.key, fallback);
    return fallback;
}

void SchemaInspector::setSpecValue(const PropertySpec &spec, const QJsonValue &value)
{
    QWidget *widget = m_editors.value(spec.key, nullptr);
    if (!widget) return;

    const QSignalBlocker blocker(widget);
    switch (spec.editor) {
    case PropertyEditorKind::IntegerSpinbox:
        qobject_cast<QSpinBox *>(widget)->setValue(value.toInt());
        break;
    case PropertyEditorKind::DoubleSpinbox:
        qobject_cast<QDoubleSpinBox *>(widget)->setValue(value.toDouble());
        break;
    case PropertyEditorKind::LineEdit:
        qobject_cast<QLineEdit *>(widget)->setText(value.toString());
        break;
    case PropertyEditorKind::ComboBox: {
        auto *combo = qobject_cast<QComboBox *>(widget);
        const int index = combo->findData(value.toVariant());
        combo->setCurrentIndex(index >= 0 ? index : 0);
        break;
    }
    case PropertyEditorKind::CheckBox:
        qobject_cast<QCheckBox *>(widget)->setChecked(value.toBool());
        break;
    case PropertyEditorKind::Color: {
        QLineEdit *edit = m_hexEdits.value(spec.key, nullptr);
        QPushButton *swatch = m_swatchButtons.value(spec.key, nullptr);
        if (edit) edit->setText(value.toString());
        if (swatch) {
            const QColor parsed(value.toString());
            QPalette pal = swatch->palette();
            pal.setColor(QPalette::Button, parsed.isValid() ? parsed : QColor(Qt::white));
            swatch->setPalette(pal);
        }
        break;
    }
    }
}

} // namespace serein::scope::yserein_hmi
