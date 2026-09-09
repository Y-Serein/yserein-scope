#include <serein/scope/SerialTransport.h>

#include "SerialTransmitQueue.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QQueue>
#include <QtCore/QRegularExpression>
#include <QtCore/QSettings>
#include <QtCore/QSet>
#include <QtCore/QThread>
#include <QtCore/QHash>
#include <QtCore/QVector>

#include <atomic>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <cwchar>
#include <stdexcept>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <setupapi.h>
#elif defined(Q_OS_LINUX)
#  include <asm/ioctls.h>
#  include <asm/termbits.h>
#  include <fcntl.h>
#  include <sys/ioctl.h>
#  include <unistd.h>
#endif

namespace serein::scope {
namespace {

QMutex g_leaseMutex;
QSet<QString> g_leasedPorts;

constexpr auto MaxPendingReceiveBytesKey =
    "runtime/serial/max-pending-receive-bytes";
constexpr auto MaxReceiveChunksPerDrainKey =
    "runtime/serial/max-receive-chunks-per-drain";
constexpr auto MaxPendingTransmitBytesKey =
    "runtime/serial/max-pending-transmit-bytes";

template<typename Value>
Value boundedSetting(const QSettings &settings, const char *key,
                     const Value fallback, const Value minimum,
                     const Value maximum)
{
    bool valid = false;
    const qlonglong raw = settings.value(QString::fromLatin1(key),
                                         qlonglong(fallback)).toLongLong(&valid);
    if (!valid) return fallback;
    return Value(std::clamp<qlonglong>(raw, qlonglong(minimum),
                                      qlonglong(maximum)));
}

QString leaseKey(const QString &port)
{
    return port.trimmed().toCaseFolded();
}

bool acquireLease(const QString &port)
{
    QMutexLocker lock(&g_leaseMutex);
    const QString key = leaseKey(port);
    if (g_leasedPorts.contains(key)) {
        return false;
    }
    g_leasedPorts.insert(key);
    return true;
}

void releaseLease(const QString &port)
{
    QMutexLocker lock(&g_leaseMutex);
    g_leasedPorts.remove(leaseKey(port));
}

#ifdef Q_OS_WIN
QString windowsError(const DWORD code)
{
    wchar_t *buffer = nullptr;
    const DWORD size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
            | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        0,
        reinterpret_cast<wchar_t *>(&buffer),
        0,
        nullptr);
    QString message = size ? QString::fromWCharArray(buffer, int(size)).trimmed()
                           : QStringLiteral("Win32 error %1").arg(code);
    if (buffer) {
        LocalFree(buffer);
    }
    return message;
}
#endif

} // namespace

class SerialTransport::Ingress final {
public:
    mutable QMutex mutex;
    QQueue<QByteArray> chunks;
    SerialTransportStats stats;
    bool drainScheduled = false;
    bool overflowReported = false;
};

class SerialWorker final : public QThread {
    Q_OBJECT

public:
    explicit SerialWorker(SerialConfig config, const qsizetype maxPendingTxBytes,
                          QObject *parent = nullptr)
        : QThread(parent), m_config(std::move(config)),
          m_transmitQueue(maxPendingTxBytes)
    {
    }

    void requestStop()
    {
        if (m_drainBeforeStop.load(std::memory_order_acquire)) {
            return;
        }
        m_transmitQueue.stopAccepting();
        m_stop.store(true, std::memory_order_release);
    }

    void requestStopAfterWrites()
    {
        m_transmitQueue.stopAccepting();
        m_drainBeforeStop.store(true, std::memory_order_release);
        m_stop.store(true, std::memory_order_release);
    }

