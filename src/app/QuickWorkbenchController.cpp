#include "QuickWorkbenchController.h"

#include <serein/scope/BackendRuntime.h>
#include <serein/scope/SerialTransport.h>

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QMetaObject>
#include <QtCore/QSettings>
#include <QtCore/QTime>
#include <QtCore/QTimer>
#include <QtGui/QGuiApplication>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QStyle>
#include <QtWidgets/QTabBar>
#include <QtWidgets/QWidget>

#include <algorithm>

namespace serein::scope::app {
namespace {

const QString HomeId = QStringLiteral("__home__");
const QString ComponentsId = QStringLiteral("__components__");
const QString LastActivePageKey = QStringLiteral("quick/main/last-active-page-v1");

QString localPath(const QUrl &url)
{
    if (url.isLocalFile()) return url.toLocalFile();
    return url.toString();
}

} // namespace

QuickComponentModel::QuickComponentModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int QuickComponentModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_components.size();
}

QVariant QuickComponentModel::data(const QModelIndex &index, const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_components.size()) {
        return {};
    }
    const ComponentSnapshot &component = m_components.at(index.row());
    switch (role) {
    case IdRole: return component.metadata.id;
    case NameRole: return component.metadata.name;
    case DescriptionRole: return component.metadata.description;
    case VersionRole: return component.metadata.version;
    case CategoryRole: return component.metadata.category;
    case CapabilitiesRole: return component.metadata.capabilities;
    case AccentRole: return component.metadata.accent;
    case LoadedRole: return component.loaded;
    case ErrorRole: return component.error;
    default: return {};
    }
}

QHash<int, QByteArray> QuickComponentModel::roleNames() const
{
    return {
        {IdRole, "componentId"},
        {NameRole, "name"},
        {DescriptionRole, "description"},
        {VersionRole, "version"},
        {CategoryRole, "category"},
        {CapabilitiesRole, "capabilities"},
        {AccentRole, "accent"},
        {LoadedRole, "loaded"},
        {ErrorRole, "errorText"},
    };
}

void QuickComponentModel::replace(QList<ComponentSnapshot> components)
{
    beginResetModel();
    m_components = std::move(components);
    endResetModel();
}

QuickWorkbenchController::QuickWorkbenchController(const bool persistState,
                                                   QObject *parent)
    : QObject(parent), m_componentModel(this)
{
    const QString storePath = componentDirectoryArgument();
    m_components = std::make_unique<ComponentManager>(this, storePath,
                                                       persistState, this);
    connect(m_components.get(), &ComponentManager::componentsChanged,
            this, &QuickWorkbenchController::refreshComponents);
    connect(&BackendRuntime::instance(), &BackendRuntime::runtimeError,
            this, [this](const QString &message) {
        log(QStringLiteral("error"), tr("后台运行错误：%1").arg(message));
        setStatus(tr("后台运行错误 · %1").arg(message));
    });
    prepareComponents();
    restoreInitialPage();
    scanPorts();
    log(QStringLiteral("info"), tr("Serein Scope QML 迁移入口已启动"));
}

QuickWorkbenchController::~QuickWorkbenchController()
{
    const QStringList ids = [&] {
        QStringList result;
        for (const auto &record : m_workspaces) result.append(record->componentId);
        return result;
    }();
    for (const QString &id : ids) closeWorkspace(id);
    if (!m_components) return;
    for (const ComponentSnapshot &component : m_components->components()) {
        if (component.loaded) m_components->unload(component.metadata.id, nullptr, false);
    }
}

QAbstractListModel *QuickWorkbenchController::components() noexcept
{
    return &m_componentModel;
}

QVariantList QuickWorkbenchController::workspaceTabs() const
{
    QVariantList tabs;
    tabs.append(QVariantMap{{QStringLiteral("id"), HomeId},
                            {QStringLiteral("title"), QStringLiteral("Scope")},
                            {QStringLiteral("closable"), false}});
    for (const auto &record : m_workspaces) {
        if (!record->widget) continue;
        const ComponentSnapshot component = m_components->component(record->componentId);
        tabs.append(QVariantMap{{QStringLiteral("id"), record->componentId},
                                {QStringLiteral("title"), component.metadata.name},
                                {QStringLiteral("closable"), true}});
    }
    return tabs;
}

