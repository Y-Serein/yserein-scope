#include "CalibrationTransfer.h"
#include "Tn160NcmProtocolUtils.h"

#include <QtCore/QByteArrayView>
#include <QtCore/QTimer>
#include <QtCore/QtEndian>

#include <utility>

namespace serein::scope::tn160ncm {
namespace {

constexpr int HeaderBytes = 64;
constexpr int ImageBytes = 160 * 120 * 2;
constexpr int GainBytes = 160 * 120 * 4;
constexpr int BadPointBytes = 10;
constexpr int MaxReconnectRetries = 30;

} // namespace

CalibrationTransfer::CalibrationTransfer(QObject *parent)
    : QObject(parent), m_http(this)
{
    connect(&m_http, &serein::scope::HttpRequestClient::completed,
            this, &CalibrationTransfer::handleHttpCompleted);
}

void CalibrationTransfer::start(const Mode mode, const QString &endpoint)
{
    if (m_active) {
        emit completed(false, tr("已有标定传输正在运行"));
        return;
    }
    if (mode == Mode::Calibrate) {
        QTimer::singleShot(0, this, [this] {
            emit completed(false,
                           tr("USB/NCM 不启用历史 0xCC 设备端标定；"
                              "请使用“生产标定”完成两点采集、V1 写入和回读"));
        });
        return;
    }
    m_endpoint = detail::normalizeEndpoint(endpoint);
    m_paths = {QStringLiteral("/api/v1/calibration/v1/data/header"),
               QStringLiteral("/api/v1/calibration/v1/data/image"),
               QStringLiteral("/api/v1/calibration/v1/data/gain"),
               QStringLiteral("/api/v1/calibration/v1/data/badpoints")};
    m_expectedSizes = {HeaderBytes, ImageBytes, GainBytes, BadPointBytes};
    m_index = 0;
    m_retryCount = 0;
    m_active = true;
    emit progress(tr("通过 USB NCM 读取 V1 标定数据"));
    requestNext();
}

void CalibrationTransfer::cancel()
{
    if (!m_active) return;
    finish(false, tr("标定读取已取消"));
}

bool CalibrationTransfer::isActive() const noexcept
{
    return m_active;
}

bool CalibrationTransfer::validHeader(const QByteArray &header)
{
    if (header.size() != HeaderBytes || !header.startsWith("TNC1")) {
        return false;
    }
    const auto *bytes = reinterpret_cast<const uchar *>(header.constData());
    return qFromLittleEndian<quint16>(bytes + 4) == 1u
           && qFromLittleEndian<quint16>(bytes + 6) == HeaderBytes
           && qFromLittleEndian<quint16>(bytes + 8) == 160u
           && qFromLittleEndian<quint16>(bytes + 10) == 120u
           && qFromLittleEndian<quint16>(bytes + 12) == 5u
           && qFromLittleEndian<quint16>(bytes + 14) == 0u
           && qFromLittleEndian<quint32>(bytes + 16) == ImageBytes
           && qFromLittleEndian<quint32>(bytes + 20) == GainBytes
           && qFromLittleEndian<quint32>(bytes + 24) == BadPointBytes
           && detail::parseNtcModelHeader(header).status
                  != detail::NtcModelHeaderStatus::Invalid
           && qFromLittleEndian<quint32>(bytes + 60)
                  == detail::crc32(QByteArrayView(header.constData(), 60));
}

void CalibrationTransfer::requestNext()
{
    if (!m_active) return;
    if (m_index >= m_paths.size()) {
        finish(true, tr("V1 标定三段读取完成"));
        return;
    }
    m_reply = m_http.get(QUrl(m_endpoint + m_paths.at(m_index)));
}

void CalibrationTransfer::handleHttpCompleted(
    const serein::scope::HttpRequestClient::RequestId requestId,
    const int statusCode, const QByteArray &body, const QString &error)
{
    if (requestId != m_reply) return;
    m_reply = 0;
    if (!m_active) return;
    if (!error.isEmpty()) {
        if (m_retryCount++ < MaxReconnectRetries) {
            emit progress(statusCode == 404
                ? tr("设备重启中，标定接口尚未就绪，重试 %1/%2")
                      .arg(m_retryCount).arg(MaxReconnectRetries)
                : tr("NCM 暂不可达，等待设备重启后重试 %1/%2：%3")
                      .arg(m_retryCount).arg(MaxReconnectRetries).arg(error));
            QTimer::singleShot(1000, this, &CalibrationTransfer::requestNext);
            return;
        }
        finish(false, tr("NCM 标定读取失败：%1").arg(error));
        return;
    }
    m_retryCount = 0;
    if (body.size() != m_expectedSizes.at(m_index)) {
        finish(false, tr("NCM 标定段 %1 长度异常：%2 / %3 bytes")
                          .arg(m_index + 1).arg(body.size())
                          .arg(m_expectedSizes.at(m_index)));
        return;
    }
    if (m_index == 0) {
        if (!validHeader(body)) {
            finish(false, tr("NCM TNC1 header 校验失败"));
            return;
        }
        emit headerReady(body);
    } else {
        emit payloadReady(m_index - 1, body);
    }
    emit progress(tr("V1 段 %1/%2 · %3 bytes")
                      .arg(m_index + 1).arg(m_paths.size()).arg(body.size()));
    ++m_index;
    requestNext();
}

void CalibrationTransfer::finish(const bool success, const QString &message)
{
    if (!m_active && success) return;
    m_active = false;
    const auto reply = std::exchange(m_reply, 0);
    m_http.cancel(reply);
    emit completed(success, message);
}

} // namespace serein::scope::tn160ncm
