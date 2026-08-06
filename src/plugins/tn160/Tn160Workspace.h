#pragma once

#include <serein/scope/PluginInterface.h>

#include <QtWidgets/QWidget>

class QStackedWidget;
class QTabBar;

namespace serein::scope::tn160 {

class CalibrationPage;
class FirmwarePage;
class LivePage;

class Tn160Workspace final : public QWidget, public IDeviceWorkspace {
    Q_OBJECT
    Q_INTERFACES(serein::scope::IDeviceWorkspace)

public:
    explicit Tn160Workspace(IHostServices *host, QWidget *parent = nullptr);
    ~Tn160Workspace() override = default;
    void shutdown() override;

    Q_INVOKABLE int currentPageIndex() const;

private slots:
    void selectPage(int index);

private:
    IHostServices *m_host;
    QTabBar *m_tabs = nullptr;
    QStackedWidget *m_stack = nullptr;
    FirmwarePage *m_firmware = nullptr;
    LivePage *m_live = nullptr;
    CalibrationPage *m_calibrate = nullptr;
    CalibrationPage *m_dump = nullptr;
};

} // namespace serein::scope::tn160
