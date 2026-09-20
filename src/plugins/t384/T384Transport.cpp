#include "T384Transport.h"

#include "T384Engine.h"

#include <QtCore/QByteArrayView>
#include <QtCore/QUrlQuery>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <utility>

namespace serein::scope::t384 {
namespace {

constexpr auto Raw16StreamPath = "/raw16.stream";
constexpr auto DiagnosticsPath = "/diag";
constexpr auto CalibrationManifestPath = "/api/v1/calibration/v1/manifest";
constexpr auto CalibrationDataPath = "/api/v1/calibration/v1/data";
constexpr int InitialRetryIntervalMs = 100;
constexpr int InitialRequestTimeoutMs = 1000;
constexpr int InitialConnectionTimeoutMs = 30000;

} // namespace

T384Transport::T384Transport(std::unique_ptr<T384Engine> engine,
                             QObject *parent)
    : QObject(parent), m_engine(std::move(engine)),
      m_network(std::make_unique<QNetworkAccessManager>(this))
{
    m_retryTimer.setSingleShot(true);
    m_initialRequestTimer.setSingleShot(true);
    m_connectDeadlineTimer.setSingleShot(true);
    connect(&m_retryTimer, &QTimer::timeout, this,
            [this] { beginStreamRequest(); });
    connect(&m_initialRequestTimer, &QTimer::timeout, this,
            &T384Transport::initialRequestTimedOut);
    connect(&m_connectDeadlineTimer, &QTimer::timeout, this,
            &T384Transport::initialConnectionTimedOut);
}

T384Transport::~T384Transport()
{
    stop();
}

bool T384Transport::isActive() const noexcept
{
    return m_streamReply && !m_stopping;
}

QString T384Transport::state() const
{
    return m_state;
}

QString T384Transport::endpoint() const
{
    return m_endpoint.toString(QUrl::RemovePath | QUrl::RemoveQuery
                               | QUrl::RemoveFragment);
}

QString T384Transport::lastError() const
{
    return m_lastError;
}

QVariantMap T384Transport::diagnostics() const
{
    return m_diagnostics;
}

bool T384Transport::replaceEngine(std::unique_ptr<T384Engine> engine,
                                  QString *error)
{
    if (!engine) {
        if (error) *error = tr("T384 数据引擎不能为空");
        return false;
    }
    m_engine = std::move(engine);
    resetDecoder();
    return true;
}

void T384Transport::start(const QString &endpoint)
{
    stop();
    if (!endpoint.trimmed().isEmpty()) m_endpoint = normalizeEndpoint(endpoint);
    if (!m_endpoint.isValid() || m_endpoint.scheme() != QStringLiteral("http")
        || m_endpoint.host().isEmpty()) {
        setError(tr("T384 endpoint 无效：%1").arg(endpoint));
        return;
    }
    if (!m_engine) {
        setError(tr("T384 数据引擎不能为空"));
        return;
    }

    m_stopping = false;
    m_lastError.clear();
    m_pendingBytes.clear();
    m_engine->reset();
    m_initialRetryEnabled = true;
    m_streamReceivedData = false;
    m_connectElapsed.start();
    m_connectDeadlineTimer.start(InitialConnectionTimeoutMs);
    beginStreamRequest();
}

void T384Transport::beginStreamRequest()
{
    if (m_stopping || !m_initialRetryEnabled || m_streamReply) return;

    m_streamReceivedData = false;
    setState(m_connectElapsed.elapsed() == 0
                 ? QStringLiteral("connecting")
                 : QStringLiteral("reconnecting"));

    QNetworkRequest request(endpointUrl(QString::fromLatin1(Raw16StreamPath)));
    request.setRawHeader("Accept", "application/octet-stream");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    m_streamReply = m_network->get(request);
    const QPointer<QNetworkReply> attempt = m_streamReply;
    m_initialRequestTimer.start(InitialRequestTimeoutMs);
    connect(attempt, &QNetworkReply::readyRead, this, [this, attempt] {
        if (!attempt || m_streamReply != attempt || m_stopping) return;
        const QByteArray chunk = attempt->readAll();
        if (chunk.isEmpty()) return;
        m_streamReceivedData = true;
        m_initialRetryEnabled = false;
        m_retryTimer.stop();
        m_initialRequestTimer.stop();
        m_connectDeadlineTimer.stop();
        emit rawDataReceived(chunk);
        emit bytesReceived(chunk.size());
        m_pendingBytes.append(chunk);
        consumeBufferedData();
        if (m_state == QStringLiteral("connecting")) {
            setState(QStringLiteral("streaming"));
        }
    });
    connect(attempt, &QNetworkReply::finished, this, [this, attempt] {
        if (!attempt || m_streamReply != attempt) return;
        QPointer<QNetworkReply> reply = std::exchange(m_streamReply, {});
        if (!reply) return;
        m_initialRequestTimer.stop();
        const bool stopped = m_stopping;
        const QString error = reply->error() == QNetworkReply::NoError
                                  ? QString{}
                                  : reply->errorString();
        const int status = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        if (stopped) return;
        if (m_initialRetryEnabled && !m_streamReceivedData
            && (status < 200 || status >= 300 || !error.isEmpty())) {
            scheduleInitialRetry(error.isEmpty()
                                     ? tr("HTTP %1").arg(status)
                                     : error);
        } else if (error.isEmpty()) {
            setError(tr("T384 RAW16 流意外结束"));
        } else {
            setError(tr("T384 RAW16 流失败：%1").arg(error));
        }
    });
    requestDiagnostics();
}

void T384Transport::scheduleInitialRetry(const QString &message)
{
    if (!m_initialRetryEnabled || m_stopping) return;
    if (m_connectElapsed.isValid()
        && m_connectElapsed.elapsed() < InitialConnectionTimeoutMs) {
        if (m_diagnosticsReply) {
            disconnect(m_diagnosticsReply, nullptr, this, nullptr);
            m_diagnosticsReply->abort();
            m_diagnosticsReply->deleteLater();
            m_diagnosticsReply = nullptr;
        }
        setState(QStringLiteral("reconnecting"));
        m_lastError = tr("等待 T384 网络恢复：%1").arg(message);
        m_retryTimer.start(InitialRetryIntervalMs);
        return;
    }
    m_initialRetryEnabled = false;
    m_connectDeadlineTimer.stop();
    setError(tr("T384 NCM 连接超时：%1").arg(message));
}

void T384Transport::initialRequestTimedOut()
{
    if (!m_initialRetryEnabled || m_stopping || !m_streamReply
        || m_streamReceivedData) return;
    QPointer<QNetworkReply> reply = std::exchange(m_streamReply, {});
    reply->abort();
    reply->deleteLater();
    scheduleInitialRetry(tr("首帧等待超过 %1 ms").arg(InitialRequestTimeoutMs));
}

void T384Transport::initialConnectionTimedOut()
{
    if (!m_initialRetryEnabled || m_stopping) return;
    m_retryTimer.stop();
    m_initialRequestTimer.stop();
    if (m_streamReply) {
        QPointer<QNetworkReply> reply = std::exchange(m_streamReply, {});
        reply->abort();
        reply->deleteLater();
    }
    m_initialRetryEnabled = false;
    setError(tr("等待 T384 USB 网络恢复超过 %1 ms")
                 .arg(InitialConnectionTimeoutMs));
}

void T384Transport::stop()
{
    const bool wasActive = isActive() || m_state == QStringLiteral("connecting")
                           || m_state == QStringLiteral("reconnecting")
                           || m_state == QStringLiteral("streaming");
    m_stopping = true;
    m_initialRetryEnabled = false;
    m_retryTimer.stop();
    m_initialRequestTimer.stop();
    m_connectDeadlineTimer.stop();
    if (m_streamReply) {
        disconnect(m_streamReply, nullptr, this, nullptr);
        m_streamReply->abort();
        m_streamReply->deleteLater();
        m_streamReply = nullptr;
    }
    if (m_diagnosticsReply) {
        disconnect(m_diagnosticsReply, nullptr, this, nullptr);
        m_diagnosticsReply->abort();
        m_diagnosticsReply->deleteLater();
        m_diagnosticsReply = nullptr;
    }
    if (m_calibrationReply) {
        disconnect(m_calibrationReply, nullptr, this, nullptr);
        m_calibrationReply->abort();
        m_calibrationReply->deleteLater();
        m_calibrationReply = nullptr;
    }
    m_pendingBytes.clear();
    if (wasActive) setState(QStringLiteral("disconnected"));
}

void T384Transport::requestCalibrationManifest()
{
    if (!m_network || !m_endpoint.isValid()) return;
    if (m_calibrationReply) { m_calibrationReply->abort(); m_calibrationReply->deleteLater(); }
    QNetworkRequest request(endpointUrl(QString::fromLatin1(CalibrationManifestPath)));
    request.setRawHeader("Accept", "application/json");
    m_calibrationReply = m_network->get(request);
    connect(m_calibrationReply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *reply = m_calibrationReply.data();
        m_calibrationReply = nullptr;
        if (!reply) return;
        const QByteArray body = reply->readAll();
        const bool ok = reply->error() == QNetworkReply::NoError &&
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() >= 200 &&
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() < 300;
        const QString message = ok ? QString{} : reply->errorString(); reply->deleteLater();
        if (ok) emit calibrationManifestReady(body);
        emit calibrationOperationFinished(QStringLiteral("manifest"), ok,
                                          ok ? QString{} : message);
    });
}