QString QuickWorkbenchController::activePage() const { return m_activePage; }

QWindow *QuickWorkbenchController::workspaceWindow() const
{
    const WorkspaceRecord *record = findWorkspace(m_activePage);
    return record ? record->window : nullptr;
}

QString QuickWorkbenchController::statusText() const { return m_statusText; }
QStringList QuickWorkbenchController::logLines() const { return m_logLines; }
QStringList QuickWorkbenchController::ports() const { return m_ports; }
QString QuickWorkbenchController::selectedPort() const { return m_selectedPort; }
int QuickWorkbenchController::baudRate() const noexcept { return m_baudRate; }
int QuickWorkbenchController::triggerBaudRate() const noexcept { return m_triggerBaudRate; }
int QuickWorkbenchController::readBaudRate() const noexcept { return m_readBaudRate; }
QString QuickWorkbenchController::engineId() const { return m_engineId; }
QString QuickWorkbenchController::connectionState() const { return m_connectionState; }

QString QuickWorkbenchController::connectionAction() const
{
    if (!canConnect()) return tr("连接");
    if (productionCalibrationContext()) {
        return connectionActive() ? tr("断开") : tr("连接");
    }
    if (calibrationContext()) return tn160PageIndex() == 3
        ? tr("读取标定数据") : tr("开始标定");
    if (connectionActive()) return serialWorkspace() ? tr("断开") : tr("停止采集");
    return serialWorkspace() ? tr("连接") : tr("开始采集");
}

bool QuickWorkbenchController::canConnect() const
{
    if (serialWorkspace()) return true;
    const int page = tn160PageIndex();
    return page >= 1 && page <= 4 && connectionTarget();
}

bool QuickWorkbenchController::connectionActive() const
{
    return targetActive(connectionTarget());
}

bool QuickWorkbenchController::serialWorkspace() const
{
    const QWidget *workspace = activeWorkspace();
    return workspace && workspace->objectName() == QStringLiteral("serialScopeWorkspace");
}

bool QuickWorkbenchController::calibrationContext() const
{
    const int page = tn160PageIndex();
    return page == 2 || page == 3;
}

bool QuickWorkbenchController::productionCalibrationContext() const
{
    return tn160PageIndex() == 4;
}

bool QuickWorkbenchController::firmwareContext() const
{
    return tn160PageIndex() == 0;
}

bool QuickWorkbenchController::rawCaptureAvailable() const
{
    return tn160PageIndex() == 1;
}

QString QuickWorkbenchController::activeComponentName() const
{
    if (m_activePage == ComponentsId) return tr("插件管理");
    const ComponentSnapshot component = m_components->component(m_activePage);
    return component.metadata.name.isEmpty() ? tr("宿主概览") : component.metadata.name;
}

QString QuickWorkbenchController::activeComponentDescription() const
{
    if (m_activePage == ComponentsId) {
        return tr("插件包安装在用户目录；只有加载后动态库才进入进程。");
    }
    const ComponentSnapshot component = m_components->component(m_activePage);
    return component.metadata.description.isEmpty()
        ? tr("从插件管理加载一个设备插件。") : component.metadata.description;
}

QString QuickWorkbenchController::activeComponentMeta() const
{
    const ComponentSnapshot component = m_components->component(m_activePage);
    if (component.metadata.id.isEmpty()) return tr("宿主 · C++20 · ABI 2");
    return tr("%1 · v%2 · ABI %3")
        .arg(component.metadata.category.toUpper(), component.metadata.version)
        .arg(component.metadata.interfaceVersion);
}

QString QuickWorkbenchController::activeComponentCapabilities() const
{
    const ComponentSnapshot component = m_components->component(m_activePage);
    return component.metadata.capabilities.isEmpty()
        ? tr("没有活动设备工作区")
        : component.metadata.capabilities.join(QStringLiteral("  ·  "));
}