    bool enqueue(const QByteArray &data)
    {
        return m_transmitQueue.enqueue(data);
    }

signals:
    void opened();
    void received(const QByteArray &data);
    void transmitted(qint64 count);
    void failed(const QString &message);

protected:
    void run() override
    {
        if (!acquireLease(m_config.portName)) {
            emit failed(tr("串口已被另一个工作区占用：%1").arg(m_config.portName));
            return;
        }
        struct LeaseGuard {
            QString port;
            ~LeaseGuard() { releaseLease(port); }
        } guard{m_config.portName};

#ifdef Q_OS_WIN
        runWindows();
#elif defined(Q_OS_LINUX)
        runLinux();
#else
        emit failed(tr("当前平台尚未实现原生串口传输"));
#endif
    }

private:
    [[nodiscard]] bool shouldDrainWrites() const
    {
        return m_drainBeforeStop.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool stopRequested() const
    {
        return m_stop.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool hasPendingWrites()
    {
        return m_transmitQueue.hasPending();
    }

    void stopImmediately()
    {
        m_transmitQueue.stopAccepting();
        m_drainBeforeStop.store(false, std::memory_order_release);
        m_stop.store(true, std::memory_order_release);
    }

#ifdef Q_OS_WIN
    void runWindows()
    {
        QString nativeName = m_config.portName;
        if (!nativeName.startsWith(QStringLiteral("\\\\.\\"))) {
            nativeName.prepend(QStringLiteral("\\\\.\\"));
        }
        const HANDLE handle = CreateFileW(
            reinterpret_cast<LPCWSTR>(nativeName.utf16()),
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            emit failed(tr("打开 %1 失败：%2")
                            .arg(m_config.portName, windowsError(GetLastError())));
            return;
        }

        SetupComm(handle, 512 * 1024, 64 * 1024);
        DCB dcb{};
        dcb.DCBlength = sizeof(DCB);
        if (!GetCommState(handle, &dcb)) {
            emit failed(tr("读取串口配置失败：%1").arg(windowsError(GetLastError())));
            CloseHandle(handle);
            return;
        }
        dcb.BaudRate = DWORD(m_config.baudRate);
        dcb.ByteSize = 8;
        dcb.Parity = NOPARITY;
        dcb.StopBits = ONESTOPBIT;
        dcb.fBinary = TRUE;
        dcb.fDtrControl = DTR_CONTROL_ENABLE;
        dcb.fRtsControl = RTS_CONTROL_ENABLE;
        dcb.fOutxCtsFlow = FALSE;
        dcb.fOutxDsrFlow = FALSE;
        dcb.fOutX = FALSE;
        dcb.fInX = FALSE;
        if (!SetCommState(handle, &dcb)) {
            emit failed(tr("设置 %1 baud 失败：%2")
                            .arg(m_config.baudRate)
                            .arg(windowsError(GetLastError())));
            CloseHandle(handle);
            return;
        }

        COMMTIMEOUTS timeouts{};
        timeouts.ReadIntervalTimeout = 20;
        timeouts.ReadTotalTimeoutConstant = 20;
        timeouts.ReadTotalTimeoutMultiplier = 0;
        timeouts.WriteTotalTimeoutConstant = 1000;
        SetCommTimeouts(handle, &timeouts);
        PurgeComm(handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
        emit opened();

        QByteArray buffer(64 * 1024, Qt::Uninitialized);
        while (true) {
            if (stopRequested() && !shouldDrainWrites()) {
                break;
            }
            if (const std::optional<QByteArray> pending = m_transmitQueue.takeNext()) {
                const QByteArray &payload = *pending;
                qsizetype offset = 0;
                while (offset < payload.size()) {
                    if (stopRequested() && !shouldDrainWrites()) {
                        break;
                    }
                    DWORD written = 0;
                    const DWORD remaining = DWORD(payload.size() - offset);
                    if (!WriteFile(handle, payload.constData() + offset, remaining,
                                   &written, nullptr)) {
                        emit failed(tr("串口写入失败：%1")
                                        .arg(windowsError(GetLastError())));
                        stopImmediately();
                        break;
                    }
                    if (written == 0) {
                        emit failed(tr("串口写入失败：系统未接受数据"));
                        stopImmediately();
                        break;
                    }
                    offset += qsizetype(written);
                }
                if (offset == payload.size()) {
                    emit transmitted(qint64(offset));
                }
            }
            if (stopRequested()) {
                if (!shouldDrainWrites()) {
                    break;
                }
                if (hasPendingWrites()) {
                    continue;
                }
                // WriteFile only guarantees that the driver accepted a byte.
                // TN160 mode-control commands must leave the serial driver
                // before this handle is closed, matching pyserial.flush() in
                // the reference tool.
                if (!FlushFileBuffers(handle)) {
                    emit failed(tr("等待串口发送完成失败：%1")
                                    .arg(windowsError(GetLastError())));
                }
                break;
            }
            DWORD receivedCount = 0;
            if (!ReadFile(handle, buffer.data(), DWORD(buffer.size()), &receivedCount,
                          nullptr)) {
                if (!stopRequested()) {
                    emit failed(tr("串口读取失败：%1")
                                    .arg(windowsError(GetLastError())));
                }
                break;
            }
            if (receivedCount > 0) {
                emit received(QByteArray(buffer.constData(), qsizetype(receivedCount)));
            }
        }
        CloseHandle(handle);
    }
#endif

#ifdef Q_OS_LINUX
    void runLinux()
    {
        const QByteArray path = QFile::encodeName(m_config.portName);
        const int fd = ::open(path.constData(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd < 0) {
            emit failed(tr("打开 %1 失败：%2")
                            .arg(m_config.portName, QString::fromLocal8Bit(strerror(errno))));
            return;
        }

        termios2 tty{};
        if (::ioctl(fd, TCGETS2, &tty) < 0) {
            emit failed(tr("读取 termios2 失败：%1")
                            .arg(QString::fromLocal8Bit(strerror(errno))));
            ::close(fd);
            return;
        }
        tty.c_iflag = 0;
        tty.c_oflag = 0;
        tty.c_lflag = 0;
        tty.c_cflag &= ~(CBAUD | CSIZE | PARENB | CSTOPB | CRTSCTS);
        tty.c_cflag |= BOTHER | CS8 | CLOCAL | CREAD;
        tty.c_ispeed = unsigned(m_config.baudRate);
        tty.c_ospeed = unsigned(m_config.baudRate);
        tty.c_cc[VMIN] = 0;
        tty.c_cc[VTIME] = 1;
        if (::ioctl(fd, TCSETS2, &tty) < 0) {
            emit failed(tr("设置 %1 baud 失败：%2")
                            .arg(m_config.baudRate)
                            .arg(QString::fromLocal8Bit(strerror(errno))));
            ::close(fd);
            return;
        }
        emit opened();

        QByteArray buffer(64 * 1024, Qt::Uninitialized);
        while (true) {
            if (stopRequested() && !shouldDrainWrites()) {
                break;
            }
            if (const std::optional<QByteArray> pending = m_transmitQueue.takeNext()) {
                const QByteArray &payload = *pending;
                qsizetype offset = 0;
                while (offset < payload.size()) {
                    if (stopRequested() && !shouldDrainWrites()) {
                        break;
                    }
                    const ssize_t written = ::write(fd, payload.constData() + offset,
                                                    size_t(payload.size() - offset));
                    if (written > 0) {
                        offset += qsizetype(written);
                        continue;
                    }
                    if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        QThread::msleep(2);
                        continue;
                    }
                    emit failed(tr("串口写入失败：%1")
                                    .arg(QString::fromLocal8Bit(strerror(errno))));
                    stopImmediately();
                    break;
                }
                if (offset == payload.size()) {
                    emit transmitted(qint64(offset));
                }
            }
            if (stopRequested()) {
                if (!shouldDrainWrites()) {
                    break;
                }
                if (hasPendingWrites()) {
                    continue;
                }
                // Match the reference tool's explicit flush before close so
                // a queued TN160 mode-control byte is not discarded.
                if (::tcdrain(fd) < 0) {
                    emit failed(tr("等待串口发送完成失败：%1")
                                    .arg(QString::fromLocal8Bit(strerror(errno))));
                }
                break;
            }
            const ssize_t received = ::read(fd, buffer.data(), size_t(buffer.size()));
            if (received > 0) {
                emit this->received(QByteArray(buffer.constData(), qsizetype(received)));
            } else if (received < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                emit failed(tr("串口读取失败：%1")
                                .arg(QString::fromLocal8Bit(strerror(errno))));
                break;
            } else {
                QThread::msleep(2);
            }
        }
        ::close(fd);
    }
#endif

    SerialConfig m_config;
    std::atomic_bool m_stop{false};
    std::atomic_bool m_drainBeforeStop{false};
    detail::SerialTransmitQueue m_transmitQueue;
};

SerialTransport::SerialTransport(QObject *parent)
    : SerialTransport(configuredLimits(), parent)
{
}

SerialTransport::SerialTransport(const SerialTransportLimits limits,
                                 QObject *parent)
    : QObject(parent), m_ingress(std::make_unique<Ingress>()), m_limits(limits)
{
    if (limits.maxPendingReceiveBytes <= 0
        || limits.maxReceiveChunksPerDrain <= 0
        || limits.maxPendingTransmitBytes <= 0) {
        throw std::invalid_argument("SerialTransport limits must be positive");
    }
    qRegisterMetaType<SerialConfig>();
}

SerialTransport::~SerialTransport()
{
    close();
    if (m_worker) {
        m_worker->wait();
    }
}

SerialTransportLimits SerialTransport::configuredLimits()
{
    const QSettings settings;
    return configuredLimits(settings);
}

SerialTransportLimits SerialTransport::configuredLimits(
    const QSettings &settings)
{
    const SerialTransportLimits defaults;
    SerialTransportLimits limits;
    limits.maxPendingReceiveBytes = boundedSetting<qsizetype>(
        settings, MaxPendingReceiveBytesKey, defaults.maxPendingReceiveBytes,
        64 * 1024, 256 * 1024 * 1024);
    limits.maxReceiveChunksPerDrain = boundedSetting<qsizetype>(
        settings, MaxReceiveChunksPerDrainKey,
        defaults.maxReceiveChunksPerDrain, 1, 1024);
    limits.maxPendingTransmitBytes = boundedSetting<qsizetype>(
        settings, MaxPendingTransmitBytesKey, defaults.maxPendingTransmitBytes,
        1024, 64 * 1024 * 1024);
    return limits;
}

bool SerialTransport::isActive() const noexcept
{
    return m_worker && m_worker->isRunning();
}

SerialTransportStats SerialTransport::stats() const
{
    QMutexLocker lock(&m_ingress->mutex);
    SerialTransportStats result = m_ingress->stats;
    result.pendingReceiveBytes = m_ingress->stats.pendingReceiveBytes;
    return result;
}

QList<SerialPortInfo> SerialTransport::availablePorts()
{
    QList<SerialPortInfo> result;
#ifdef Q_OS_WIN
    QHash<QString, QString> ports;
    const QRegularExpression pattern(QStringLiteral("^COM\\d+$"),
                                     QRegularExpression::CaseInsensitiveOption);
    const auto addPort = [&ports, &pattern](const QString &name,
                                            const QString &description) {
        const QString normalized = name.trimmed().toUpper();
        if (!pattern.match(normalized).hasMatch()) {
            return;
        }
        const auto existing = ports.constFind(normalized);
        if (existing == ports.cend() || existing->isEmpty()) {
            ports.insert(normalized, description);
        }
    };

    const auto collectDeviceSet = [&addPort](const HDEVINFO deviceSet) {
        for (DWORD index = 0;; ++index) {
            SP_DEVINFO_DATA device{};
            device.cbSize = sizeof(device);
            if (!SetupDiEnumDeviceInfo(deviceSet, index, &device)) {
                if (GetLastError() == ERROR_NO_MORE_ITEMS) {
                    break;
                }
                continue;
            }
            const HKEY key = SetupDiOpenDevRegKey(deviceSet, &device,
                                                   DICS_FLAG_GLOBAL, 0,
                                                   DIREG_DEV, KEY_QUERY_VALUE);
            if (key == INVALID_HANDLE_VALUE) {
                continue;
            }
            wchar_t portName[64]{};
            DWORD type = 0;
            DWORD bytes = sizeof(portName);
            const LONG query = RegQueryValueExW(key, L"PortName", nullptr, &type,
                                                reinterpret_cast<LPBYTE>(portName), &bytes);
            RegCloseKey(key);
            if (query != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) {
                continue;
            }
            wchar_t description[512]{};
            DWORD required = 0;
            QString label;
            if (SetupDiGetDeviceRegistryPropertyW(deviceSet, &device, SPDRP_FRIENDLYNAME,
                                                  nullptr,
                                                  reinterpret_cast<PBYTE>(description),
                                                  sizeof(description), &required)
                || SetupDiGetDeviceRegistryPropertyW(deviceSet, &device, SPDRP_DEVICEDESC,
                                                     nullptr,
                                                     reinterpret_cast<PBYTE>(description),
                                                     sizeof(description), &required)) {
                label = QString::fromWCharArray(description).trimmed();
            }
            addPort(QString::fromWCharArray(portName),
                    label.isEmpty() ? QStringLiteral("Windows serial port") : label);
        }
    };
    DWORD classGuidCount = 0;
    SetupDiClassGuidsFromNameW(L"Ports", nullptr, 0, &classGuidCount);
    if (classGuidCount > 0) {
        QVector<GUID> classGuids;
        classGuids.resize(qsizetype(classGuidCount));
        if (SetupDiClassGuidsFromNameW(L"Ports", classGuids.data(), classGuidCount,
                                       &classGuidCount)) {
            for (const GUID &classGuid : classGuids) {
                const HDEVINFO deviceSet = SetupDiGetClassDevsW(
                    &classGuid, nullptr, nullptr, DIGCF_PRESENT);
                if (deviceSet == INVALID_HANDLE_VALUE) {
                    continue;
                }
                collectDeviceSet(deviceSet);
                SetupDiDestroyDeviceInfoList(deviceSet);
            }
        }
    }

    HKEY serialMap = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DEVICEMAP\\SERIALCOMM", 0,
                      KEY_QUERY_VALUE, &serialMap) == ERROR_SUCCESS) {
        for (DWORD index = 0;; ++index) {
            wchar_t valueName[256]{};
            DWORD valueNameLength = 256;
            wchar_t value[256]{};
            DWORD type = 0;
            DWORD bytes = sizeof(value);
            const LONG status = RegEnumValueW(serialMap, index, valueName,
                                               &valueNameLength, nullptr, &type,
                                               reinterpret_cast<LPBYTE>(value), &bytes);
            if (status == ERROR_NO_MORE_ITEMS) {
                break;
            }
            if (status == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ)) {
                addPort(QString::fromWCharArray(value), QStringLiteral("Windows serial port"));
            }
        }
        RegCloseKey(serialMap);
    }

