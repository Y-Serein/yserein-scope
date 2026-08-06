#pragma once

#include "CoreExport.h"

#include <QtCore/QByteArray>
#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QString>

namespace serein::scope {

struct SerialPortInfo {
    QString portName;
    QString description;
};

struct SerialConfig {
    QString portName;
    int baudRate = 115200;
};

class SerialWorker;

class SEREIN_SCOPE_CORE_EXPORT SerialTransport final : public QObject {
    Q_OBJECT

public:
    explicit SerialTransport(QObject *parent = nullptr);
    ~SerialTransport() override;

    [[nodiscard]] bool isActive() const noexcept;
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

private:
    SerialWorker *m_worker = nullptr;
};

} // namespace serein::scope

Q_DECLARE_METATYPE(serein::scope::SerialConfig)