QString QuickWorkbenchController::centerValue() const { return m_centerValue; }
QString QuickWorkbenchController::ntcValue() const { return m_ntcValue; }
QString QuickWorkbenchController::centerAduValue() const { return m_centerAduValue; }
QString QuickWorkbenchController::vtempValue() const { return m_vtempValue; }
QString QuickWorkbenchController::fpsValue() const { return m_fpsValue; }
bool QuickWorkbenchController::darkMode() const noexcept { return m_darkMode; }
bool QuickWorkbenchController::navigationVisible() const noexcept { return m_navigationVisible; }
bool QuickWorkbenchController::inspectorVisible() const noexcept { return m_inspectorVisible; }
bool QuickWorkbenchController::outputVisible() const noexcept { return m_outputVisible; }
bool QuickWorkbenchController::componentLibraryVisible() const noexcept
{
    return m_componentLibraryVisible;
}

void QuickWorkbenchController::activatePage(const QString &pageId)
{
    if (pageId == ComponentsId) {
        m_activePage = ComponentsId;
        setStatus(tr("插件管理 · 安装与运行时管理"));
    } else if (pageId == HomeId || pageId.isEmpty()) {
        m_activePage = HomeId;
        setStatus(tr("Scope · 就绪"));
    } else {
        if (!findWorkspace(pageId)) {
            static_cast<void>(openComponent(pageId));
            return;
        }
        m_activePage = pageId;
        setStatus(tr("%1 · 工作区运行中")
                      .arg(m_components->component(pageId).metadata.name));
    }
    QSettings().setValue(LastActivePageKey, m_activePage);
    emit activePageChanged();
    emit workspaceWindowChanged();
    emit inspectorChanged();
    refreshConnectionContext();
    if (QWidget *workspace = activeWorkspace()) {
        applyDarkModeToWorkspace(workspace);
    }
}

bool QuickWorkbenchController::openComponent(const QString &componentId)
{
    if (componentId.trimmed().isEmpty()) return false;
    if (WorkspaceRecord *existing = findWorkspace(componentId)) {
        m_activePage = componentId;
        QSettings().setValue(LastActivePageKey, m_activePage);
        setStatus(tr("%1 · 工作区运行中")
                      .arg(m_components->component(componentId).metadata.name));
        emit activePageChanged();
        emit workspaceWindowChanged();
        emit inspectorChanged();
        refreshConnectionContext();
        return existing->widget;
    }

    QString error;
    if (!m_components->isLoaded(componentId)
        && !m_components->load(componentId, &error)) {
        log(QStringLiteral("error"), tr("加载插件失败：%1").arg(error));
        emit operationFailed(tr("插件加载失败"), error);
        return false;
    }
    IDevicePlugin *plugin = m_components->plugin(componentId);
    QWidget *workspace = plugin ? plugin->createWorkspace(nullptr) : nullptr;
    if (!workspace) {
        const QString message = tr("插件未能创建设备工作区");
        log(QStringLiteral("error"), message);
        emit operationFailed(tr("打开失败"), message);
        return false;
    }

    workspace->setWindowFlag(Qt::FramelessWindowHint, true);
    workspace->setAttribute(Qt::WA_NativeWindow, true);
    workspace->resize(1000, 680);
    static_cast<void>(workspace->winId());
    QWindow *nativeWindow = workspace->windowHandle();
    if (!nativeWindow) {
        if (auto *managed = qobject_cast<IDeviceWorkspace *>(workspace)) managed->shutdown();
        delete workspace;
        const QString message = tr("无法为插件工作区创建原生窗口容器");
        emit operationFailed(tr("打开失败"), message);
        return false;
    }
    nativeWindow->setFlags(Qt::FramelessWindowHint);

    auto record = std::make_unique<WorkspaceRecord>();
    record->componentId = componentId;
    record->widget = workspace;
    record->window = nativeWindow;
    m_workspaces.emplace_back(std::move(record));
    connectWorkspaceSignals(workspace);
    applyDarkModeToWorkspace(workspace);

    m_activePage = componentId;
    QSettings().setValue(LastActivePageKey, m_activePage);
    setStatus(tr("%1 · 工作区运行中")
                  .arg(m_components->component(componentId).metadata.name));
    log(QStringLiteral("info"), tr("已创建 QML 宿主工作区：%1")
                                  .arg(m_components->component(componentId).metadata.name));
    emit workspaceTabsChanged();
    emit activePageChanged();
    emit workspaceWindowChanged();
    emit inspectorChanged();
    refreshConnectionContext();
    refreshComponents();
    return true;
}

