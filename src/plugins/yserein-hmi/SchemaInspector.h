#pragma once

#include "ControlSchema.h"

#include <QtCore/QHash>
#include <QtCore/QJsonObject>
#include <QtWidgets/QWidget>

class QComboBox;
class QDoubleSpinBox;
class QFrame;
class QLabel;
class QLineEdit;
class QSpinBox;
class QCheckBox;
class QPushButton;
class QVBoxLayout;

namespace serein::scope::yserein_hmi {

// A property inspector generated from a ControlSchema trait table. Instead of
// bespoke per-control-type widget code, this widget walks the schema's ordered
// PropertySpec list and materialises a grouped form (Geometry / Content /
// Appearance / Binding / Event). When the user edits a field it emits a keyed
// property change; the owning workspace routes that to the canvas write path.
class SchemaInspector final : public QWidget {
    Q_OBJECT

public:
    explicit SchemaInspector(QWidget *parent = nullptr);

    // Rebuild the form for a control type. Passing a null schema clears the
    // panel to a "nothing selected" state.
    void load(const ControlSchema *schema, const QJsonObject &properties,
              const QString &name);
    void clear();

signals:
    void propertyEdited(const QString &key, const QJsonValue &value);
    void geometryEdited(double x, double y, double width, double height);
    void nameEdited(const QString &name);
    void bindingEdited(const QString &binding);
    void bindingModeEdited(const QString &mode);
    void eventEdited(const QString &eventType, const QString &eventTarget);

private:
    QWidget *buildField(const PropertySpec &spec, QWidget *parent);
    void setSpecValue(const PropertySpec &spec, const QJsonValue &value);
    void connectGeometryEditors();

    QVBoxLayout *m_root = nullptr;
    QLabel *m_selection = nullptr;
    QFrame *m_body = nullptr;
    QHash<QString, QWidget *> m_editors;
    QHash<QString, QPushButton *> m_swatchButtons;
    QHash<QString, QLineEdit *> m_hexEdits;
    bool m_updating = false;
    const ControlSchema *m_schema = nullptr;
};

} // namespace serein::scope::yserein_hmi
