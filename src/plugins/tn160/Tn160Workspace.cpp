#include "Tn160Workspace.h"
#include "CalibrationPage.h"
#include "FirmwarePage.h"
#include "LivePage.h"
#include "ProductionCalibrationPage.h"

#include <QtCore/QCoreApplication>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QTabBar>
#include <QtWidgets/QVBoxLayout>

#include <utility>

namespace serein::scope::tn160 {
namespace {

struct PageInfo {
    QString title;
    QString description;
};

const QList<PageInfo> Pages{
    {QObject::tr("固件烧录"), QObject::tr("批量写入 UF2，并保护系统盘与监听前已存在的卷")},
    {QObject::tr("实时诊断"), QObject::tr("Center 5×5、热图、板载 NTC、原始量与分轴趋势")},
    {QObject::tr("设备标定（工程）"), QObject::tr("旧 0xCC 单帧工程入口；量产请使用生产标定")},
    {QObject::tr("标定数据"), QObject::tr("只读检查背景、Gain、坏点和写入一致性")},
    {QObject::tr("生产标定"), QObject::tr("正式量产入口：32 帧两点 Gain、最终单点 Offset、V1 写入回读与完整导出")},
};

} // namespace

Tn160Workspace::Tn160Workspace(IHostServices *host, QWidget *parent)
    : QWidget(parent), m_host(host)
{
    setObjectName(QStringLiteral("instrumentWorkspace"));
    setProperty("shellRole", QStringLiteral("workspace-content"));
    auto *root = new QVBoxLayout(this);
    // The host center surface owns the shared horizontal inset. Keeping a
    // second plugin-level inset makes TN160 sit farther in than the output
    // dock whenever the side panels are hidden.
    root->setContentsMargins(0, 6, 0, 0);
    root->setSpacing(8);

    auto *tabsFrame = new QFrame(this);
    tabsFrame->setObjectName(QStringLiteral("functionTabsComponent"));
    tabsFrame->setFixedHeight(40);
    auto *tabsLayout = new QHBoxLayout(tabsFrame);
    tabsLayout->setContentsMargins(5, 2, 5, 2);
    tabsLayout->setSpacing(0);

    m_tabs = new QTabBar(tabsFrame);
    m_tabs->setObjectName(QStringLiteral("workspaceTabs"));
    m_tabs->setDrawBase(false);
    m_tabs->setExpanding(false);
    for (const PageInfo &page : Pages) {
        const int tab = m_tabs->addTab(page.title);
        m_tabs->setTabToolTip(tab, page.description);
    }
    tabsLayout->addWidget(m_tabs);
    root->addWidget(tabsFrame);

    m_stack = new QStackedWidget;
    m_stack->setObjectName(QStringLiteral("tnPageStack"));
    m_stack->setProperty("shellRole", QStringLiteral("workspace-stack"));
    m_firmware = new FirmwarePage(host);
    m_firmware->setObjectName(QStringLiteral("tn160FirmwarePage"));
    m_live = new LivePage(host);
    m_live->setObjectName(QStringLiteral("tn160LivePage"));
    m_calibrate = new CalibrationPage(CalibrationTransfer::Mode::Calibrate, host);
    m_calibrate->setObjectName(QStringLiteral("tn160CalibrationPage"));
    m_dump = new CalibrationPage(CalibrationTransfer::Mode::Dump, host);
    m_dump->setObjectName(QStringLiteral("tn160CalibrationDataPage"));
    m_production = new ProductionCalibrationPage(host);
    m_production->setObjectName(QStringLiteral("tn160ProductionCalibrationPage"));
    m_stack->addWidget(m_firmware);
    m_stack->addWidget(m_live);
    m_stack->addWidget(m_calibrate);
    m_stack->addWidget(m_dump);
    m_stack->addWidget(m_production);
    m_uiProvider = new WorkspaceUiProvider(this);
    root->addWidget(m_stack, 1);
    int initialPage = 0;
    for (const QString &argument : QCoreApplication::arguments()) {
        if (argument.startsWith(QStringLiteral("--tn-page="))) {
            bool ok = false;
            const int requested = argument.sliced(QStringLiteral("--tn-page=").size()).toInt(&ok);
            if (ok && requested >= 0 && requested < Pages.size()) initialPage = requested;
        }
    }
    m_tabs->setCurrentIndex(initialPage);
    connect(m_tabs, &QTabBar::currentChanged, this, &Tn160Workspace::selectPage);
    selectPage(initialPage);
    if (m_host) m_host->log(QStringLiteral("info"), tr("TN160 原生 C++ 插件已加载"));
}

void Tn160Workspace::shutdown()
{
    m_firmware->shutdown();
    m_live->shutdown();
    m_calibrate->shutdown();
    m_dump->shutdown();
    m_production->shutdown();
}

WorkspaceUiProvider *Tn160Workspace::workspaceUiProvider()
{
    return m_uiProvider;
}

QList<WorkspaceComponentDefinition> Tn160Workspace::componentDefinitions() const
{
    return m_live && currentPageIndex() == 1 ? m_live->componentDefinitions()
                  : QList<WorkspaceComponentDefinition>{};
}

QWidget *Tn160Workspace::createComponent(const QString &definitionId,
                                         QWidget *parent)
{
    return m_live && currentPageIndex() == 1
        ? m_live->createComponent(definitionId, parent) : nullptr;
}

void Tn160Workspace::selectPage(const int index)
{
    if (index < 0 || index >= Pages.size()) return;
    const int previousIndex = m_stack ? m_stack->currentIndex() : -1;
    if (previousIndex != index) {
        if (m_uiProvider) m_uiProvider->prepareContextChange();
        emit pageAboutToChange(previousIndex, index);
    }
    // QStackedWidget otherwise reserves the largest minimum-size hint from
    // every hidden calibration/firmware page.  The live heatmap can safely
    // yield that space to the host trend output, while the form-heavy pages
    // retain their normal minimum-size contract.
    m_stack->setSizePolicy(QSizePolicy::Ignored,
                           index == 1 ? QSizePolicy::Ignored
                                      : QSizePolicy::Preferred);
    m_stack->setCurrentIndex(index);
    m_stack->updateGeometry();
    WorkspaceUiContext uiContext;
    switch (index) {
    case 0:
        uiContext.id = QStringLiteral("tn160/firmware");
        uiContext.layoutProfile = WorkspaceLayoutProfile::FirmwareTask;
        uiContext.contextPanel = {m_firmware->contextPanel(), tr("固件烧录"), nullptr, {}};
        break;
    case 1:
        uiContext.id = QStringLiteral("tn160/live");
        uiContext.layoutProfile = WorkspaceLayoutProfile::Diagnostics;
        uiContext.contextPanel = {m_live->contextPanel(), tr("设备连接"), nullptr, {}};
        uiContext.inspectorPanel = {m_live->inspectorPanel(), tr("实时数据"), nullptr, {}};
        uiContext.outputPanel = {m_live->outputPanel(), tr("实时趋势"), m_live,
                                 QByteArrayLiteral("clearDiagnosticSession")};
        uiContext.componentLibrary.items = {};
        uiContext.componentLibrary.activationTarget = m_live;
        uiContext.componentLibrary.activationMethod =
            QByteArrayLiteral("activateLibraryComponent");
        break;
    case 2:
        uiContext.id = QStringLiteral("tn160/engineering-calibration");
        uiContext.layoutProfile = WorkspaceLayoutProfile::CompactTask;
        uiContext.contextPanel = {m_calibrate->contextPanel(), tr("工程标定"), nullptr, {}};
        break;
    case 3:
        uiContext.id = QStringLiteral("tn160/calibration-data");
        uiContext.contextPanel = {m_dump->contextPanel(), tr("标定数据"), nullptr, {}};
        break;
    case 4:
        uiContext.id = QStringLiteral("tn160/production-calibration");
        uiContext.layoutProfile = WorkspaceLayoutProfile::CompactTask;
        uiContext.contextPanel = {m_production->contextPanel(), tr("生产标定"), nullptr, {}};
        break;
    default:
        break;
    }
    if (m_uiProvider) m_uiProvider->setContext(std::move(uiContext));
    if (m_host) m_host->setStatus(tr("TN160 · %1").arg(Pages[index].title));
}

int Tn160Workspace::currentPageIndex() const
{
    return m_tabs ? m_tabs->currentIndex() : -1;
}

} // namespace serein::scope::tn160