bool QuickWorkbenchController::closeWorkspace(const QString &componentId)
{
    WorkspaceRecord *record = findWorkspace(componentId);
    if (!record || !record->widget) return true;
    const bool active = m_activePage == componentId;
    if (active) {
        m_activePage = HomeId;
        setStatus(tr("Scope · 就绪"));
        emit activePageChanged();
        emit workspaceWindowChanged();
    }

    QWidget *widget = record->widget;
    record->window.clear();
    if (auto *managed = qobject_cast<IDeviceWorkspace *>(widget)) managed->shutdown();
    widget->hide();
    delete widget;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    std::erase_if(m_workspaces, [&componentId](const auto &candidate) {
        return candidate->componentId == componentId;
    });
    QSettings().setValue(LastActivePageKey, m_activePage);
    log(QStringLiteral("info"), tr("设备工作区已关闭：%1").arg(componentId));
    emit workspaceTabsChanged();
    emit inspectorChanged();
    refreshConnectionContext();
    return true;
}

bool QuickWorkbenchController::unloadComponent(const QString &componentId)
{
    closeWorkspace(componentId);
    QString error;
    if (!m_components->unload(componentId, &error)) {
        emit operationFailed(tr("卸载失败"), error);
        return false;
    }
    refreshComponents();
    return true;
}

bool QuickWorkbenchController::importComponent(const QUrl &sourceUrl)
{
    const QString path = localPath(sourceUrl);
    QString error;
    const QString id = m_components->importPackage(path, &error);
    if (id.isEmpty()) {
        emit operationFailed(tr("导入失败"), error);
        return false;
    }
    refreshComponents();
    return true;
}

void QuickWorkbenchController::scanPorts()
{
    m_ports.clear();
    for (const SerialPortInfo &port : SerialTransport::availablePorts()) {
        m_ports.append(port.portName);
    }
    if (!m_selectedPort.isEmpty() && !m_ports.contains(m_selectedPort)) {
        m_ports.prepend(m_selectedPort);
    }
    if (m_selectedPort.isEmpty() && !m_ports.isEmpty()) m_selectedPort = m_ports.first();
    m_connectionState = m_ports.isEmpty()
        ? tr("未发现串口；可手动输入 COMx")
        : tr("扫描到 %1 个串口").arg(m_ports.size());
    emit connectionContextChanged();
}

void QuickWorkbenchController::toggleConnection()
{
    QObject *target = connectionTarget();
    if (!target || !canConnect()) return;
    const QString port = m_selectedPort.trimmed();
    if (!port.isEmpty()) {
        QMetaObject::invokeMethod(target, "setPort", Qt::DirectConnection,
                                  Q_ARG(QString, port));
    }
    const int page = tn160PageIndex();
    const char *method = nullptr;
    if (serialWorkspace()) {
        QMetaObject::invokeMethod(target, "setBaud", Qt::DirectConnection,
                                  Q_ARG(int, m_baudRate));
        QMetaObject::invokeMethod(target, "setEngine", Qt::DirectConnection,
                                  Q_ARG(QString, m_engineId));
        method = "toggleConnection";
    } else if (page == 1) {
        QMetaObject::invokeMethod(target, "setBaud", Qt::DirectConnection,
                                  Q_ARG(int, m_baudRate));
        method = "toggleCapture";
    } else if (page == 2 || page == 3) {
        QMetaObject::invokeMethod(target, "setTriggerBaud", Qt::DirectConnection,
                                  Q_ARG(int, m_triggerBaudRate));
        QMetaObject::invokeMethod(target, "setReadBaud", Qt::DirectConnection,
                                  Q_ARG(int, m_readBaudRate));
        method = "startTransfer";
    } else if (page == 4) {
        method = "toggleConnection";
    }
    if (method && !QMetaObject::invokeMethod(target, method, Qt::DirectConnection)) {
        emit operationFailed(tr("操作失败"), tr("当前工作区不支持该连接动作"));
    }
}

