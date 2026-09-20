#include "UDPWinLink.h"
#include <QSettings>
#include "ZSMLoggingCategory.h"
#include "ZSMBackend.h"
#include "UDPWinReceiver.h"
#include "UDPWinSender.h"

#include <QElapsedTimer>
#include <QTimer>

ZSM_LOGGING_CATEGORY(UDPWinLinkLog, "Backend.Comms.UDPWinLink")

UDPWinLink::UDPWinLink(QObject* parent)
    : QObject(parent)
    , _receiver(new UDPWinReceiver())
    , _recvThread(new QThread(this))
    , _sender(new UDPWinSender())
    // , _sendThread(new QThread(this))
{
    zInfo(UDPWinLinkLog) << "create";

    _defaultGCSAddress.sin_family = AF_INET;
    // Defaults to 37890 per the fixed contract; override via Mavlink/UdpTargetPort (e.g. 41020 on hosts where the WSL2/Hyper-V range reserves 37890).
    _defaultGCSAddress.sin_port = htons(QSettings().value(QStringLiteral("Mavlink/UdpTargetPort"), 37890).toUInt()); ///< 地面站接收端口号
    _defaultGCSAddress.sin_addr.s_addr = inet_addr("192.168.20.251");  ///< 主程序固定监听地址
    // _defaultGCSAddress.sin_addr.s_addr = inet_addr("192.168.1.103");
    // _defaultGCSAddress.sin_addr.s_addr = inet_addr("192.168.1.114");

    // init recv
    _recvThread->setObjectName("UDP Receiver");
    _receiver->moveToThread(_recvThread);

    QObject::connect(_recvThread, &QThread::started, _receiver, &UDPWinReceiver::workLoop);
    QObject::connect(_recvThread, &QThread::finished, _receiver, &QObject::deleteLater);

    // _recvThread->start();

    // init send
    // _sendThread->setObjectName("UDP Sender");
    // _sendThread->moveToThread(_sendThread);

    // QObject::connect(_sendThread, &QThread::started, _sender, &UDPWinSender::setupThread);
    // QObject::connect(_sendThread, &QThread::finished, _sender, &QObject::deleteLater);
}

UDPWinLink::~UDPWinLink()
{
    _receiver->stop();
    _recvThread->quit();

    _sender->stop();

    if ( !_recvThread->wait() ) {
        zWarning(UDPWinLinkLog) << "failed to wait udp receive thread to close";
    }

    // _sendThread->quit();
    // if ( !_sendThread->wait() ) {
    //     zWarning(UDPWinLinkLog) << "failed to wait udp sender to close";
    // }

    zInfo(UDPWinLinkLog) << "destory";
}

void UDPWinLink::writeBytesThreadSafe(QByteArray& data)
{
    _receiver->sendData(data, _defaultGCSAddress);
}

void UDPWinLink::writeBytesThreadSafe(std::vector<QByteArray> &buffer)
{
    for (auto& data : buffer) {
        _receiver->sendData(data, _defaultGCSAddress);
    }
}

bool UDPWinLink::isConnected() const
{
    // return _udpServer->isRunning();
    return _receiver->isRunning();
}

void UDPWinLink::connect()
{
    _recvThread->start();
    _sender->start();
    // QObject::connect(zsmApp(), &ZSMBackend::back1HzSignal, this, &UDPWinLink::_report);
}

void UDPWinLink::_onDataReceived(const QByteArray& data, const SOCKADDR_IN& host)
{
    UDPQtClient addr(host.sin_addr, host.sin_port);

    ///TODO remoe me
    static QElapsedTimer elapsed;       ///< Test

    if ( !elapsed.isValid() ) elapsed.start();

    zDebug(UDPWinLinkLog) << "receive:" << data.size() << addr << "host:" << addr << "after wait:" << elapsed.elapsed();
    elapsed.restart();

    emit bytesReceived(data, QVariant::fromValue(addr));
}

// void UDPWinLink::_report()
// {
//     uint32_t now = _nowBytesReceived.load(std::memory_order_relaxed);
//     uint32_t last = _lastBytesReceived.load(std::memory_order_relaxed);
//     double rate = static_cast<double>(now - last) / 1024.0 / 1024.0;
//     _lastBytesReceived.store(now, std::memory_order_relaxed);
//
//     zDebug(UDPWinLinkLog) << "send rate:" << rate << "MB/s";
// }