    DWORD capacity = 32768;
    QVector<wchar_t> buffer;
    buffer.resize(qsizetype(capacity));
    DWORD length = QueryDosDeviceW(nullptr, buffer.data(), capacity);
    if (length == 0 && GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
        capacity = 262144;
        buffer.resize(int(capacity));
        length = QueryDosDeviceW(nullptr, buffer.data(), capacity);
    }
    if (length > 0) {
        const wchar_t *entry = buffer.constData();
        while (*entry) {
            const QString name = QString::fromWCharArray(entry);
            addPort(name, QStringLiteral("Windows serial port"));
            entry += wcslen(entry) + 1;
        }
    }
    for (auto it = ports.cbegin(); it != ports.cend(); ++it) {
        result.append({it.key(), it.value()});
    }
#elif defined(Q_OS_LINUX)
    const QDir dev(QStringLiteral("/dev"));
    const QStringList filters{
        QStringLiteral("ttyUSB*"),
        QStringLiteral("ttyACM*"),
        QStringLiteral("ttyS*"),
        QStringLiteral("ttyAMA*"),
    };
    for (const QString &entry : dev.entryList(filters, QDir::System | QDir::Files,
                                               QDir::Name)) {
        result.append({dev.absoluteFilePath(entry), QStringLiteral("Linux serial port")});
    }
#endif
    std::sort(result.begin(), result.end(), [](const auto &left, const auto &right) {
        return left.portName.localeAwareCompare(right.portName) < 0;
    });
    return result;
}