void QuickWorkbenchController::startRawCapture(const int frameCount)
{
    QObject *target = connectionTarget();
    if (!target || !rawCaptureAvailable()) return;
    QMetaObject::invokeMethod(target, "startRawCapture", Qt::DirectConnection,
                              Q_ARG(int, std::clamp(frameCount, 1, 1000)));
}

void QuickWorkbenchController::chooseFirmware()
{
    QWidget *workspace = activeWorkspace();
    QObject *firmware = workspace
        ? workspace->findChild<QObject *>(QStringLiteral("tn160FirmwarePage")) : nullptr;
    if (firmware) QMetaObject::invokeMethod(firmware, "chooseFirmware", Qt::DirectConnection);
}

void QuickWorkbenchController::toggleFirmwareMonitoring()
{
    QWidget *workspace = activeWorkspace();
    QObject *firmware = workspace
        ? workspace->findChild<QObject *>(QStringLiteral("tn160FirmwarePage")) : nullptr;
    if (firmware) QMetaObject::invokeMethod(firmware, "toggleMonitoring", Qt::DirectConnection);
}

bool QuickWorkbenchController::showLibraryComponent(const QString &componentId)
{
    QWidget *workspace = activeWorkspace();
    if (!workspace || componentId.trimmed().isEmpty()) return false;
    for (QObject *canvas : workspace->findChildren<QObject *>(
             QStringLiteral("workspaceCanvas"))) {
        bool added = false;
        if (QMetaObject::invokeMethod(canvas, "addOrShowComponentAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(bool, added),
                                      Q_ARG(QString, componentId),
                                      Q_ARG(QPoint, QPoint(-1, -1))) && added) {
            setComponentLibraryVisible(false);
            return true;
        }
    }
    log(QStringLiteral("warning"), tr("当前工作区不支持组件：%1").arg(componentId));
    return false;
}

QStringList QuickWorkbenchController::availableLibraryComponents() const
{
    if (serialWorkspace()) return {QStringLiteral("waveform"),
                                   QStringLiteral("channels"),
                                   QStringLiteral("terminal")};
    if (tn160PageIndex() == 1) return {QStringLiteral("thermal")};
    return {};
}

QString QuickWorkbenchController::libraryComponentTitle(const QString &componentId) const
{
    if (componentId == QStringLiteral("waveform")) return tr("实时波形");
    if (componentId == QStringLiteral("channels")) return tr("数据通道");
    if (componentId == QStringLiteral("terminal")) return tr("串口终端");
    if (componentId == QStringLiteral("thermal")) return tr("TN160 热图");
    return componentId;
}

void QuickWorkbenchController::selectTemplate(const int index)
{
    switch (index) {
    case 0:
        m_navigationVisible = true;
        m_componentLibraryVisible = false;
        setStatus(tr("模板组件 1 · 工作区"));
        break;
    case 1:
        m_outputVisible = true;
        setStatus(tr("模板组件 2 · 信号视图"));
        break;
    case 2:
        m_inspectorVisible = true;
        setStatus(tr("模板组件 3 · 层级视图"));
        break;
    case 3:
        m_navigationVisible = true;
        m_componentLibraryVisible = false;
        setStatus(tr("模板组件 4 · 数据容器"));
        break;
    default:
        return;
    }
    emit panelVisibilityChanged();
}

void QuickWorkbenchController::resetPanels()
{
    m_navigationVisible = true;
    m_inspectorVisible = false;
    m_outputVisible = false;
    m_componentLibraryVisible = false;
    setStatus(tr("已恢复默认面板布局"));
    emit panelVisibilityChanged();
}

void QuickWorkbenchController::setSelectedPort(const QString &port)
{
    const QString normalized = port.trimmed();
    if (m_selectedPort == normalized) return;
    m_selectedPort = normalized;
    if (!normalized.isEmpty() && !m_ports.contains(normalized)) m_ports.prepend(normalized);
    emit connectionContextChanged();
}

void QuickWorkbenchController::setBaudRate(const int baudRate)
{
    const int bounded = std::clamp(baudRate, 300, 12000000);
    if (m_baudRate == bounded) return;
    m_baudRate = bounded;
    emit connectionContextChanged();
}

