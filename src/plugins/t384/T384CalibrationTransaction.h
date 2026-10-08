#pragma once

#include "T384CalibrationProtocol.h"

#include <serein/scope/HttpRequestClient.h>

#include <QtCore/QByteArray>
#include <QtCore/QJsonObject>
#include <QtCore/QObject>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtCore/QVariantMap>

namespace serein::scope::t384 {

/** Serial, fail-closed A/B calibration transaction.
 *
 * A write is never retried after commit dispatch. If the commit response or
 * any later verification response is lost, the result is WriteUncertain and
 * callers may only startVerify() with the same packet.
 */
class T384CalibrationTransaction final : public QObject {
    Q_OBJECT
public:
    enum class Outcome { Succeeded, Failed, Cancelled, WriteUncertain };
    enum class Phase {
        Idle, Diag, Status, BackupManifest, BackupData, BackupManifest2,
        Upload, Commit,
        VerifyManifest1, VerifyData, VerifyManifest2, VerifyStatus, Done
    };
    struct ExpectedDevice {
        QUrl endpoint;
        QString pn;
        QString sn;
        QString fwHex;
        QString profile;
    };

    explicit T384CalibrationTransaction(HttpRequestClient *http,
                                        QObject *parent = nullptr);
    ~T384CalibrationTransaction() override;

    bool startWrite(const ExpectedDevice &expected, const QByteArray &packet);
    bool startVerify(const ExpectedDevice &expected, const QByteArray &packet);
    void cancel();
    void shutdown();

    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] Phase phase() const noexcept { return m_phase; }
    [[nodiscard]] QByteArray previousPacket() const { return m_previousPacket; }
    [[nodiscard]] QJsonObject result() const { return m_result; }

signals:
    void progress(serein::scope::t384::T384CalibrationTransaction::Phase phase,
                  const QString &detail);
    void previousPacketReady(const QByteArray &packet);
    void complete(serein::scope::t384::T384CalibrationTransaction::Outcome outcome,
                  const QString &detail, const QJsonObject &result);

private slots:
    void onHttpCompleted(HttpRequestClient::RequestId requestId, int status,
                         const QByteArray &body, const QString &networkError);
    void onRequestTimeout();

private:
    bool begin(const ExpectedDevice &expected, const QByteArray &packet,
               bool verifyOnly);
    void requestGet(Phase phase, const QString &path);
    void requestPut(Phase phase, const QString &path, const QByteArray &body);
    void requestPost(Phase phase, const QString &path);
    void advance(int status, const QByteArray &body);
    void transportFailure(const QString &detail);
    void definitiveFailure(const QString &detail);
    void finish(Outcome outcome, const QString &detail);
    void bestEffortAbortStaging();
    [[nodiscard]] QUrl api(const QString &path) const;
    [[nodiscard]] bool validateDiag(const QByteArray &body, QString *why) const;
    [[nodiscard]] bool validateStorageStatus(const QJsonObject &status,
                                             bool final, QString *why) const;
    [[nodiscard]] bool validateManifest(const QJsonObject &manifest,
                                        bool expected, QString *why) const;
    [[nodiscard]] bool reconstructPreviousPacket(const QByteArray &payload,
                                                 QString *why);
    [[nodiscard]] static QVariantMap parseDiag(const QByteArray &body);
    [[nodiscard]] static QJsonObject parseObject(const QByteArray &body,
                                                 QString *why);
    [[nodiscard]] static quint32 jsonUInt(const QJsonObject &object,
                                         const QString &key);

    HttpRequestClient *m_http = nullptr;
    HttpRequestClient::RequestId m_requestId = 0;
    QTimer m_timeout;
    Phase m_phase = Phase::Idle;
    ExpectedDevice m_expected;
    QByteArray m_packet;
    CalibrationPacket m_decoded;
    QByteArray m_payload;
    QByteArray m_previousPacket;
    QByteArray m_backupPayload;
    QJsonObject m_backupManifest;
    QJsonObject m_verifyManifest;
    QJsonObject m_result;
    bool m_verifyOnly = false;
    bool m_stagingMayExist = false;
    bool m_commitDispatched = false;
    bool m_shutdown = false;
};

} // namespace serein::scope::t384

Q_DECLARE_METATYPE(serein::scope::t384::T384CalibrationTransaction::Phase)
Q_DECLARE_METATYPE(serein::scope::t384::T384CalibrationTransaction::Outcome)
