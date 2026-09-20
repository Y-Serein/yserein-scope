#include "UDPWinSender.h"

#include <qdatetime.h>

#include "ZSMLoggingCategory.h"
#include "LinkManager.h"
#include "UDPWinLink.h"

#include <QUuid>

ZSM_LOGGING_CATEGORY(UDPWinSenderLog, "Backend.Comms.UDPWinSender")

UDPWinSender::UDPWinSender(QObject *parent)
    : QObject(parent)
    , _1HzTimer(new QTimer(this))
{
    zInfo(UDPWinSenderLog) << "create";

    _1HzTimer->setInterval(1000);
    connect(_1HzTimer, &QTimer::timeout, this, &UDPWinSender::_1HzLoop);

    init();
}

UDPWinSender::~UDPWinSender()
{
    stop();
    zInfo(UDPWinSenderLog) << "destroy";
}

void UDPWinSender::init()
{
    if (_initNoError) return;

    // 1. initialise WSA socket
    if (!NetUtils::initWSA()) return;

    // 2. create Socket
    _socket = NetUtils::createSocket(WSA_FLAG_OVERLAPPED);
    if (_socket == INVALID_SOCKET) return;

    // 3. Bind socket to port
    const quint16 udpListenPort = 0;					        ///< 发送端口, 系统随便找个端口发送
    if (!NetUtils::bind(_socket, udpListenPort)) return;

    // 4. create iocp , bind to socket
    _hIOCP = NetUtils::createIOCP(_socket);

    _initNoError = true;

    /// ---------------------------------------------------------
    /// 完成后创建 worker
    /// 初始化线程
    _worker = new UDPWinSenderWorker(_hIOCP);
    _workerThread = new QThread(this);

    _workerThread->setObjectName("UDP Sender Worker");
    _worker->moveToThread(_workerThread);

    QObject::connect(_workerThread, &QThread::started, _worker, &UDPWinSenderWorker::workLoop);
    QObject::connect(_workerThread, &QThread::finished, _worker, &QObject::deleteLater);
}

void UDPWinSender::stop()
{
    _1HzTimer->stop();
    _worker->stop();

    _workerThread->quit();
    if ( !_workerThread->wait() ) {
        zWarning(UDPWinSenderLog) << "failed to wait udp sender to close";
    }

    if (_socket != INVALID_SOCKET)
        closesocket(_socket);
    if (_hIOCP)
        CloseHandle(_hIOCP);

    WSACleanup();

    zInfo(UDPWinSenderLog) << "socket closed";
}

void UDPWinSender::start()
{
    _workerThread->start();
    _1HzTimer->start();
}

void UDPWinSender::sendData(QByteArray &data, const SOCKADDR_IN &target)
{
    // _sendMsgQueue.enqueue({data,target});

    auto* context = new SendContext();
    ZeroMemory(&context->overlapped, sizeof(OVERLAPPED));
    ZeroMemory(&context->targetAddr, sizeof(context->targetAddr));
    context->payload = data;
    context->wsaBuf.buf = context->payload.data();
    context->wsaBuf.len = context->payload.size();
    context->targetAddr = target;

    DWORD send_cnt = 0;

    int rc,err;

    rc = ::WSASendTo(_socket, &context->wsaBuf, 1, &send_cnt, 0,
                reinterpret_cast<sockaddr*>(&context->targetAddr), context->senderAddrLen, &context->overlapped, 0);

    if ((rc == SOCKET_ERROR) && (WSA_IO_PENDING != (err = ::WSAGetLastError())))
    {
        zWarning(UDPWinSenderLog) << "WSASend error" << err;
    } else {
        _request_package.fetch_add(1, std::memory_order_release);
    }
    /// test ///////////////////////////////
    // if ((rc == SOCKET_ERROR) && (WSA_IO_PENDING == (err = ::WSAGetLastError())))
    // {
    //     qDebug() << "IOCP send!";
    // }
    /// test ///////////////////////////////
}

void UDPWinSender::sendData(std::vector<QByteArray> &vec, const SOCKADDR_IN &target)
{
    for (auto& it : vec) {
        sendData(it, target);
    }
}

void UDPWinSender::_1HzLoop()
{
    uint32_t success_package = _worker->_successPackage.load();
    uint32_t request_package = _request_package.load();
    float success_rate = success_package * 100.0f / _request_package;
    // zInfo(UDPWinSenderLog) << "request:" << _request_package << "success:" << success_package << "rate:" << success_rate << "%";

    // _request_package.store(0,std::memory_order_release);
    // _worker->_successPackage.store(0, std::memory_order_release);
}

// void UDPWinSender::setupThread()
// {
//     zInfo(UDPWinSenderLog) << "UDP Send Server start";
//     _isRunning.store(true, std::memory_order_release);
//
//     _500HzTimer = new QTimer(this);
//     _500HzTimer->setInterval(2);
//     _1HzTimer = new QTimer(this);
//     _1HzTimer->setInterval(1000);
//
//     connect(_500HzTimer, &QTimer::timeout , this, &UDPWinSender::_500HzLoop);
//     connect(_1HzTimer, &QTimer::timeout , this, &UDPWinSender::_1HzLoop);
//
//     _500HzTimer->start();
//     _1HzTimer->start();
// }
//
// void UDPWinSender::_500HzLoop()
// {
//     _consumeQueueTask();
// }
//
// void UDPWinSender::_1HzLoop()
// {
//     _reportTask();
// }
//
// void UDPWinSender::_consumeQueueTask()
// {
//     static constexpr size_t BUF_SIZE = 128;
//
//     WSABUF _buf = {0};
//     int err, rc;
//     MessageSend_t messageBuf[BUF_SIZE];
//
//     // 一次循环最多执行 100 次
//     for (int i = 0; i < 300; i++){
//
//         size_t msg_size = _sendMsgQueue.try_dequeue_bulk(messageBuf, BUF_SIZE);
//
//         if (msg_size == 0 || !_isRunning.load(std::memory_order_relaxed)) break;
//
//         for (int i = 0;  i < msg_size;  i++) {
//             MessageSend_t& sendData = messageBuf[i];
//
//             _buf.buf = sendData.bytes.data();
//             _buf.len = sendData.bytes.size();
//
//     		DWORD bytesSent = 0;
//
//     		rc = ::WSASendTo(_socket, &_buf, 1, &bytesSent, 0,
//     			reinterpret_cast<sockaddr*>(&sendData.target), sizeof(sendData.target), 0, 0);
//
//     		if ((rc == SOCKET_ERROR) && (WSA_IO_PENDING != (err = ::WSAGetLastError())))
//     		{
//     			zWarning(UDPWinSenderLog) << "WSASend error" << err;
//     		}
//             /// test ///////////////////////////////
//             // if ((rc == SOCKET_ERROR) && (WSA_IO_PENDING == (err = ::WSAGetLastError())))
//             // {
//             //     qDebug() << "IOCP send!";
//             // }
//             /// test ///////////////////////////////
//
//             _nowCnt++;
//         }
//     }
// }
//
// void UDPWinSender::_reportTask()
// {
//     int sendCnt = _nowCnt - _lastCnt;
//     _lastCnt = _nowCnt;
//     // QString uuid = QUuid::createUuid().toString();
//     // zDebug(UDPWinSenderLog) << "send cnt:" << sendCnt << uuid;
//
//     qDebug() << QString("[%1]").arg(QDateTime::currentDateTime().toString()) << "padding message:" << _sendMsgQueue.size_approx();
// }
