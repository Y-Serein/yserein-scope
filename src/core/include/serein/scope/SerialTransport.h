#pragma once

#include "CoreExport.h"

#include <QtCore/QByteArray>
#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QString>

#include <memory>

class QSettings;

namespace serein::scope {

struct SerialPortInfo {
    QString portName;
    QString description;
};

struct SerialConfig {
    QString portName;
    int baudRate = 115200;
};

struct SerialTransportLimits {
    qsizetype maxPendingReceiveBytes = 4 * 1024 * 1024;
    qsizetype maxReceiveChunksPerDrain = 8;
    qsizetype maxPendingTransmitBytes = 1024 * 1024;
};

struct SerialTransportStats {
    quint64 acceptedReceiveBytes = 0;
    quint64 rejectedReceiveBytes = 0;
    quint64 deliveredReceiveBytes = 0;
    qsizetype pendingReceiveBytes = 0;
    qsizetype receiveHighWaterBytes = 0;
    quint64 acceptedTransmitBytes = 0;
    quint64 rejectedTransmitBytes = 0;
    quint64 transmittedBytes = 0;
    quint64 abandonedTransmitBytes = 0;
    qsizetype pendingTransmitBytes = 0;
    qsizetype transmitHighWaterBytes = 0;
};

class SerialWorker;

class SEREIN_SCOPE_CORE_EXPORT SerialTransport final : public QObject {
    Q_OBJECT

public:
    static SerialTransportLimits configuredLimits();
    static SerialTransportLimits configuredLimits(const QSettings &settings);

    explicit SerialTransport(QObject *parent = nullptr);
    explicit SerialTransport(SerialTransportLimits limits,
                             QObject *parent = nullptr);
    ~SerialTransport() override;

    [[nodiscard]] bool isActive() const noexcept;
    [[nodiscard]] SerialTransportStats stats() const;
    [[nodiscard]] static QList<SerialPortInfo> availablePorts();

public slots:
    void open(const serein::scope::SerialConfig &config);
    void close();
    // Request a bounded graceful shutdown after already queued control bytes
    // have reached the native serial driver.  This is used by protocol mode
    // restoration paths such as TN160 RAW -> monitor.
    void closeAfterWrites();
    void write(const QByteArray &data);

signals:
    void stateChanged(const QString &state);
    void dataReceived(const QByteArray &data);
    void bytesWritten(qint64 count);
    void errorOccurred(const QString &message);

private slots:
    void workerFinished();
    void drainReceived();

private:
    class Ingress;
    std::unique_ptr<Ingress> m_ingress;
    SerialTransportLimits m_limits;
    SerialWorker *m_worker = nullptr;

    bool enqueueReceived(QByteArray data);
    void clearReceived();
};

} // namespace serein::scope

Q_DECLARE_METATYPE(serein::scope::SerialConfig)
