#pragma once

#include <serein/scope/PluginInterface.h>

#include <QtWidgets/QFrame>

namespace serein::scope::app {

class PluginCard final : public QFrame {
    Q_OBJECT

public:
    explicit PluginCard(PluginMetadata metadata, bool loaded, bool workspaceOpen,
                        bool managementActions, QWidget *parent = nullptr);

signals:
    void primaryRequested(const QString &pluginId);
    void unloadRequested(const QString &pluginId);
    void removeRequested(const QString &pluginId);

private:
    PluginMetadata m_metadata;
};

} // namespace serein::scope::app