void SerialTransport::open(const SerialConfig &config)
{
    if (isActive()) {
        emit errorOccurred(tr("已有活动串口会话"));
        return;
    }
    if (config.portName.trimmed().isEmpty() || config.baudRate <= 0) {
        emit errorOccurred(tr("串口名称或波特率无效"));
        return;
    }
    clearReceived();
    m_worker = new SerialWorker(config, m_limits.maxPendingTransmitBytes, this);
    SerialWorker *worker = m_worker;
    connect(m_worker, &SerialWorker::opened, this, [this] {
        emit stateChanged(QStringLiteral("connected"));
    });
    connect(m_worker, &SerialWorker::received, this,
            [this, worker](const QByteArray &data) {
                if (!enqueueReceived(data)) worker->requestStop();
            }, Qt::DirectConnection);
    connect(m_worker, &SerialWorker::transmitted, this,
            [this](const qint64 count) {
                {
                    QMutexLocker lock(&m_ingress->mutex);
                    const qsizetype delivered = std::min(
                        qsizetype(std::max<qint64>(count, 0)),
                        m_ingress->stats.pendingTransmitBytes);
                    m_ingress->stats.pendingTransmitBytes -= delivered;
                    m_ingress->stats.transmittedBytes += quint64(delivered);
                }
                emit bytesWritten(count);
            });
    connect(m_worker, &SerialWorker::failed, this, &SerialTransport::errorOccurred);
    connect(m_worker, &QThread::finished, this, &SerialTransport::workerFinished);
    emit stateChanged(QStringLiteral("connecting"));
    m_worker->start();
}