void QuickWorkbenchController::setTriggerBaudRate(const int baudRate)
{
    const int bounded = std::clamp(baudRate, 300, 12000000);
    if (m_triggerBaudRate == bounded) return;
    m_triggerBaudRate = bounded;
    emit connectionContextChanged();
}

void QuickWorkbenchController::setReadBaudRate(const int baudRate)
{
    const int bounded = std::clamp(baudRate, 300, 12000000);
    if (m_readBaudRate == bounded) return;
    m_readBaudRate = bounded;
    emit connectionContextChanged();
}

void QuickWorkbenchController::setEngineId(const QString &engineId)
{
    if (engineId != QStringLiteral("delimited") && engineId != QStringLiteral("raw")) return;
    if (m_engineId == engineId) return;
    m_engineId = engineId;
    emit connectionContextChanged();
}

void QuickWorkbenchController::setDarkMode(const bool enabled)
{
    if (m_darkMode == enabled) return;
    m_darkMode = enabled;
    for (const auto &record : m_workspaces) {
        if (record->widget) applyDarkModeToWorkspace(record->widget);
    }
    setStatus(enabled ? tr("Sleep · 深色材质") : tr("Sleep · 浅色材质"));
    emit darkModeChanged();
}

void QuickWorkbenchController::setNavigationVisible(const bool visible)
{
    if (m_navigationVisible == visible
        && !(visible && m_componentLibraryVisible)) return;
    m_navigationVisible = visible;
    if (visible && m_componentLibraryVisible) m_componentLibraryVisible = false;
    emit panelVisibilityChanged();
}

void QuickWorkbenchController::setInspectorVisible(const bool visible)
{
    setPanelFlag(m_inspectorVisible, visible);
}

void QuickWorkbenchController::setOutputVisible(const bool visible)
{
    setPanelFlag(m_outputVisible, visible);
}

void QuickWorkbenchController::setComponentLibraryVisible(const bool visible)
{
    if (m_componentLibraryVisible == visible) return;
    m_componentLibraryVisible = visible;
    if (visible) m_navigationVisible = false;
    emit panelVisibilityChanged();
}

void QuickWorkbenchController::log(const QString &level, const QString &message)
{
    const QString line = QStringLiteral("%1  %2  %3")
        .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")),
             level.toUpper().leftJustified(7), message);
    m_logLines.append(line);
    constexpr int MaxLogLines = 500;
    while (m_logLines.size() > MaxLogLines) m_logLines.removeFirst();
    emit logLinesChanged();
}

void QuickWorkbenchController::setStatus(const QString &message)
{
    if (m_statusText == message) return;
    m_statusText = message;
    emit statusTextChanged();
}

void QuickWorkbenchController::refreshComponents()
{
    m_componentModel.replace(m_components->components());
}

void QuickWorkbenchController::handleWorkspacePageChanged()
{
    refreshConnectionContext();
    emit inspectorChanged();
}

void QuickWorkbenchController::handleConnectionState(const QString &state,
                                                       const QString &text)
{
    if (sender() && sender() != connectionTarget()) return;
    m_connectionState = text;
    emit connectionContextChanged();
    if (state == QStringLiteral("error")) log(QStringLiteral("error"), text);
}

void QuickWorkbenchController::handleTransferState(const QString &state,
                                                     const QString &text)
{
    if (sender() && sender() != connectionTarget()) return;
    m_connectionState = text;
    emit connectionContextChanged();
    if (state == QStringLiteral("error")) log(QStringLiteral("error"), text);
}

void QuickWorkbenchController::handleRawCaptureState(const QString &state,
                                                      const QString &text)
{
    Q_UNUSED(state)
    if (sender() && sender() != connectionTarget()) return;
    m_connectionState = text;
    emit connectionContextChanged();
}

void QuickWorkbenchController::handleTelemetry(const QString &center, const QString &ntc,
                                                const QString &centerAdu,
                                                const QString &vtemp, const QString &fps)
{
    m_centerValue = center;
    m_ntcValue = ntc;
    m_centerAduValue = centerAdu;
    m_vtempValue = vtemp;
    m_fpsValue = fps;
    emit telemetryChanged();
}

