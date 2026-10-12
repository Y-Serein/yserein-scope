#include "T384OemKPanel.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QRegularExpression>
#include <QtCore/QStringList>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>
#include <limits>
#include <utility>

namespace serein::scope::t384 {
namespace {
const QStringList kOemKActionNames = {QStringLiteral("low"), QStringLiteral("high"),
    QStringLiteral("compute"), QStringLiteral("save"), QStringLiteral("cancel")};
QString field(const QByteArray &body, const QString &key)
{
    const auto match = QRegularExpression(QStringLiteral("(?:^|[\\r\\n])%1=([^\\r\\n]*)")
        .arg(QRegularExpression::escape(key))).match(QString::fromUtf8(body));
    return match.hasMatch() ? match.captured(1).trimmed() : QString{};
}
}

T384OemKPanel::T384OemKPanel(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("t384OemKPanel"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *description = new QLabel(tr("低温 ≤25°C → 高温 65°C → 计算 → 检查后保存。\n"
        "校正全画面空间均匀性；完成后需重新核对测温。"), this);
    description->setWordWrap(true);
    layout->addWidget(description);
    m_refresh = new QPushButton(tr("读取机芯标定状态"), this);
    m_refresh->setObjectName(QStringLiteral("t384OemK_refresh"));
    layout->addWidget(m_refresh);
    connect(m_refresh, &QPushButton::clicked, this, [this] { begin(-1); });
    auto *grid = new QGridLayout;
    const QStringList labels = {tr("1 · 低温采集"), tr("2 · 65°C 采集"),
        tr("3 · 计算 K"), tr("4 · 检查通过，保存"), tr("取消，恢复原 K")};
    for (int i = 0; i < labels.size(); ++i) {
        auto *button = new QPushButton(labels[i], this);
        button->setObjectName(QStringLiteral("t384OemK_%1").arg(kOemKActionNames[i]));
        button->setMinimumWidth(0);
        grid->addWidget(button, i / 2, i % 2, 1, i == 4 ? 2 : 1);
        m_actions.append(button);
        connect(button, &QPushButton::clicked, this, [this, i] { begin(i); });
    }
    layout->addLayout(grid);
    m_feedback = new QLabel(tr("连接设备后读取状态。"), this);
    m_feedback->setWordWrap(true);
    m_feedback->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_feedback);
    m_poll.setSingleShot(true);
    m_deadline.setSingleShot(true);
    connect(&m_deadline, &QTimer::timeout, this, [this] {
        if (m_reply) {
            disconnect(m_reply, nullptr, this, nullptr);
            m_reply->abort(); m_reply->deleteLater(); m_reply = nullptr;
        }
        if (m_postSent) m_uncertain = true;
        finish(m_postSent ? tr("标定结果未确认，请恢复连接后读取状态；未自动重发。")
                          : tr("连接超时，未发送标定命令。"), false);
    });
    updateButtons();
}

void T384OemKPanel::setEndpoint(const QString &endpoint)
{
    if (m_busy || holdsCalibration()) return;
    QUrl url(endpoint.trimmed());
    if (url.scheme().isEmpty()) url = QUrl(QStringLiteral("http://") + endpoint.trimmed());
    if (url == m_endpoint) return;
    m_endpoint = url;
    m_identity.clear();
    m_status = {};
    updateButtons();
}

bool T384OemKPanel::holdsCalibration() const
{
    const int step = m_status.value(QStringLiteral("step")).toInt();
    return m_uncertain || m_status.value(QStringLiteral("locked")).toBool()
        || m_status.value(QStringLiteral("result")).toInt() == 4
        || (step >= 1 && step <= 3);
}

void T384OemKPanel::shutdown()
{
    m_shutdown = true;
    m_deadline.stop();
    m_poll.stop();
    if (m_reply) {
        disconnect(m_reply, nullptr, this, nullptr);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    // Never automatically save or cancel on unload/disconnect.
}

void T384OemKPanel::updateButtons()
{
    const bool known = !m_status.isEmpty();
    const int step = m_status.value(QStringLiteral("step")).toInt();
    const bool ready = known && !m_busy && !m_uncertain
        && !m_status.value(QStringLiteral("locked")).toBool()
        && m_status.value(QStringLiteral("result")).toInt() != 1;
    m_refresh->setEnabled(!m_busy && m_endpoint.isValid()
        && m_endpoint.scheme() == QStringLiteral("http") && !m_endpoint.host().isEmpty());
    m_actions[0]->setEnabled(ready && (step == 0 || step == 4 || step == 5));
    m_actions[1]->setEnabled(ready && step == 1);
    m_actions[2]->setEnabled(ready && step == 2);
    m_actions[3]->setEnabled(ready && step == 3);
    m_actions[4]->setEnabled(ready && step >= 1 && step <= 3);
    emit availabilityChanged();
}

void T384OemKPanel::begin(int action)
{
    if (m_busy || m_shutdown || !m_endpoint.isValid()) return;
    if (action >= 0) {
        if (!m_actions[action]->isEnabled()) return;
        const QStringList prompts = {
            tr("确认黑体辐射面 ≤25°C 且已稳定，均匀区域覆盖整个视场。开始原厂低温采集？"),
            tr("确认低温采集后画面已恢复，黑体辐射面现为 65°C 且已稳定，均匀区域覆盖整个视场。开始高温采集？"),
            tr("确认高温采集后画面已恢复稳定。让机芯计算 K？计算后先检查画面，再决定保存。"),
            tr("确认已检查全画面均匀性和异常像素。将当前 K 保存到机芯？此操作会覆盖机芯原 K，且不是温度精度验收。"),
            tr("放弃本轮未保存的 K，恢复原 K？")};
        if (QMessageBox::question(this, tr("原厂 K 标定"), prompts[action],
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    }
    m_busy = true;
    m_action = action;
    m_postSent = false;
    m_elapsed.start();
    m_deadline.start(55000);
    m_feedback->setText(tr("正在释放视频流并核对设备…"));
    updateButtons();
    emit prepareCommand();
    waitForStream();
}

void T384OemKPanel::request(const QString &path, const QByteArray &body,
                          std::function<void(int, const QByteArray &)> complete)
{
    if (m_shutdown || m_reply) return;
    QUrl url = m_endpoint;
    url.setPath(path); url.setQuery(QString{}); url.setFragment(QString{});
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(3000);
    request.setRawHeader("Accept", "application/json");
    if (body.isEmpty()) m_reply = m_network.get(request);
    else {
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/octet-stream"));
        m_reply = m_network.post(request, body);
    }
    connect(m_reply, &QNetworkReply::finished, this, [this, complete = std::move(complete)] {
        QPointer<QNetworkReply> reply = std::exchange(m_reply, {});
        if (!reply) return;
        const int code = reply->error() == QNetworkReply::NoError
            ? reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
            : -reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        reply->deleteLater();
        if (!m_shutdown) complete(code, body);
    });
}

void T384OemKPanel::waitForStream()
{
    request(QStringLiteral("/diag"), {}, [this](int code, const QByteArray &body) {
        if (code != 200) { finish(tr("设备诊断不可达；未发送标定命令。"), false); return; }
        const QString pn = field(body, QStringLiteral("mini2.pn"));
        const QString sn = field(body, QStringLiteral("mini2.sn"));
        const QString fw = field(body, QStringLiteral("mini2.firmware_version"));
        if (pn != QStringLiteral("TIFSC640") || sn.isEmpty() || fw != QStringLiteral("01.00.01.03")) {
            finish(tr("当前机芯型号或固件不匹配此标定入口；未发送命令。"), false); return;
        }
        const QString identity = pn + QLatin1Char('/') + sn + QLatin1Char('/') + fw;
        if (!m_identity.isEmpty() && identity != m_identity && holdsCalibration()) {
            finish(tr("设备身份已变化；停止本轮标定。"), false); return;
        }
        m_identity = identity;
        const QString stream = field(body, QStringLiteral("stream.active"));
        if (stream != QStringLiteral("0")) {
            if (m_elapsed.elapsed() >= 8000 || stream.isEmpty()) {
                finish(tr("视频流未释放，请关闭其他视频客户端后重试。"), false); return;
            }
            disconnect(&m_poll, nullptr, this, nullptr);
            connect(&m_poll, &QTimer::timeout, this, &T384OemKPanel::waitForStream);
            m_poll.start(250); return;
        }
        readStatus(true);
    });
}

bool T384OemKPanel::acceptStatus(const QJsonObject &status)
{
    for (const QString &key : {QStringLiteral("schema"), QStringLiteral("revision"),
         QStringLiteral("step"), QStringLiteral("action"), QStringLiteral("result"),
         QStringLiteral("error"), QStringLiteral("uart_status")}) {
        const auto value = status.value(key);
        if (!value.isDouble() || value.toDouble() != double(value.toInteger())) return false;
    }
    const double revision = status.value(QStringLiteral("revision")).toDouble();
    const int step = status.value(QStringLiteral("step")).toInt(-1);
    const int result = status.value(QStringLiteral("result")).toInt(-1);
    if (status.value(QStringLiteral("schema")).toInt() != 1 || revision < 0
        || revision > std::numeric_limits<quint32>::max() || step < 0 || step > 5
        || result < 0 || result > 4 || !status.value(QStringLiteral("locked")).isBool()) return false;
    const int action = status.value(QStringLiteral("action")).toInt(-1);
    const int uart = status.value(QStringLiteral("uart_status")).toInt(-1);
    if (action < 0 || action > 4 || uart < 0 || uart > 255
        || (result == 2 && (uart != 0 || step != action + 1 || revision == 0))) return false;
    m_status = status;
    return true;
}

void T384OemKPanel::readStatus(bool beforeCommand)
{
    request(QStringLiteral("/api/v1/oem-k/status"), {},
        [this, beforeCommand](int code, const QByteArray &body) {
        const QJsonObject previous = m_status;
        if (code != 200 || !acceptStatus(QJsonDocument::fromJson(body).object())) {
            if (m_postSent) {
                m_uncertain = true;
                finish(tr("命令已提交，但状态无法确认。禁止重发；恢复连接后读取状态。"), false);
            } else finish(tr("无法读取原厂 K 状态。请确认设备已安装支持此功能的固件。"), false);
            return;
        }
        const quint32 revision = quint32(m_status.value(QStringLiteral("revision")).toDouble());
        const int result = m_status.value(QStringLiteral("result")).toInt();
        if (beforeCommand && m_action >= 0) {
            if (previous.isEmpty() || previous.value(QStringLiteral("revision")) != m_status.value(QStringLiteral("revision"))
                || previous.value(QStringLiteral("step")) != m_status.value(QStringLiteral("step"))
                || result == 1 || result == 4 || m_status.value(QStringLiteral("locked")).toBool()) {
                finish(tr("机芯状态已变化或被锁定，请读取并核对状态后再操作。"), false); return;
            }
            if (revision == std::numeric_limits<quint32>::max()) {
                finish(tr("机芯事务计数已满，请重启设备后重新开始。"), false); return;
            }
            m_expectedRevision = revision + 1;
            sendAction(); return;
        }
        if (m_postSent && (revision != m_expectedRevision
                || m_status.value(QStringLiteral("action")).toInt(-1) != m_action)) {
            m_uncertain = true;
            finish(tr("标定事务不匹配，无法确认本次命令；禁止自动重发。"), false); return;
        }
        if (result == 1) {
            if (m_elapsed.elapsed() >= 50000) {
                m_uncertain = true;
                finish(tr("等待机芯应答超时；请读取状态，勿重复发送。"), false); return;
            }
            disconnect(&m_poll, nullptr, this, nullptr);
            connect(&m_poll, &QTimer::timeout, this, [this] { readStatus(false); });
            m_poll.start(250); return;
        }
        m_uncertain = result == 4 || m_status.value(QStringLiteral("locked")).toBool();
        if (m_uncertain) {
            finish(tr("机芯命令结果不确定（错误 %1）。请重启设备后重新核对；本轮未自动保存或重发。")
                .arg(m_status.value(QStringLiteral("error")).toInt()), false); return;
        }
        if (result == 3) {
            finish(tr("原厂操作失败：错误 %1，UART 状态 %2；未进入下一步。")
                .arg(m_status.value(QStringLiteral("error")).toInt())
                .arg(m_status.value(QStringLiteral("uart_status")).toInt()), true); return;
        }
        const QStringList states = {tr("就绪：先采集 ≤25°C 低温黑体。"),
            tr("低温命令已应答。等待画面恢复，再准备 65°C 黑体。"),
            tr("65°C 高温命令已应答。等待画面恢复，再计算 K。"),
            tr("计算命令已应答。检查全画面后选择保存或取消。"),
            tr("保存命令已应答。断电保持性及测温精度仍需实际复核。"),
            tr("取消命令已应答。请检查恢复后的画面。")};
        finish(states[m_status.value(QStringLiteral("step")).toInt()], true);
    });
}

void T384OemKPanel::sendAction()
{
    emit commandSubmitted();
    m_postSent = true; // Set before network I/O: even a lost HTTP reply is ambiguous.
    m_feedback->setText(tr("等待机芯应答…"));
    emit record(tr("原厂 K：%1 · %2 · 事务 %3").arg(kOemKActionNames[m_action], m_identity).arg(m_expectedRevision));
    const QByteArray body = kOemKActionNames[m_action].toLatin1() + ' ' + QByteArray::number(m_expectedRevision - 1);
    request(QStringLiteral("/api/v1/oem-k/action"), body, [this](int code, const QByteArray &) {
        if (code == -400 || code == -403 || code == -409 || code == -415 || code == -503) {
            m_postSent = false;
            finish(tr("设备拒绝标定请求（HTTP %1），请重新读取状态。未自动重发。")
                .arg(-code), false); return;
        }
        // Successful or lost POST reply: only poll, never resend the write.
        readStatus(false);
    });
}

void T384OemKPanel::finish(const QString &message, bool resume)
{
    m_poll.stop();
    m_busy = false;
    m_deadline.stop();
    m_feedback->setText(message);
    emit record(message);
    updateButtons();
    emit operationFinished(resume);
}
}
