#pragma once

#include <serein/scope/HttpRequestClient.h>
#include <serein/scope/Packet.h>
#include <serein/scope/PluginInterface.h>

#include <QtCore/QHash>
#include <QtCore/QElapsedTimer>
#include <QtCore/QMap>
#include <QtCore/QPointer>
#include <QtCore/QStringList>
#include <QtCore/QTimer>
#include <QtCore/QVariantMap>
#include <QtCore/QVector>
#include <QtWidgets/QWidget>
class QComboBox;

class QLabel;
class QPushButton;
class QScrollArea;
class QStackedWidget;

namespace serein::scope::tn160ncm {

class ProductionCalibrationPage;

class ProductionCalibrationHub final : public QWidget {
    Q_OBJECT

public:
    explicit ProductionCalibrationHub(IHostServices *host,
                                      QWidget *parent = nullptr);
    void shutdown();

public slots:
    void scanDevices();
    void setManualEndpoint(const QString &endpoint);
    void connectManualEndpoint();
    void prepareForSingleDevice();
    void notifyFirmwareFlashed(const QString &firmwarePath,
                               const QString &firmwareSha256,
                               const QString &bootselRoot);
    [[nodiscard]] QWidget *contextPanel() const noexcept;
    [[nodiscard]] QWidget *outputPanel() const noexcept;
    [[nodiscard]] bool connectionActive() const noexcept;
    [[nodiscard]] int baudRate() const noexcept;

signals:
    void manualEndpointChanged(const QString &endpoint);
    void devicesDiscovered(const QStringList &endpoints);
    void livePacketsDecoded(const serein::scope::PacketList &packets);
    void workflowStateChanged(const QVariantMap &state);
    void continuousRecordingChanged(bool active, const QString &path);
    void calibrationCompleted(const QString &serial, bool productionPass,
                              const QString &sessionPath);

private:
    struct DeviceInfo {
        QString serial;
        int segment = 0;
    };

    static QUrl endpoint(int segment, const QString &path = {});
    static DeviceInfo parseDevice(const QByteArray &body);
    void beginDeviceScan();
    void finishScan();
    void startScanRound();
    void continueDeviceScan();
    void scanProductAddress();
    void processScanResults();
    void allocateUnassigned(const DeviceInfo &device);
    void reassignProductForRecalibration(ProductionCalibrationPage *page);
    void clearPendingAssignment();
    void updateStationLabel(ProductionCalibrationPage *page,
                            const QString &state);
    void setStationVisible(ProductionCalibrationPage *page, bool visible);
    void refreshStationListVisibility();
    void selectStation(int index);
    void grantNextFinalization();
    [[nodiscard]] int pageIndex(ProductionCalibrationPage *page) const;

    IHostServices *m_host = nullptr;
    QWidget *m_contextPanel = nullptr;
    QWidget *m_outputPanel = nullptr;
    QLabel *m_discoveryStatus = nullptr;
    QLabel *m_emptyState = nullptr;
    QPushButton *m_scanButton = nullptr;
    QComboBox *m_manualPort = nullptr;
    QPushButton *m_manualConnectButton = nullptr;
    QScrollArea *m_stationScroll = nullptr;
    QWidget *m_stationList = nullptr;
    QStackedWidget *m_contextStack = nullptr;
    QStackedWidget *m_outputStack = nullptr;
    QVector<ProductionCalibrationPage *> m_pages;

    HttpRequestClient m_scanHttp;
    HttpRequestClient m_configHttp;
    QHash<HttpRequestClient::RequestId, int> m_scanRequests;
    QMap<int, DeviceInfo> m_scanResults;
    HttpRequestClient::RequestId m_configRequest = 0;
    QTimer m_scanDeadline;
    QElapsedTimer m_discoveryElapsed;
    int m_pendingSegment = 0;
    QString m_pendingSerial;
    QPointer<ProductionCalibrationPage> m_pendingPage;
    bool m_pendingProductRecalibration = false;
    QString m_productSerial;
    int m_productMisses = 0;
    QPointer<ProductionCalibrationPage> m_finalizingPage;
    QList<QPointer<ProductionCalibrationPage>> m_finalizationQueue;
    bool m_shuttingDown = false;
    bool m_discoveryActive = false;
    bool m_discoveryFoundAny = false;
    bool m_scanAgainRequested = false;
    bool m_singleDeviceMode = false;
    QPointer<ProductionCalibrationPage> m_singleDevicePage;
};

} // namespace serein::scope::tn160ncm