void QuickWorkbenchController::prepareComponents()
{
    m_components->discover();
    const QString packagedPath = ComponentManager::packagedStorePath();
    QDir packagedComponents(packagedPath);
    if (!packagedPath.isEmpty()) {
        for (const QFileInfo &package : packagedComponents.entryInfoList(
                 {QStringLiteral("*.sscomponent")}, QDir::Files, QDir::Name)) {
            QString error;
            if (m_components->importPackage(package.absoluteFilePath(), &error).isEmpty()) {
                log(QStringLiteral("warning"), tr("内置插件包导入失败：%1").arg(error));
            }
        }
    }
    m_components->restorePreferredComponents();
    refreshComponents();
}

void QuickWorkbenchController::restoreInitialPage()
{
    QString initialPage = HomeId;
    bool explicitPage = false;
    for (const QString &argument : QCoreApplication::arguments()) {
        if (argument.startsWith(QStringLiteral("--open="))) {
            initialPage = argument.sliced(QStringLiteral("--open=").size());
            explicitPage = true;
            break;
        }
    }
    if (!explicitPage) {
        initialPage = QSettings().value(LastActivePageKey, HomeId).toString();
    }
    if (initialPage != HomeId && initialPage != ComponentsId
        && !m_components->isLoaded(initialPage)) {
        initialPage = HomeId;
    }
    if (initialPage != HomeId && initialPage != ComponentsId) {
        QTimer::singleShot(0, this, [this, initialPage] { openComponent(initialPage); });
    } else {
        m_activePage = initialPage;
    }
}

void QuickWorkbenchController::connectWorkspaceSignals(QWidget *workspace)
{
    if (auto *tabs = workspace->findChild<QTabBar *>(QStringLiteral("workspaceTabs"))) {
        connect(tabs, &QTabBar::currentChanged,
                this, &QuickWorkbenchController::handleWorkspacePageChanged);
    }
    if (workspace->objectName() == QStringLiteral("serialScopeWorkspace")) {
        connect(workspace, SIGNAL(connectionStateChanged(QString,QString)),
                this, SLOT(handleConnectionState(QString,QString)));
        return;
    }
    QObject *live = workspace->findChild<QObject *>(QStringLiteral("tn160LivePage"));
    if (!live) return;
    connect(live, SIGNAL(connectionStateChanged(QString,QString)),
            this, SLOT(handleConnectionState(QString,QString)));
    connect(live, SIGNAL(rawCaptureStateChanged(QString,QString)),
            this, SLOT(handleRawCaptureState(QString,QString)));
    connect(live, SIGNAL(telemetryChanged(QString,QString,QString,QString,QString)),
            this, SLOT(handleTelemetry(QString,QString,QString,QString,QString)));
    for (const QString &name : {QStringLiteral("tn160CalibrationPage"),
                                QStringLiteral("tn160CalibrationDataPage"),
                                QStringLiteral("tn160ProductionCalibrationPage")}) {
        if (QObject *page = workspace->findChild<QObject *>(name)) {
            connect(page, SIGNAL(transferStateChanged(QString,QString)),
                    this, SLOT(handleTransferState(QString,QString)));
        }
    }
}

void QuickWorkbenchController::refreshConnectionContext()
{
    QObject *target = connectionTarget();
    if (!target) {
        m_connectionState = firmwareContext()
            ? tr("选择 UF2 后监听新出现的 BOOTSEL 卷")
            : tr("当前页面无串口动作");
        emit connectionContextChanged();
        return;
    }
    QMetaObject::invokeMethod(target, "refreshPorts", Qt::DirectConnection);
    QWidget *workspace = activeWorkspace();
    const QString portObject = serialWorkspace() ? QStringLiteral("serialPort")
        : calibrationContext()
            ? (tn160PageIndex() == 3 ? QStringLiteral("tn160CalibrationDataPort")
                                     : QStringLiteral("tn160CalibrationPort"))
            : productionCalibrationContext()
                ? QStringLiteral("tn160ProductionPort")
                : QStringLiteral("tn160Port");
    if (workspace) {
        if (QComboBox *source = workspace->findChild<QComboBox *>(portObject)) {
            QStringList sourcePorts;
            for (int i = 0; i < source->count(); ++i) sourcePorts.append(source->itemText(i));
            m_ports = sourcePorts;
            if (!source->currentText().trimmed().isEmpty()) {
                m_selectedPort = source->currentText().trimmed();
            }
        }
        if (serialWorkspace()) {
            if (QComboBox *engine = workspace->findChild<QComboBox *>(
                    QStringLiteral("serialEngine"))) {
                const QString id = engine->currentData().toString();
                if (!id.isEmpty()) m_engineId = id;
            }
        }
    }
    int currentBaud = 0;
    if (!calibrationContext()
        && QMetaObject::invokeMethod(target, "baudRate", Qt::DirectConnection,
                                     Q_RETURN_ARG(int, currentBaud))
        && currentBaud > 0) {
        m_baudRate = currentBaud;
    }
    m_connectionState = serialWorkspace() ? tr("Serial Scope · 就绪")
        : calibrationContext() ? (tn160PageIndex() == 3
              ? tr("TN160 · 标定数据") : tr("TN160 · 设备标定"))
        : productionCalibrationContext() ? tr("TN160 · 生产标定")
        : tr("TN160 · 实时诊断");
    emit connectionContextChanged();
}

