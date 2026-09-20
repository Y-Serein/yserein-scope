#pragma once

#include "CoreExport.h"
#include "WorkspaceComponents.h"

#include <QtWidgets/QWidget>

class QFormLayout;
class QVBoxLayout;

namespace serein::scope {

// Host-owned editor for a component definition/instance pair. It renders
// only the declared property schema and binding kinds; it never creates a
// dock, changes canvas geometry or reaches into a plugin's private widgets.
class SEREIN_SCOPE_CORE_EXPORT WorkspaceComponentInspector final : public QWidget {
    Q_OBJECT

public:
    explicit WorkspaceComponentInspector(QWidget *parent = nullptr);

    void setDefinition(WorkspaceComponentDefinition definition);
    void setInstance(WorkspaceComponentInstance instance);
    void setState(WorkspaceComponentDefinition definition,
                  WorkspaceComponentInstance instance);
    void clear();

    [[nodiscard]] const WorkspaceComponentDefinition &definition() const noexcept;
    [[nodiscard]] const WorkspaceComponentInstance &instance() const noexcept;

signals:
    void propertyEdited(const QString &key, const QVariant &value);
    void bindingEdited(const QList<serein::scope::WorkspaceBinding> &bindings);
    void stateChanged(const serein::scope::WorkspaceComponentInstance &instance);

private:
    void rebuild();
    void updateProperty(const QString &key, const QVariant &value);
    void updateBinding(int index, WorkspaceBinding binding);
    void addBinding();
    void emitStateChanged();

    WorkspaceComponentDefinition m_definition;
    WorkspaceComponentInstance m_instance;
    QWidget *m_body = nullptr;
    QFormLayout *m_propertyLayout = nullptr;
    QVBoxLayout *m_bindingLayout = nullptr;
    bool m_rebuilding = false;
};

} // namespace serein::scope