void SerialTransport::close()
{
    if (!m_worker) {
        return;
    }
    emit stateChanged(QStringLiteral("disconnecting"));
    m_worker->requestStop();
}

void SerialTransport::closeAfterWrites()
{
    if (!m_worker) {
        return;
    }
    emit stateChanged(QStringLiteral("disconnecting"));
    m_worker->requestStopAfterWrites();
}

void SerialTransport::write(const QByteArray &data)
{
    if (data.isEmpty()) return;
    if (!isActive()) {
        {
            QMutexLocker lock(&m_ingress->mutex);
            m_ingress->stats.rejectedTransmitBytes += quint64(data.size());
        }
        emit errorOccurred(tr("串口尚未连接"));
        return;
    }
    if (!m_worker->enqueue(data)) {
        {
            QMutexLocker lock(&m_ingress->mutex);
            m_ingress->stats.rejectedTransmitBytes += quint64(data.size());
        }
        emit errorOccurred(tr("串口发送队列已满，连接已停止以避免静默丢命令"));
        m_worker->requestStop();
        return;
    }
    {
        QMutexLocker lock(&m_ingress->mutex);
        m_ingress->stats.acceptedTransmitBytes += quint64(data.size());
        m_ingress->stats.pendingTransmitBytes += data.size();
        m_ingress->stats.transmitHighWaterBytes = std::max(
            m_ingress->stats.transmitHighWaterBytes,
            m_ingress->stats.pendingTransmitBytes);
    }
}