void QuickWorkbenchController::applyDarkModeToWorkspace(QWidget *workspace) const
{
    if (!workspace) return;
    workspace->setProperty("darkMode", m_darkMode);
    for (QWidget *widget : workspace->findChildren<QWidget *>()) {
        widget->setProperty("darkMode", m_darkMode);
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
        widget->update();
    }
    workspace->style()->unpolish(workspace);
    workspace->style()->polish(workspace);
    workspace->update();
}

QuickWorkbenchController::WorkspaceRecord *
QuickWorkbenchController::findWorkspace(const QString &componentId)
{
    for (auto &record : m_workspaces) {
        if (record->componentId == componentId) return record.get();
    }
    return nullptr;
}

const QuickWorkbenchController::WorkspaceRecord *
QuickWorkbenchController::findWorkspace(const QString &componentId) const
{
    for (const auto &record : m_workspaces) {
        if (record->componentId == componentId) return record.get();
    }
    return nullptr;
}

QWidget *QuickWorkbenchController::activeWorkspace() const
{
    const WorkspaceRecord *record = findWorkspace(m_activePage);
    return record ? record->widget : nullptr;
}

QObject *QuickWorkbenchController::connectionTarget() const
{
    QWidget *workspace = activeWorkspace();
    if (!workspace) return nullptr;
    if (serialWorkspace()) return workspace;
    const int page = tn160PageIndex();
    const QString objectName = page == 1 ? QStringLiteral("tn160LivePage")
        : page == 2 ? QStringLiteral("tn160CalibrationPage")
        : page == 3 ? QStringLiteral("tn160CalibrationDataPage")
        : page == 4 ? QStringLiteral("tn160ProductionCalibrationPage") : QString();
    return objectName.isEmpty() ? nullptr : workspace->findChild<QObject *>(objectName);
}

int QuickWorkbenchController::tn160PageIndex() const
{
    QWidget *workspace = activeWorkspace();
    if (!workspace || workspace->objectName() != QStringLiteral("instrumentWorkspace")) {
        return -1;
    }
    int page = -1;
    QMetaObject::invokeMethod(workspace, "currentPageIndex", Qt::DirectConnection,
                              Q_RETURN_ARG(int, page));
    return page;
}

bool QuickWorkbenchController::targetActive(QObject *target) const
{
    if (!target) return false;
    bool active = false;
    const char *method = calibrationContext() ? "transferActive" : "connectionActive";
    return QMetaObject::invokeMethod(target, method, Qt::DirectConnection,
                                     Q_RETURN_ARG(bool, active)) && active;
}

QString QuickWorkbenchController::componentDirectoryArgument() const
{
    for (const QString &argument : QCoreApplication::arguments()) {
        if (argument.startsWith(QStringLiteral("--component-dir="))) {
            return argument.sliced(QStringLiteral("--component-dir=").size());
        }
    }
    return ComponentManager::defaultStorePath();
}

void QuickWorkbenchController::setPanelFlag(bool &target, const bool value)
{
    if (target == value) return;
    target = value;
    emit panelVisibilityChanged();
}

} // namespace serein::scope::app
