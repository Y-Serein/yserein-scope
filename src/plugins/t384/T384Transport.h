#pragma once

#include <serein/scope/Packet.h>

#include <QtCore/QByteArray>
#include <QtCore/QElapsedTimer>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtCore/QString>
#include <QtCore/QUrl>
#include <QtCore/QVariantMap>

#include <memory>

QT_BEGIN_NAMESPACE
class QNetworkAccessManager;
class QNetworkReply;
QT_END_NAMESPACE

namespace serein::scope::t384 {

class T384Engine;

// Owns one HTTP chunked RAW16 stream.  The decoder remains deliberately
// transport-agnostic: each readyRead chunk is appended and then consumed by
// IDataEngine::feed(), so split HTTP/TCP reads never become frame boundaries.
class T384Transport final : public QObject {
    Q_OBJECT

public:
    explicit T384Transport(std::unique_ptr<T384Engine> engine,
                           QObject *parent = nullptr);
    ~T384Transport() override;

    [[nodiscard]] bool isActive() const noexcept;
    [[nodiscard]] QString state() const;
    [[nodiscard]] QString endpoint() const;
    [[nodiscard]] QString lastError() const;
    [[nodiscard]] QVariantMap diagnostics() const;

    bool replaceEngine(std::unique_ptr<T384Engine> engine,
                       QString *error = nullptr);

public slots:
    void start(const QString &endpoint = {});
    void stop();
    void resetDecoder();
    void requestDiagnostics();
    void requestCalibrationManifest();
    void requestCalibrationData();
    void uploadCalibrationData(const QByteArray &payload);
    void commitCalibration();
    void abortCalibration();

signals:
    void stateChanged(const QString &state);
    void packetsDecoded(const serein::scope::PacketList &packets);
    void rawDataReceived(const QByteArray &data);
    void bytesReceived(qint64 count);
    void errorOccurred(const QString &message);
    void diagnosticsReady(const QVariantMap &diagnostics);
    void calibrationManifestReady(const QByteArray &manifest);
    void calibrationDataReady(const QByteArray &data);
    void calibrationProgress(qint64 sent, qint64 total);
    void calibrationOperationFinished(const QString &operation, bool success,
                                      const QString &message);

private:
    void beginStreamRequest();
    void scheduleInitialRetry(const QString &message);
    void initialRequestTimedOut();
    void initialConnectionTimedOut();
    void setState(const QString &state);
    void setError(const QString &error);
    void consumeBufferedData();
    [[nodiscard]] QUrl endpointUrl(const QString &path) const;
    [[nodiscard]] static QUrl normalizeEndpoint(const QString &endpoint);
    static QVariantMap parseDiagnostics(const QByteArray &body);

    std::unique_ptr<T384Engine> m_engine;
    std::unique_ptr<QNetworkAccessManager> m_network;
    QPointer<QNetworkReply> m_streamReply;
    QPointer<QNetworkReply> m_diagnosticsReply;
    QPointer<QNetworkReply> m_calibrationReply;
    QByteArray m_pendingBytes;
    QTimer m_retryTimer;
    QTimer m_initialRequestTimer;
    QTimer m_connectDeadlineTimer;
    QElapsedTimer m_connectElapsed;
    QUrl m_endpoint = QUrl(QStringLiteral("http://192.168.17.1"));
    QVariantMap m_diagnostics;
    QString m_state = QStringLiteral("disconnected");
    QString m_lastError;
    bool m_stopping = false;
    bool m_initialRetryEnabled = false;
    bool m_streamReceivedData = false;
};

} // namespace serein::scope::t384