void SerialTransport::workerFinished()
{
    while (stats().pendingReceiveBytes > 0) drainReceived();
    {
        QMutexLocker lock(&m_ingress->mutex);
        m_ingress->stats.abandonedTransmitBytes += quint64(
            m_ingress->stats.pendingTransmitBytes);
        m_ingress->stats.pendingTransmitBytes = 0;
    }
    auto *worker = qobject_cast<SerialWorker *>(sender());
    if (worker == m_worker) {
        m_worker = nullptr;
    }
    if (worker) {
        worker->deleteLater();
    }
    emit stateChanged(QStringLiteral("disconnected"));
}

bool SerialTransport::enqueueReceived(QByteArray data)
{
    if (data.isEmpty()) return true;
    bool scheduleDrain = false;
    bool reportOverflow = false;
    {
        QMutexLocker lock(&m_ingress->mutex);
        if (data.size() > m_limits.maxPendingReceiveBytes
                              - m_ingress->stats.pendingReceiveBytes) {
            m_ingress->stats.rejectedReceiveBytes += quint64(data.size());
            if (!m_ingress->overflowReported) {
                m_ingress->overflowReported = true;
                reportOverflow = true;
            }
        } else {
            m_ingress->chunks.enqueue(std::move(data));
            const qsizetype accepted = m_ingress->chunks.back().size();
            m_ingress->stats.acceptedReceiveBytes += quint64(accepted);
            m_ingress->stats.pendingReceiveBytes += accepted;
            m_ingress->stats.receiveHighWaterBytes = std::max(
                m_ingress->stats.receiveHighWaterBytes,
                m_ingress->stats.pendingReceiveBytes);
            if (!m_ingress->drainScheduled) {
                m_ingress->drainScheduled = true;
                scheduleDrain = true;
            }
        }
    }
    if (reportOverflow) {
        QMetaObject::invokeMethod(this, [this] {
            emit errorOccurred(
                tr("串口接收队列已满，连接已停止以避免静默丢协议字节"));
        }, Qt::QueuedConnection);
        return false;
    }
    if (scheduleDrain) {
        QMetaObject::invokeMethod(this, &SerialTransport::drainReceived,
                                  Qt::QueuedConnection);
    }
    return true;
}