void T384Transport::requestCalibrationData()
{
    if (!m_network || !m_endpoint.isValid()) return;
    if (m_calibrationReply) { m_calibrationReply->abort(); m_calibrationReply->deleteLater(); }
    QNetworkRequest request(endpointUrl(QString::fromLatin1(CalibrationDataPath)));
    request.setRawHeader("Accept", "application/json");
    m_calibrationReply = m_network->get(request);
    connect(m_calibrationReply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *reply = m_calibrationReply.data();
        m_calibrationReply = nullptr;
        if (!reply) return;
        const QByteArray body = reply->readAll();
        const bool ok = reply->error() == QNetworkReply::NoError &&
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() >= 200 &&
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() < 300;
        const QString message = ok ? QString{} : reply->errorString(); reply->deleteLater();
        if (ok) emit calibrationDataReady(body);
        emit calibrationOperationFinished(QStringLiteral("read"), ok,
                                          ok ? QString{} : message);
    });
}

void T384Transport::uploadCalibrationData(const QByteArray &payload)
{
    if (!m_network || !m_endpoint.isValid() || payload.isEmpty()) return;
    if (m_calibrationReply) { m_calibrationReply->abort(); m_calibrationReply->deleteLater(); }
    QNetworkRequest request(endpointUrl(QString::fromLatin1(CalibrationDataPath)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/octet-stream"));
    request.setRawHeader("Accept", "application/json");
    m_calibrationReply = m_network->put(request, payload);
    connect(m_calibrationReply, &QNetworkReply::uploadProgress, this,
            &T384Transport::calibrationProgress);
    connect(m_calibrationReply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *reply = m_calibrationReply.data();
        m_calibrationReply = nullptr;
        if (!reply) return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
        const QString message = ok ? QString{} : (reply->errorString().isEmpty() ? QStringLiteral("HTTP %1").arg(status) : reply->errorString());
        reply->deleteLater();
        emit calibrationOperationFinished(QStringLiteral("upload"), ok, message);
    });
}

void T384Transport::commitCalibration()
{
    if (!m_network || !m_endpoint.isValid()) return;
    QNetworkRequest request(endpointUrl(QStringLiteral("/api/v1/calibration/v1/commit")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    m_calibrationReply = m_network->post(request, QByteArray{});
    connect(m_calibrationReply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *reply = m_calibrationReply.data();
        m_calibrationReply = nullptr;
        if (!reply) return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
        const QString msg = ok ? QString{} : (reply->errorString().isEmpty() ? QStringLiteral("HTTP %1").arg(status) : reply->errorString());
        reply->deleteLater(); emit calibrationOperationFinished(QStringLiteral("commit"), ok, msg);
    });
}

void T384Transport::abortCalibration()
{
    if (!m_network || !m_endpoint.isValid()) return;
    QNetworkRequest request(endpointUrl(QStringLiteral("/api/v1/calibration/v1/abort")));
    m_calibrationReply = m_network->post(request, QByteArray{});
    connect(m_calibrationReply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *reply = m_calibrationReply.data();
        m_calibrationReply = nullptr;
        if (!reply) return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
        const QString msg = ok ? QString{} : (reply->errorString().isEmpty() ? QStringLiteral("HTTP %1").arg(status) : reply->errorString());
        reply->deleteLater(); emit calibrationOperationFinished(QStringLiteral("abort"), ok, msg);
    });
}

void T384Transport::resetDecoder()
{
    m_pendingBytes.clear();
    if (m_engine) m_engine->reset();
}

void T384Transport::requestDiagnostics()
{
    if (!m_network || !m_endpoint.isValid()) return;
    if (m_diagnosticsReply) {
        disconnect(m_diagnosticsReply, nullptr, this, nullptr);
        m_diagnosticsReply->abort();
        m_diagnosticsReply->deleteLater();
    }
    QNetworkRequest request(endpointUrl(QString::fromLatin1(DiagnosticsPath)));
    request.setRawHeader("Accept", "text/plain");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    m_diagnosticsReply = m_network->get(request);
    connect(m_diagnosticsReply, &QNetworkReply::finished, this, [this] {
        QPointer<QNetworkReply> reply = std::exchange(m_diagnosticsReply, {});
        if (!reply) return;
        const QByteArray body = reply->readAll();
        const QString error = reply->error() == QNetworkReply::NoError
                                  ? QString{}
                                  : reply->errorString();
        const int status = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        if (m_stopping) return;
        if (!error.isEmpty() || status < 200 || status >= 300) {
            emit errorOccurred(tr("读取 T384 /diag 失败：%1")
                                   .arg(error.isEmpty()
                                            ? tr("HTTP %1").arg(status)
                                            : error));
            return;
        }
        m_diagnostics = parseDiagnostics(body);
        emit diagnosticsReady(m_diagnostics);
    });
}

void T384Transport::setState(const QString &state)
{
    if (m_state == state) return;
    m_state = state;
    emit stateChanged(m_state);
}

void T384Transport::setError(const QString &error)
{
    m_lastError = error;
    setState(QStringLiteral("error"));
    emit errorOccurred(error);
}

void T384Transport::consumeBufferedData()
{
    while (!m_pendingBytes.isEmpty() && m_engine) {
        const ParseResult result = m_engine->feed(
            QByteArrayView(m_pendingBytes.constData(), m_pendingBytes.size()));
        if (!result.packets.isEmpty()) emit packetsDecoded(result.packets);
        if (result.consumed < 0 || result.consumed > m_pendingBytes.size()) {
            setError(tr("T384 解码器返回非法 consumed=%1").arg(result.consumed));
            return;
        }
        if (result.consumed == 0) return;
        m_pendingBytes.remove(0, result.consumed);
    }
}

QUrl T384Transport::endpointUrl(const QString &path) const
{
    QUrl url = m_endpoint;
    url.setPath(path);
    url.setQuery(QUrlQuery{});
    url.setFragment({});
    return url;
}

QUrl T384Transport::normalizeEndpoint(const QString &endpoint)
{
    QString normalized = endpoint.trimmed();
    if (!normalized.contains(QStringLiteral("://"))) {
        normalized.prepend(QStringLiteral("http://"));
    }
    QUrl url = QUrl::fromUserInput(normalized);
    url.setPath({});
    url.setQuery(QUrlQuery{});
    url.setFragment({});
    return url;
}

QVariantMap T384Transport::parseDiagnostics(const QByteArray &body)
{
    QVariantMap values;
    const QList<QByteArray> lines = body.split('\n');
    for (const QByteArray &line : lines) {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith('#')) continue;
        const qsizetype separator = trimmed.indexOf('=');
        if (separator <= 0) continue;
        const QString key = QString::fromUtf8(trimmed.first(separator)).trimmed();
        const QString value = QString::fromUtf8(trimmed.sliced(separator + 1)).trimmed();
        if (!key.isEmpty()) values.insert(key, value);
    }
    return values;
}

} // namespace serein::scope::t384
