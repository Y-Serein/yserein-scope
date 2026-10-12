#include <serein/scope/PluginInterface.h>

using namespace serein::scope;

class TestPlugin final : public QObject, public IDevicePlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "com.serein.scope.IDevicePlugin/2.0" FILE "TestPluginV2.json")
    Q_INTERFACES(serein::scope::IDevicePlugin)

public:
    PluginMetadata metadata() const override
    {
        return {.id = QStringLiteral("scope-test-versioned"),
                .name = QStringLiteral("版本管理测试插件"),
                .version = QStringLiteral("2.0.0")};
    }
    bool initialize(IHostServices *) override { return true; }
    QWidget *createWorkspace(QWidget *) override { return nullptr; }
    bool canUnload() const override { return true; }
    void shutdown() override {}
};

#include "TestPluginV2.moc"