void SerialTransport::drainReceived()
{
    for (qsizetype count = 0; count < m_limits.maxReceiveChunksPerDrain; ++count) {
        QByteArray data;
        {
            QMutexLocker lock(&m_ingress->mutex);
            if (m_ingress->chunks.isEmpty()) break;
            data = std::move(m_ingress->chunks.head());
            m_ingress->chunks.dequeue();
            m_ingress->stats.pendingReceiveBytes -= data.size();
            m_ingress->stats.deliveredReceiveBytes += quint64(data.size());
        }
        emit dataReceived(data);
    }

    bool scheduleAgain = false;
    {
        QMutexLocker lock(&m_ingress->mutex);
        if (m_ingress->chunks.isEmpty()) {
            m_ingress->drainScheduled = false;
        } else {
            scheduleAgain = true;
        }
    }
    if (scheduleAgain) {
        QMetaObject::invokeMethod(this, &SerialTransport::drainReceived,
                                  Qt::QueuedConnection);
    }
}

void SerialTransport::clearReceived()
{
    QMutexLocker lock(&m_ingress->mutex);
    m_ingress->chunks.clear();
    m_ingress->stats = {};
    m_ingress->drainScheduled = false;
    m_ingress->overflowReported = false;
}

} // namespace serein::scope

#include "SerialTransport.moc"
