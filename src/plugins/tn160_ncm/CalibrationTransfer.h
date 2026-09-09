#pragma once

#include <serein/scope/HttpRequestClient.h>

#include <QtCore/QObject>
#include <QtCore/QStringList>

namespace serein::scope::tn160ncm {

class CalibrationTransfer final : public QObject {
    Q_OBJECT

public:
    enum class Mode { Calibrate, Dump };

    explicit CalibrationTransfer(QObject *parent = nullptr);
    void start(Mode mode, const QString &endpoint);
    void cancel();
    [[nodiscard]] bool isActive() const noexcept;

signals:
    void progress(const QString &message);
    void headerReady(const QByteArray &header);
    void payloadReady(int index, const QByteArray &payload);
    void completed(bool success, const QString &message);

private:
    void requestNext();
    void finish(bool success, const QString &message);
    void handleHttpCompleted(serein::scope::HttpRequestClient::RequestId requestId,
                             int statusCode, const QByteArray &body,
                             const QString &error);
    [[nodiscard]] static bool validHeader(const QByteArray &header);

    serein::scope::HttpRequestClient m_http;
    serein::scope::HttpRequestClient::RequestId m_reply = 0;
    QString m_endpoint;
    QStringList m_paths;
    QList<int> m_expectedSizes;
    int m_index = 0;
    int m_retryCount = 0;
    bool m_active = false;
};

} // namespace serein::scope::tn160ncm
