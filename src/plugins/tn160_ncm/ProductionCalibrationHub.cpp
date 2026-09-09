#include "ProductionCalibrationHub.h"

#include "ProductionCalibrationPage.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QVariantMap>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QVBoxLayout>

#include <utility>

namespace serein::scope::tn160ncm {
namespace {

constexpr int ProvisioningSegment = 10;
constexpr int FirstStationSegment = 11;
constexpr int LastStationSegment = 16;
constexpr int ProductSegment = 17;
constexpr int ScanRoundDeadlineMs = 1400;
constexpr int DiscoveryRetryMs = 500;
constexpr int DiscoveryWindowMs = 30000;
constexpr int ProductScanDeadlineMs = 600;

QString shortSerial(const QString &serial)
{
    return serial.size() > 8 ? serial.right(8) : serial;
}

} // namespace

ProductionCalibrationHub::ProductionCalibrationHub(IHostServices *host,
                                                   QWidget *parent)
    : QWidget(parent), m_host(host), m_scanHttp(this), m_configHttp(this)
{
    setObjectName(QStringLiteral("tn160ProductionCalibrationPage"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(8);
    m_stationScroll = new QScrollArea(this);
    m_stationScroll->setObjectName(
        QStringLiteral("tn160ProductionStationScroll"));
    m_stationScroll->setWidgetResizable(true);
    m_stationScroll->setFrameShape(QFrame::NoFrame);
    m_stationScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_stationScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_stationList = new QWidget(m_stationScroll);
    m_stationList->setObjectName(QStringLiteral("tn160ProductionStationList"));
    auto *stationListLayout = new QVBoxLayout(m_stationList);
    stationListLayout->setContentsMargins(4, 4, 4, 4);
    stationListLayout->setSpacing(8);
    m_emptyState = new QLabel(
        tr("尚未发现生产设备，请点击右侧“扫描设备”"), m_stationList);
    m_emptyState->setObjectName(QStringLiteral("emptyState"));
    m_emptyState->setAlignment(Qt::AlignCenter);
    stationListLayout->addWidget(m_emptyState);
    m_stationScroll->setWidget(m_stationList);
    root->addWidget(m_stationScroll, 1);

    m_contextPanel = new QWidget(this);
    m_contextPanel->setObjectName(QStringLiteral("workspaceContextContent"));
    auto *contextLayout = new QVBoxLayout(m_contextPanel);
    contextLayout->setContentsMargins(0, 0, 0, 0);
    contextLayout->setSpacing(8);
    contextLayout->addWidget(new QLabel(tr("USB NCM 地址"), m_contextPanel));
    m_manualPort = new QComboBox(m_contextPanel);
    m_manualPort->setEditable(true);
    m_manualPort->setObjectName(QStringLiteral("tn160ProductionManualPort"));
    m_manualPort->addItems({QStringLiteral("http://192.168.10.1"),
                            QStringLiteral("http://192.168.17.1")});
    contextLayout->addWidget(m_manualPort);
    auto *stationRow = new QHBoxLayout;
    m_scanButton = new QPushButton(tr("扫描设备"), m_contextPanel);
    m_scanButton->setObjectName(QStringLiteral("tn160ProductionScan"));
    m_manualConnectButton = new QPushButton(tr("连接"), m_contextPanel);
    m_manualConnectButton->setObjectName(
        QStringLiteral("tn160ProductionManualConnect"));
    m_manualConnectButton->setProperty("kind", QStringLiteral("primary"));
    stationRow->addWidget(m_scanButton);
    stationRow->addWidget(m_manualConnectButton, 1);
    contextLayout->addLayout(stationRow);
    m_discoveryStatus = new QLabel(
        tr("每次只接入一台地址为 10 的新设备"), m_contextPanel);
    m_discoveryStatus->setObjectName(QStringLiteral("tn160ProductionDiscovery"));
    m_discoveryStatus->setWordWrap(true);
    m_discoveryStatus->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_discoveryStatus->setFixedHeight(58);
    contextLayout->addWidget(m_discoveryStatus);
    auto *recordLabel = new QLabel(tr("生成记录"), m_contextPanel);
    recordLabel->setObjectName(QStringLiteral("tn160ProductionRecordLabel"));
    contextLayout->addWidget(recordLabel);
    m_contextStack = new QStackedWidget(m_contextPanel);
    QSizePolicy contextStackPolicy(QSizePolicy::Ignored,
                                   QSizePolicy::Expanding);
    m_contextStack->setSizePolicy(contextStackPolicy);
    contextLayout->addWidget(m_contextStack, 1);

    m_outputPanel = new QWidget(this);
    m_outputPanel->setObjectName(
        QStringLiteral("tn160ProductionResultsPanel"));
    auto *outputLayout = new QVBoxLayout(m_outputPanel);
    outputLayout->setContentsMargins(0, 0, 0, 0);
    outputLayout->setSpacing(0);
    m_outputStack = new QStackedWidget(m_outputPanel);
    QSizePolicy outputStackPolicy(QSizePolicy::Expanding,
                                  QSizePolicy::Ignored);
    outputStackPolicy.setRetainSizeWhenHidden(true);
    m_outputStack->setSizePolicy(outputStackPolicy);
    outputLayout->addWidget(m_outputStack, 1);

    for (int segment = FirstStationSegment;
         segment <= LastStationSegment; ++segment) {
        auto *page = new ProductionCalibrationPage(m_host, m_stationList);
        page->setObjectName(
            QStringLiteral("tn160ProductionStationPage%1")
                .arg(segment - FirstStationSegment + 1));
        page->prepareForStation(segment);
        page->hide();
        m_pages.append(page);
        stationListLayout->addWidget(page);
        page->contextPanel()->setObjectName(
            QStringLiteral("tn160ProductionStationContext%1")
                .arg(segment - FirstStationSegment + 1));
        page->outputPanel()->setObjectName(
            QStringLiteral("tn160ProductionStationOutput%1")
                .arg(segment - FirstStationSegment + 1));
        m_contextStack->addWidget(page->contextPanel());
        m_outputStack->addWidget(page->outputPanel());

        connect(page, &ProductionCalibrationPage::workspaceActivated,
                this, [this](ProductionCalibrationPage *activatedPage) {
                    selectStation(pageIndex(activatedPage));
                });
        connect(page, &ProductionCalibrationPage::livePacketsDecoded,
                this, [this, page](const PacketList &packets) {
                    if (page == m_singleDevicePage) emit livePacketsDecoded(packets);
                });
        connect(page, &ProductionCalibrationPage::workflowStateChanged,
                this, [this, page](const QVariantMap &state) {
                    if (page == m_singleDevicePage) emit workflowStateChanged(state);
                });
        connect(page, &ProductionCalibrationPage::continuousRecordingChanged,
                this, [this, page](const bool active, const QString &path) {
                    if (page == m_singleDevicePage) emit continuousRecordingChanged(active, path);
                });
        connect(page, &ProductionCalibrationPage::calibrationCompleted,
                this, [this, page](const QString &serial, const bool productionPass,
                                   const QString &sessionPath) {
                    if (page == m_singleDevicePage) {
                        emit calibrationCompleted(serial, productionPass, sessionPath);
                    }
                });

        connect(page, &ProductionCalibrationPage::deviceIdentified,
                this, [this](ProductionCalibrationPage *connectedPage,
                             const QString &serial, const int segment) {
                    setStationVisible(connectedPage, true);
                    updateStationLabel(connectedPage, tr("确认首帧"));
                    if (connectedPage == m_pendingPage
                        && segment == m_pendingSegment
                        && serial == m_pendingSerial) {
                        const bool productRecalibration =
                            m_pendingProductRecalibration;
                        const int station =
                            m_pendingSegment - FirstStationSegment + 1;
                        clearPendingAssignment();
                        if (productRecalibration) {
                            m_productSerial.clear();
                            m_productMisses = 0;
                        }
                        m_discoveryStatus->setText(
                            tr("工位 %1 的设备身份已确认，正在验证首帧")
                                .arg(station));
                        grantNextFinalization();
                    }
                });
        connect(page, &ProductionCalibrationPage::deviceConnected,
                this, [this](ProductionCalibrationPage *connectedPage,
                             const QString &, const int) {
                    setStationVisible(connectedPage, true);
                    updateStationLabel(connectedPage, tr("已接入"));
                    m_discoveryStatus->setText(
                        tr("设备身份与首个合法监控帧已确认"));
                });
        connect(page, &ProductionCalibrationPage::deviceDisconnected,
                this, [this](ProductionCalibrationPage *disconnectedPage,
                             const QString &, int) {
                    updateStationLabel(disconnectedPage, tr("等待重连"));
                });
        connect(page, &ProductionCalibrationPage::finalizationRequested,
                this, [this](ProductionCalibrationPage *requestedPage) {
                    if (!requestedPage
                        || m_finalizationQueue.contains(requestedPage)
                        || m_finalizingPage == requestedPage) {
                        return;
                    }
                    if (requestedPage->stationSegment() == ProductSegment) {
                        updateStationLabel(requestedPage,
                                           tr("等待重新标定"));
                        m_discoveryStatus->setText(
                            tr("请先点击“重新标定”分配临时工位"));
                        return;
                    }
                    m_finalizationQueue.append(requestedPage);
                    updateStationLabel(requestedPage, tr("等待完成"));
                    scanProductAddress();
                });
        connect(page,
                &ProductionCalibrationPage::productRecalibrationRequested,
                this,
                &ProductionCalibrationHub::reassignProductForRecalibration);
        connect(page,
                &ProductionCalibrationPage::finalProductReadyForRemoval,
                this, [this](ProductionCalibrationPage *readyPage,
                             const QString &serial) {
                    m_productSerial = serial;
                    m_productMisses = 0;
                    updateStationLabel(readyPage, tr("请拔出"));
                    m_discoveryStatus->setText(
                        tr("设备已完成并切换到 192.168.17.1，请拔出"));
                });
        connect(page, &ProductionCalibrationPage::finalProductRemoved,
                this, [this](ProductionCalibrationPage *removedPage,
                             const QString &) {
                    m_productSerial.clear();
                    m_productMisses = 0;
                    const int segment = removedPage
                        ? pageIndex(removedPage) + FirstStationSegment : 0;
                    if (removedPage && segment >= FirstStationSegment
                        && segment <= LastStationSegment) {
                        removedPage->prepareForStation(segment);
                        updateStationLabel(removedPage, tr("等待设备"));
                        setStationVisible(removedPage, false);
                    }
                    if (m_finalizingPage == removedPage) {
                        m_finalizingPage.clear();
                    }
                    m_discoveryStatus->setText(
                        tr("设备已拔出，可以继续完成下一台"));
                    grantNextFinalization();
                    QTimer::singleShot(0, this,
                                       &ProductionCalibrationHub::beginDeviceScan);
                });
        connect(page, &ProductionCalibrationPage::finalizationFailed,
                this, [this](ProductionCalibrationPage *failedPage,
                             const QString &message) {
                    updateStationLabel(failedPage, tr("完成失败"));
                    if (m_finalizingPage == failedPage) {
                        if (failedPage
                            && failedPage->productAddressCommitted()) {
                            m_productSerial = failedPage->deviceSerial();
                            m_productMisses = 0;
                            m_discoveryStatus->setText(
                                tr("%1；设备可能仍占用 192.168.17.1，"
                                   "请先拔出再重新打开生产标定")
                                    .arg(message));
                            return;
                        }
                        m_finalizingPage.clear();
                    }
                    m_discoveryStatus->setText(message);
                    beginDeviceScan();
                });
    }
    stationListLayout->addStretch(1);

    connect(m_scanButton, &QPushButton::clicked,
            this, &ProductionCalibrationHub::scanDevices);
    connect(m_manualConnectButton, &QPushButton::clicked,
            this, &ProductionCalibrationHub::connectManualEndpoint);
    connect(m_manualPort, &QComboBox::editTextChanged,
            this, &ProductionCalibrationHub::manualEndpointChanged);
    connect(&m_scanHttp, &HttpRequestClient::completed,
            this, [this](const HttpRequestClient::RequestId requestId,
                         int, const QByteArray &body, const QString &error) {
                const auto it = m_scanRequests.find(requestId);
                if (it == m_scanRequests.end()) return;
                const int requestedSegment = it.value();
                m_scanRequests.erase(it);
                if (error.isEmpty()) {
                    DeviceInfo device = parseDevice(body);
                    if (!device.serial.isEmpty()
                        && device.segment == requestedSegment) {
                        m_discoveryFoundAny = true;
                        m_scanResults.insert(requestedSegment,
                                             std::move(device));
                    }
                }
                if (m_scanRequests.isEmpty()) finishScan();
            });
    connect(&m_configHttp, &HttpRequestClient::completed,
            this, [this](const HttpRequestClient::RequestId requestId,
                         const int statusCode, const QByteArray &body,
                         const QString &error) {
                if (requestId != m_configRequest) return;
                m_configRequest = 0;
                const QJsonObject result =
                    QJsonDocument::fromJson(body).object();
                const bool accepted =
                    error.isEmpty() && statusCode == 202
                    && result.value(QStringLiteral("accepted")).toBool();
                if (!accepted) {
                    const bool outcomeUnknown =
                        !error.isEmpty() || statusCode == 0;
                    if (outcomeUnknown && m_pendingPage) {
                        const QPointer<ProductionCalibrationPage> pendingPage =
                            m_pendingPage;
                        const int pendingSegment = m_pendingSegment;
                        const QString pendingSerial = m_pendingSerial;
                        m_discoveryStatus->setText(
                            tr("地址切换结果未确认，正在查找同一设备"));
                        QTimer::singleShot(
                            DiscoveryRetryMs, this,
                            [this, pendingPage, pendingSegment,
                             pendingSerial] {
                                if (!m_shuttingDown && pendingPage
                                    && pendingPage == m_pendingPage
                                    && pendingSegment == m_pendingSegment
                                    && pendingSerial == m_pendingSerial) {
                                    pendingPage->connectAutomatically();
                                }
                            });
                        return;
                    }
                    if (m_pendingProductRecalibration) {
                        m_discoveryStatus->setText(
                            tr("重新标定地址切换结果未确认，正在重新查找设备"));
                        QTimer::singleShot(
                            500, this,
                            &ProductionCalibrationHub::beginDeviceScan);
                        return;
                    }
                    m_discoveryStatus->setText(
                        tr("设备地址分配失败，请只保留一台新设备后重试"));
                    if (m_pendingPage) {
                        setStationVisible(
                            m_pendingPage, false);
                    }
                    clearPendingAssignment();
                    return;
                }
                m_discoveryStatus->setText(
                    (m_pendingProductRecalibration
                         ? tr("重新标定设备已切换到 192.168.%1.1，等待同一设备重连")
                         : tr("已分配到 192.168.%1.1，等待设备重启"))
                        .arg(m_pendingSegment));
                if (m_pendingPage) {
                    const QPointer<ProductionCalibrationPage> pendingPage =
                        m_pendingPage;
                    const int pendingSegment = m_pendingSegment;
                    const QString pendingSerial = m_pendingSerial;
                    QTimer::singleShot(
                        2500, this,
                        [this, pendingPage, pendingSegment, pendingSerial] {
                            if (!m_shuttingDown && pendingPage
                                && pendingPage == m_pendingPage
                                && pendingSegment == m_pendingSegment
                                && pendingSerial == m_pendingSerial) {
                                pendingPage->connectAutomatically();
                            }
                        });
                }
            });

    m_scanDeadline.setSingleShot(true);
    connect(&m_scanDeadline, &QTimer::timeout,
            this, &ProductionCalibrationHub::finishScan);
    m_outputStack->hide();
    refreshStationListVisibility();
    QTimer::singleShot(0, this, &ProductionCalibrationHub::beginDeviceScan);
}

void ProductionCalibrationHub::shutdown()
{
    if (m_shuttingDown) return;
    m_shuttingDown = true;
    m_discoveryActive = false;
    m_scanDeadline.stop();
    m_scanHttp.cancelAll();
    m_configHttp.cancelAll();
    for (ProductionCalibrationPage *page : std::as_const(m_pages)) {
        if (page) page->shutdown();
    }
}

void ProductionCalibrationHub::prepareForSingleDevice()
{
    if (m_shuttingDown) return;
    m_singleDeviceMode = true;
    m_discoveryActive = false;
    m_scanDeadline.stop();
    m_scanHttp.cancelAll();
    m_configHttp.cancelAll();
    m_scanRequests.clear();
    m_scanResults.clear();
    m_configRequest = 0;
    clearPendingAssignment();
    m_finalizationQueue.clear();
    m_finalizingPage.clear();
    m_productSerial.clear();
    m_singleDevicePage = m_pages.isEmpty() ? nullptr : m_pages.front();
    for (ProductionCalibrationPage *page : std::as_const(m_pages)) {
        if (!page) continue;
        if (page == m_singleDevicePage) {
            page->prepareForSingleDevice();
            setStationVisible(page, true);
        } else {
            page->shutdown();
            setStationVisible(page, false);
        }
    }
    if (m_singleDevicePage) {
        selectStation(pageIndex(m_singleDevicePage));
        m_singleDevicePage->setPort(QStringLiteral("http://192.168.17.1"));
    }
    m_emptyState->setText(tr("等待固件烧录或设备重启"));
    m_discoveryStatus->setText(tr("单设备模式：固定连接 192.168.17.1"));
    emit workflowStateChanged(QVariantMap{
        {QStringLiteral("state"), QStringLiteral("waiting-device")},
        {QStringLiteral("single_device"), true},
        {QStringLiteral("endpoint"), QStringLiteral("http://192.168.17.1")}});
}

void ProductionCalibrationHub::notifyFirmwareFlashed(
    const QString &firmwarePath, const QString &firmwareSha256,
    const QString &bootselRoot)
{
    if (!m_singleDeviceMode) prepareForSingleDevice();
    if (!m_singleDevicePage) return;
    setStationVisible(m_singleDevicePage, true);
    selectStation(pageIndex(m_singleDevicePage));
    m_singleDevicePage->notifyFirmwareFlashed(
        firmwarePath, firmwareSha256, bootselRoot);
}

QWidget *ProductionCalibrationHub::contextPanel() const noexcept
{
    return m_contextPanel;
}

QWidget *ProductionCalibrationHub::outputPanel() const noexcept
{
    return m_outputPanel;
}

void ProductionCalibrationHub::setManualEndpoint(const QString &endpointText)
{
    if (m_manualPort && m_manualPort->currentText() != endpointText) {
        m_manualPort->setCurrentText(endpointText);
    }
}

void ProductionCalibrationHub::connectManualEndpoint()
{
    QString endpointText = m_manualPort
        ? m_manualPort->currentText().trimmed() : QString{};
    if (endpointText.isEmpty()) {
        m_discoveryStatus->setText(tr("请先填写设备 IP 或 HTTP 地址"));
        return;
    }
    if (!endpointText.startsWith(QStringLiteral("http://"),
                                 Qt::CaseInsensitive)
        && !endpointText.startsWith(QStringLiteral("https://"),
                                    Qt::CaseInsensitive)) {
        endpointText.prepend(QStringLiteral("http://"));
    }
    QUrl url(endpointText);
    const QStringList octets = url.host().split(QLatin1Char('.'));
    bool segmentOk = false;
    const int segment = octets.size() == 4
        ? octets.at(2).toInt(&segmentOk) : 0;
    if (!url.isValid() || !segmentOk || octets.at(0) != QStringLiteral("192")
        || octets.at(1) != QStringLiteral("168")
        || octets.at(3) != QStringLiteral("1")
        || segment < ProvisioningSegment || segment > ProductSegment) {
        m_discoveryStatus->setText(
            tr("生产标定地址必须为 192.168.10.1～192.168.17.1"));
        return;
    }
    url.setPath(QString{});
    url.setQuery(QString{});
    url.setFragment(QString{});
    endpointText = url.toString();
    setManualEndpoint(endpointText);
    emit manualEndpointChanged(endpointText);

    if (segment == ProvisioningSegment) {
        m_discoveryStatus->setText(
            tr("正在识别空白设备并分配临时工位"));
        scanDevices();
        return;
    }

    ProductionCalibrationPage *target = nullptr;
    if (segment >= FirstStationSegment && segment <= LastStationSegment) {
        target = m_pages.value(segment - FirstStationSegment);
    } else {
        for (ProductionCalibrationPage *page : std::as_const(m_pages)) {
            if (page && page->stationSegment() == ProductSegment) {
                target = page;
                break;
            }
        }
        if (!target) {
            for (ProductionCalibrationPage *page : std::as_const(m_pages)) {
                if (!page || page->connectionActive()
                    || !page->deviceSerial().isEmpty()) {
                    continue;
                }
                target = page;
                target->prepareForProductRecalibration({});
                break;
            }
        }
    }
    if (!target) {
        m_discoveryStatus->setText(tr("没有可用生产工位"));
        return;
    }
    setStationVisible(target, true);
    selectStation(pageIndex(target));
    target->setPort(endpointText);
    target->connectAutomatically();
    m_discoveryStatus->setText(
        segment == ProductSegment
            ? tr("正在连接已标定设备，可重新标定")
            : tr("正在手动连接生产工位 %1").arg(segment));
}

bool ProductionCalibrationHub::connectionActive() const noexcept
{
    for (const ProductionCalibrationPage *page : m_pages) {
        if (page && page->connectionActive()) return true;
    }
    return false;
}

int ProductionCalibrationHub::baudRate() const noexcept
{
    return 0;
}

QUrl ProductionCalibrationHub::endpoint(const int segment,
                                        const QString &path)
{
    return QUrl(QStringLiteral("http://192.168.%1.1%2")
                    .arg(segment)
                    .arg(path));
}

ProductionCalibrationHub::DeviceInfo
ProductionCalibrationHub::parseDevice(const QByteArray &body)
{
    const QJsonObject root = QJsonDocument::fromJson(body).object();
    const QJsonObject api = root.value(QStringLiteral("api")).toObject();
    const QJsonObject device = root.value(QStringLiteral("device")).toObject();
    const QJsonObject network =
        root.value(QStringLiteral("network")).toObject();
    if (api.value(QStringLiteral("name")).toString()
            != QStringLiteral("sipeed-thermal-http")
        || api.value(QStringLiteral("major")).toInt() != 1
        || device.value(QStringLiteral("model")).toString()
               != QStringLiteral("T160")) {
        return {};
    }
    return {device.value(QStringLiteral("serial")).toString(),
            network.value(QStringLiteral("segment")).toInt()};
}

void ProductionCalibrationHub::scanDevices()
{
    if (m_singleDeviceMode) return;
    for (ProductionCalibrationPage *page : std::as_const(m_pages)) {
        if (page) page->resumeAutomaticConnection();
    }
    beginDeviceScan();
}

void ProductionCalibrationHub::beginDeviceScan()
{
    if (m_shuttingDown || m_configRequest) return;
    m_discoveryActive = true;
    m_discoveryFoundAny = false;
    m_discoveryElapsed.restart();
    if (!m_scanRequests.isEmpty()) {
        m_scanAgainRequested = true;
        return;
    }
    m_scanAgainRequested = false;
    startScanRound();
}

void ProductionCalibrationHub::startScanRound()
{
    if (m_shuttingDown || m_configRequest || !m_discoveryActive
        || !m_scanRequests.isEmpty()) {
        return;
    }
    m_scanResults.clear();
    m_discoveryStatus->setText(tr("正在扫描设备，页面仍可继续操作"));
    for (int segment = ProvisioningSegment;
         segment <= ProductSegment; ++segment) {
        if (segment >= FirstStationSegment) {
            ProductionCalibrationPage *page =
                m_pages.value(segment - FirstStationSegment);
            if (page && page->connectionActive()) continue;
        }
        const HttpRequestClient::RequestId id =
            m_scanHttp.get(endpoint(segment, QStringLiteral("/api/v1/device")));
        m_scanRequests.insert(id, segment);
    }
    if (!m_scanRequests.isEmpty()) {
        m_scanDeadline.start(ScanRoundDeadlineMs);
    }
}

void ProductionCalibrationHub::continueDeviceScan()
{
    if (m_shuttingDown || m_configRequest || !m_discoveryActive) return;
    if (!m_discoveryElapsed.isValid()
        || m_discoveryElapsed.elapsed() >= DiscoveryWindowMs) {
        m_discoveryActive = false;
        if (!m_discoveryFoundAny) {
            m_discoveryStatus->setText(
                tr("30 秒内未发现可接入的 TN160 设备"));
        } else {
            m_discoveryStatus->setText(
                tr("设备扫描完成，已自动进入接入流程"));
        }
        return;
    }
    startScanRound();
}

void ProductionCalibrationHub::scanProductAddress()
{
    if (m_shuttingDown || m_configRequest) return;
    m_discoveryActive = false;
    m_scanAgainRequested = false;
    m_scanDeadline.stop();
    const QList<HttpRequestClient::RequestId> pending =
        m_scanRequests.keys();
    m_scanRequests.clear();
    for (const HttpRequestClient::RequestId id : pending) {
        m_scanHttp.cancel(id);
    }
    m_scanResults.clear();
    const HttpRequestClient::RequestId id =
        m_scanHttp.get(endpoint(ProductSegment,
                                QStringLiteral("/api/v1/device")));
    m_scanRequests.insert(id, ProductSegment);
    m_scanDeadline.start(ProductScanDeadlineMs);
}

void ProductionCalibrationHub::finishScan()
{
    m_scanDeadline.stop();
    const QList<HttpRequestClient::RequestId> pending =
        m_scanRequests.keys();
    m_scanRequests.clear();
    for (const HttpRequestClient::RequestId id : pending) {
        m_scanHttp.cancel(id);
    }
    processScanResults();
    if (m_scanAgainRequested) {
        m_scanAgainRequested = false;
        QTimer::singleShot(0, this,
                           &ProductionCalibrationHub::startScanRound);
        return;
    }
    if (m_discoveryActive && !m_configRequest) {
        if (m_discoveryElapsed.isValid()
            && m_discoveryElapsed.elapsed() < DiscoveryWindowMs) {
            QTimer::singleShot(
                DiscoveryRetryMs, this,
                &ProductionCalibrationHub::continueDeviceScan);
        } else {
            m_discoveryActive = false;
            if (!m_discoveryFoundAny) {
                m_discoveryStatus->setText(
                    tr("30 秒内未发现可接入的 TN160 设备"));
            } else {
                m_discoveryStatus->setText(
                    tr("设备扫描完成，已自动进入接入流程"));
            }
        }
    }
}

void ProductionCalibrationHub::processScanResults()
{
    for (int segment = FirstStationSegment;
         segment <= LastStationSegment; ++segment) {
        const auto it = m_scanResults.constFind(segment);
        if (it == m_scanResults.cend()) continue;
        ProductionCalibrationPage *page =
            m_pages.value(segment - FirstStationSegment);
        if (!page) continue;
        if (segment == m_pendingSegment && !m_pendingSerial.isEmpty()) {
            if (it->serial != m_pendingSerial) {
                updateStationLabel(page, tr("地址冲突"));
                m_discoveryStatus->setText(
                    tr("工位地址被其他设备占用，请拔出冲突设备"));
                continue;
            }
            if (m_pendingProductRecalibration && page == m_pendingPage) {
                m_productSerial.clear();
                m_productMisses = 0;
            }
            clearPendingAssignment();
        }
        if (!page->deviceSerial().isEmpty()
            && page->deviceSerial() != it->serial) {
            setStationVisible(page, true);
            updateStationLabel(page, tr("地址冲突"));
            continue;
        }
        setStationVisible(page, true);
        page->setPort(endpoint(segment).toString());
        page->connectAutomatically();
    }
    const auto unassigned = m_scanResults.constFind(ProvisioningSegment);
    bool provisioningSerialAlreadyMoved = false;
    if (unassigned != m_scanResults.cend()) {
        for (auto it = m_scanResults.cbegin(); it != m_scanResults.cend(); ++it) {
            if (it.key() != ProvisioningSegment
                && it->serial == unassigned->serial) {
                provisioningSerialAlreadyMoved = true;
                break;
            }
        }
    }
    if (unassigned != m_scanResults.cend()
        && provisioningSerialAlreadyMoved) {
        m_discoveryStatus->setText(
            tr("同一设备已在新工位出现，已忽略旧 .10 响应"));
    } else if (unassigned != m_scanResults.cend() && !m_configRequest
        && m_pendingSegment == 0) {
        allocateUnassigned(*unassigned);
    } else if (unassigned != m_scanResults.cend()
               && m_pendingSegment != 0) {
        m_discoveryStatus->setText(
            tr("上一台设备正在分配地址，请稍候"));
    } else if (!m_scanResults.isEmpty()) {
        m_discoveryStatus->setText(tr("设备扫描完成"));
    }

    const auto product = m_scanResults.constFind(ProductSegment);
    if (product != m_scanResults.cend()) {
        m_productSerial = product->serial;
        m_productMisses = 0;
        if (m_pendingProductRecalibration
            && product->serial == m_pendingSerial) {
            ProductionCalibrationPage *pendingPage = m_pendingPage;
            const QString serial = m_pendingSerial;
            clearPendingAssignment();
            if (pendingPage) {
                pendingPage->prepareForProductRecalibration(serial);
                setStationVisible(pendingPage, true);
                updateStationLabel(pendingPage,
                                   tr("已标定 · 可重新标定"));
                pendingPage->setPort(endpoint(ProductSegment).toString());
                pendingPage->connectAutomatically();
            }
            m_discoveryStatus->setText(
                tr("设备仍在 192.168.17.1，可重新点击“重新标定”"));
        } else {
            ProductionCalibrationPage *productPage = nullptr;
            for (ProductionCalibrationPage *page : std::as_const(m_pages)) {
                if (page && page->stationSegment() == ProductSegment) {
                    productPage = page;
                    break;
                }
            }
            if (!productPage) {
                for (ProductionCalibrationPage *page : std::as_const(m_pages)) {
                    if (!page || page->connectionActive()
                        || !page->deviceSerial().isEmpty()) {
                        continue;
                    }
                    productPage = page;
                    productPage->prepareForProductRecalibration(
                        product->serial);
                    break;
                }
            }
            if (productPage) {
                setStationVisible(productPage, true);
                updateStationLabel(productPage,
                                   tr("已标定 · 可重新标定"));
                productPage->setPort(endpoint(ProductSegment).toString());
                productPage->connectAutomatically();
            }
        }
    } else if (!m_productSerial.isEmpty() && !m_finalizingPage
               && !m_pendingProductRecalibration) {
        bool productConnectionActive = false;
        for (const ProductionCalibrationPage *page : std::as_const(m_pages)) {
            if (page && page->stationSegment() == ProductSegment
                && page->connectionActive()) {
                productConnectionActive = true;
                break;
            }
        }
        if (productConnectionActive) {
            m_productMisses = 0;
        } else if (++m_productMisses >= 3) {
            m_productSerial.clear();
            m_productMisses = 0;
        }
    }
    if (!m_productSerial.isEmpty() && !m_finalizingPage) {
        m_discoveryStatus->setText(
            m_pendingProductRecalibration
                ? tr("正在将 192.168.17.1 的重新标定设备切换到临时工位")
                : tr("检测到 192.168.17.1 已标定设备，可选择后重新标定"));
    }
    QStringList discoveredEndpoints;
    for (auto it = m_scanResults.cbegin(); it != m_scanResults.cend(); ++it) {
        const QString discovered = endpoint(it.key()).toString();
        discoveredEndpoints.append(discovered);
        if (m_manualPort
            && m_manualPort->findText(discovered, Qt::MatchFixedString) < 0) {
            m_manualPort->addItem(discovered);
        }
    }
    emit devicesDiscovered(discoveredEndpoints);
    grantNextFinalization();
}

void ProductionCalibrationHub::allocateUnassigned(const DeviceInfo &device)
{
    if (device.serial.isEmpty()) return;
    ProductionCalibrationPage *target = nullptr;
    for (ProductionCalibrationPage *page : std::as_const(m_pages)) {
        if (!page || page->connectionActive()
            || !page->deviceSerial().isEmpty()) {
            continue;
        }
        if (m_scanResults.contains(page->stationSegment())) continue;
        target = page;
        break;
    }
    if (!target) {
        m_discoveryStatus->setText(
            tr("没有空闲工位，请先完成或移除一台设备"));
        return;
    }
    m_pendingSegment = target->stationSegment();
    m_pendingSerial = device.serial;
    m_pendingPage = target;
    m_pendingProductRecalibration = false;
    m_discoveryActive = false;
    target->prepareForStation(m_pendingSegment, m_pendingSerial);
    setStationVisible(target, true);
    updateStationLabel(target, tr("正在分配"));
    const QByteArray payload =
        QJsonDocument(QJsonObject{
            {QStringLiteral("segment"), m_pendingSegment}})
            .toJson(QJsonDocument::Compact);
    m_configRequest = m_configHttp.put(
        endpoint(ProvisioningSegment, QStringLiteral("/api/v1/network")),
        payload,
        {{"Content-Type", "application/json"},
         {"X-Sipeed-Request", "1"}});
    m_discoveryStatus->setText(
        tr("识别到新设备 %1，正在分配工位 %2")
            .arg(shortSerial(device.serial))
            .arg(m_pendingSegment - FirstStationSegment + 1));
}

void ProductionCalibrationHub::reassignProductForRecalibration(
    ProductionCalibrationPage *page)
{
    if (!page || pageIndex(page) < 0
        || page->stationSegment() != ProductSegment
        || !page->connectionActive() || page->deviceSerial().isEmpty()) {
        m_discoveryStatus->setText(
            tr("请先连接 192.168.17.1 的已标定设备"));
        return;
    }
    if (m_configRequest || m_pendingSegment != 0
        || m_pendingProductRecalibration || !m_scanRequests.isEmpty()) {
        m_discoveryStatus->setText(
            tr("设备地址正在处理，请稍候后重试"));
        return;
    }

    const int targetSegment = pageIndex(page) + FirstStationSegment;
    if (targetSegment < FirstStationSegment
        || targetSegment > LastStationSegment
        || m_scanResults.contains(targetSegment)) {
        m_discoveryStatus->setText(
            tr("没有可用的临时工位，请先完成或移除一台设备"));
        return;
    }

    m_pendingSegment = targetSegment;
    m_pendingSerial = page->deviceSerial();
    m_pendingPage = page;
    m_pendingProductRecalibration = true;
    m_discoveryActive = false;
    m_productSerial = m_pendingSerial;
    m_productMisses = 0;
    page->prepareForStation(m_pendingSegment, m_pendingSerial);
    setStationVisible(page, true);
    updateStationLabel(page, tr("正在切换临时工位"));

    const QByteArray payload =
        QJsonDocument(QJsonObject{
            {QStringLiteral("segment"), m_pendingSegment}})
            .toJson(QJsonDocument::Compact);
    m_configRequest = m_configHttp.put(
        endpoint(ProductSegment, QStringLiteral("/api/v1/network")),
        payload,
        {{"Content-Type", "application/json"},
         {"X-Sipeed-Request", "1"}});
    m_discoveryStatus->setText(
        tr("正在将设备 %1 从 192.168.17.1 分配到工位 %2")
            .arg(shortSerial(m_pendingSerial))
            .arg(m_pendingSegment - FirstStationSegment + 1));
}

void ProductionCalibrationHub::clearPendingAssignment()
{
    m_pendingSegment = 0;
    m_pendingSerial.clear();
    m_pendingPage.clear();
    m_pendingProductRecalibration = false;
}

void ProductionCalibrationHub::updateStationLabel(
    ProductionCalibrationPage *page, const QString &state)
{
    const int index = pageIndex(page);
    if (index < 0) return;
    const QString serial = page ? shortSerial(page->deviceSerial()) : QString{};
    page->setStationPresentation(index + 1,
                                 serial.isEmpty()
                                     && page->stationSegment() == m_pendingSegment
                                     ? shortSerial(m_pendingSerial)
                                     : serial,
                                 state);
}

void ProductionCalibrationHub::setStationVisible(
    ProductionCalibrationPage *page, const bool visible)
{
    if (!page) return;
    const bool currentlyVisible = !page->isHidden();
    if (currentlyVisible == visible) return;
    page->setVisible(visible);
    if (visible && (m_contextStack->isHidden()
                    || !m_pages.value(m_contextStack->currentIndex())
                    || m_pages.value(m_contextStack->currentIndex())->isHidden())) {
        selectStation(pageIndex(page));
    } else if (!visible && m_contextStack->currentIndex() == pageIndex(page)) {
        int next = -1;
        for (int index = 0; index < m_pages.size(); ++index) {
            if (m_pages.at(index) && !m_pages.at(index)->isHidden()) {
                next = index;
                break;
            }
        }
        if (next >= 0) {
            selectStation(next);
        } else {
            for (ProductionCalibrationPage *candidate : std::as_const(m_pages)) {
                if (candidate) candidate->setStationSelected(false);
            }
            m_outputStack->hide();
        }
    }
    refreshStationListVisibility();
}

void ProductionCalibrationHub::refreshStationListVisibility()
{
    bool hasVisibleStation = false;
    for (const ProductionCalibrationPage *page : std::as_const(m_pages)) {
        if (page && !page->isHidden()) {
            hasVisibleStation = true;
            break;
        }
    }
    if (m_emptyState) m_emptyState->setVisible(!hasVisibleStation);
}

void ProductionCalibrationHub::selectStation(const int index)
{
    if (index < 0 || index >= m_pages.size()) return;
    for (int pageIndex = 0; pageIndex < m_pages.size(); ++pageIndex) {
        if (m_pages.at(pageIndex)) {
            m_pages.at(pageIndex)->setStationSelected(pageIndex == index);
        }
    }
    m_contextStack->setCurrentIndex(index);
    m_outputStack->setCurrentIndex(index);
    m_outputStack->show();
}

void ProductionCalibrationHub::grantNextFinalization()
{
    if (m_finalizingPage || m_configRequest || !m_scanRequests.isEmpty()
        || !m_productSerial.isEmpty()) {
        return;
    }
    while (!m_finalizationQueue.isEmpty()) {
        QPointer<ProductionCalibrationPage> page =
            m_finalizationQueue.takeFirst();
        if (!page) continue;
        m_finalizingPage = page;
        updateStationLabel(page, tr("正在完成"));
        page->grantFinalization();
        return;
    }
}

int ProductionCalibrationHub::pageIndex(
    ProductionCalibrationPage *page) const
{
    return m_pages.indexOf(page);
}

} // namespace serein::scope::tn160ncm
