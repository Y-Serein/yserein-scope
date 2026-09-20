#pragma once

#include "Tn160Engine.h"

#include <serein/scope/HttpRequestClient.h>
#include <serein/scope/IDataEngine.h>

#include <QtCore/QElapsedTimer>
#include <QtCore/QObject>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtCore/QVariantMap>

#include <memory>

namespace serein::scope::tn160ncm {

/* Compatibility-shaped session for the copied TN160 pages.  It deliberately
 * keeps the page-facing lifecycle (open/close/write/replaceEngine) while the
 * transport is an ordinary USB CDC-NCM IP link.  No serial port is opened. */
class Tn160NcmTransport final : public QObject {
    Q_OBJECT

public:
    explicit Tn160NcmTransport(std::unique_ptr<IDataEngine> engine,
                               QObject *parent = nullptr);
    ~Tn160NcmTransport() override;

    [[nodiscard]] bool isActive() const noexcept;
    [[nodiscard]] bool isConnecting() const noexcept;
    [[nodiscard]] QString endpoint() const;
    [[nodiscard]] QString deviceIdentity() const;
    [[nodiscard]] QString deviceSerial() const;
    [[nodiscard]] int networkSegment() const noexcept;
    bool replaceEngine(std::unique_ptr<IDataEngine> engine,
                       QString *error = nullptr);

    void open(const QString &endpoint);
    void openWhenAvailable(const QString &endpoint, int timeoutMs = 30000);
    void shutdown(const QByteArray &finalWrite = {});
    void setContinuousAdc16Enabled(bool enabled, int intervalMs = 500);
    [[nodiscard]] bool continuousAdc16Enabled() const noexcept;

public slots:
    void close();
    void closeAfterWrites(const QByteArray &finalWrite = {},
                          int drainDelayMs = 0);
    void write(const QByteArray &data);
    void resetDecoder();
    void setDecodingEnabled(bool enabled);
    void ingest(const QByteArray &data);
    void uploadCalibration(const QByteArray &packet,
                           bool completeProduction = false);
    void uploadThermalModel(const QByteArray &packet);
    void clearThermalModel();
    void requestThermalModel();
    void requestThermalModelState();
    void setNetworkSegment(int segment);

signals:
    void stateChanged(const QString &state);
    void packetsDecoded(const serein::scope::PacketList &packets);
    void rawDataReceived(const QByteArray &data);
    void bytesReceived(qint64 count);
    void bytesWritten(qint64 count);
    void errorOccurred(const QString &message);
    void calibrationUploadFinished(bool success, const QVariantMap &result,
                                   const QString &error);
    void thermalModelUploadFinished(bool success, const QVariantMap &result,
                                    const QString &error);
    void thermalModelClearFinished(bool success, const QVariantMap &result,
                                   const QString &error);
    void thermalModelReadbackFinished(bool success, const QByteArray &packet,
                                      const QString &error);
    void thermalModelStateReady(bool success, const QVariantMap &state,
                                const QString &error);
    void networkSegmentChangeFinished(bool success, int segment,
                                      const QString &error);
    void monitorFrameMetadata(qint64 hostTimestampUs, quint32 frameNumber,
                              quint32 deviceTimestampMs,
                              quint32 calibrationId, quint16 ffcCount);
    void continuousAdc16Record(qint64 hostTimestampUs,
                               const QByteArray &record,
                               quint32 sourceFrameNumber,
                               quint32 deviceTimestampMs,
                               quint32 calibrationId, quint16 ffcCount);

private slots:
    void pollFrame();
    void retryInitialConnection();
    void initialRequestTimedOut();
    void initialConnectionTimedOut();
    void activeFrameRequestTimedOut();
    void pollContinuousAdc16();

private:
    enum class Mode { Monitor, Raw };

    void setState(const QString &state);
    void beginOpen(const QString &endpoint, int retryTimeoutMs);
    void request(const QString &path);
    void handleActiveFrameFailure(const QString &message);
    void terminateActiveConnection(const QString &message);
    void refreshDiagnostics();
    void handleHttpCompleted(serein::scope::HttpRequestClient::RequestId requestId,
                             int statusCode, const QByteArray &body,
                             const QString &error);
    void handleFrame(const QByteArray &record);
    void handleMonitorFrame(const QByteArray &record);
    void handleRawFrame(const QByteArray &record);
    void emitMonitorPackets(const QByteArray &image, qint64 timestampUs,
                            const QVariantMap &diagnostics,
                            const QByteArray &meta);
    [[nodiscard]] QUrl url(const QString &path) const;
    [[nodiscard]] static double ntcCelsius(quint16 adc);
    [[nodiscard]] static quint16 jsonU16(const QVariantMap &map,
                                         const QString &key,
                                         quint16 fallback = 0);
    [[nodiscard]] static qint16 jsonI16(const QVariantMap &map,
                                         const QString &key,
                                         qint16 fallback = 0x7fff);
    [[nodiscard]] static qint32 jsonI32(const QVariantMap &map,
                                         const QString &key,
                                         qint32 fallback = 0);
    [[nodiscard]] static double jsonDouble(const QVariantMap &map,
                                            const QString &key,
                                            double fallback = 0.0);

    serein::scope::HttpRequestClient m_http;
    serein::scope::HttpRequestClient::RequestId m_reply = 0;
    serein::scope::HttpRequestClient::RequestId m_diagnosticsReply = 0;
    serein::scope::HttpRequestClient::RequestId m_uploadReply = 0;
    serein::scope::HttpRequestClient::RequestId m_thermalModelUploadReply = 0;
    serein::scope::HttpRequestClient::RequestId m_thermalModelClearReply = 0;
    serein::scope::HttpRequestClient::RequestId m_thermalModelReadReply = 0;
    serein::scope::HttpRequestClient::RequestId m_thermalModelStateReply = 0;
    serein::scope::HttpRequestClient::RequestId m_networkReply = 0;
    serein::scope::HttpRequestClient::RequestId m_continuousAdcReply = 0;
    QTimer m_pollTimer;
    QTimer m_connectRetryTimer;
    QTimer m_initialRequestTimer;
    QTimer m_connectDeadlineTimer;
    QTimer m_activeFrameRequestTimer;
    QTimer m_continuousAdcTimer;
    QElapsedTimer m_connectRetryElapsed;
    std::unique_ptr<IDataEngine> m_engine;
    QString m_endpoint = QStringLiteral("http://192.168.17.1");
    Mode m_mode = Mode::Monitor;
    bool m_active = false;
    bool m_decodingEnabled = true;
    bool m_requestPending = false;
    bool m_connectRetryEnabled = false;
    bool m_uploadInProgress = false;
    bool m_continuousAdcEnabled = false;
    int m_continuousAdcIntervalMs = 500;
    int m_connectRetryTimeoutMs = 0;
    int m_networkTargetSegment = 0;
    int m_networkSegment = 0;
    int m_consecutiveFailures = 0;
    QVariantMap m_lastDiagnostics;
    QString m_deviceIdentity;
    QString m_deviceSerial;
};

} // namespace serein::scope::tn160ncm
