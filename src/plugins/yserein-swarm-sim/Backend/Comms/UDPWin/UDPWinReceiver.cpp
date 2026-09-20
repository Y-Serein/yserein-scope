#include "UDPWinReceiver.h"
#include <QSettings>
#include "ZSMLoggingCategory.h"
#include "UDPWinLink.h"
#include "LinkManager.h"
#include "MavlinkDistributor.h"

ZSM_LOGGING_CATEGORY(UDPWinReceiverLog, "Backend.Comms.UDPWinReceiver")

UDPWinReceiver::UDPWinReceiver(QObject *parent)
    : QObject(parent)
{
    zInfo(UDPWinReceiverLog) << "create";

    _link = LinkManager::instance()->udpWinLink();

    init();
}

UDPWinReceiver::~UDPWinReceiver()
{
    stop();
    zInfo(UDPWinReceiverLog) << "destory";
}

void UDPWinReceiver::init()
{
    if (_initNoError) return;

    // 1. initialise WSA socket
    if (!NetUtils::initWSA()) return;

    // 2. create Socket
    _socket = NetUtils::createSocket(WSA_FLAG_OVERLAPPED);
    if (_socket == INVALID_SOCKET) return;

    // 3. Bind socket to port
    // Defaults to 37880 per the fixed contract; override via Mavlink/UdpListenPort (e.g. 41010 on hosts where the WSL2/Hyper-V range reserves 37880).
    const quint16 udpListenPort = QSettings().value(QStringLiteral("Mavlink/UdpListenPort"), 37880).toUInt(); ///< 监听端口
    if (!NetUtils::bind(_socket, udpListenPort)) return;

    // 4. create iocp , bind to socket
    _hIOCP = NetUtils::createIOCP(_socket);
    if (0 == _hIOCP) return;

    // 5. post iocp receive
    if (!NetUtils::postIOCPRecvs(_socket, _recvBufferSize, _iocpPendingRecvs)) return;

    _initNoError = true;
}

bool UDPWinReceiver::sendData(const QByteArray& data, const SOCKADDR_IN& target)
{
    if (_socket == INVALID_SOCKET) return false;

    const int sent = ::sendto(
        _socket, data.constData(), static_cast<int>(data.size()), 0,
        reinterpret_cast<const sockaddr*>(&target), sizeof(target));
    if (sent == SOCKET_ERROR) {
        zWarning(UDPWinReceiverLog) << "sendto error:" << WSAGetLastError();
        return false;
    }
    return sent == data.size();
}

void UDPWinReceiver::stop()
{
    _isRunning.store(false, std::memory_order_release);

    // 通知 Loop 停止
    if (0 == ::PostQueuedCompletionStatus(_hIOCP, 0, 0, 0)) {
        zWarning(UDPWinReceiverLog) << "PostQueuedCompletionStatus error";
    }

    if (_socket != INVALID_SOCKET)
        closesocket(_socket);
    if (_hIOCP)
        CloseHandle(_hIOCP);

    WSACleanup();

    zInfo(UDPWinReceiverLog) << "socket closed";
}

void UDPWinReceiver::workLoop()
{
    _isRunning.store(true, std::memory_order_release);
    zInfo(UDPWinReceiverLog) << "UDP Receive Server Loop start wait";

    DWORD numberOfBytes = 0;
    ULONG_PTR completionKey = 0;
    LPOVERLAPPED pOverlapped = nullptr;
    BOOL result;

    while (true) {
        // 7. 等待 IOCP 完成通知 （阻塞）
        if (!GetQueuedCompletionStatus(_hIOCP, &numberOfBytes, &completionKey, &pOverlapped, INFINITE)) {

            const DWORD lastError = ::GetLastError();

            // 忽略 ERROR_PORT_UNREACHABLE
            //TODO 在多个 port 都 报错时会不会影响性能？
            if (lastError == 1234) continue;

            zWarning(UDPWinReceiverLog) << "receiver iocp wait error:" << lastError;
            continue;
        }
        if (!_isRunning || completionKey == 0) { break; }

        // 8. 处理接收的数据
        NetUtils::EXTENDED_OVERLAPPED* context = reinterpret_cast<NetUtils::EXTENDED_OVERLAPPED*>(pOverlapped);
        QByteArray bytes(context->wsaBuf.buf, numberOfBytes);

        //* 我不需要知道 IP PORT
        // QString ip = QString::fromLatin1(inet_ntoa(context->senderAddr.sin_addr));
        // quint16 port = ntohs(context->senderAddr.sin_port);
        // zTest() << "recv cnt:" << numberOfBytes << "ip:port" << QString("%1:%2").arg(ip).arg(port);

        //TODO 先放入缓存列表里, 批量投递, 减少线程的切换
        //TODO 改为 emit signals
        // QMetaObject::invokeMethod(_link, "_onDataReceived", Qt::QueuedConnection, Q_ARG(QByteArray, bytes), Q_ARG(SOCKADDR_IN, context->senderAddr));
        MavlinkDistributor::instance()->enqueueMsg(bytes);


        // 9. 投递下一次请求
        // 在函数调用中使用结构之前，应始终将此结构的任何未使用成员初始化为零。 否则，函数可能会失败并返回 ERROR_INVALID_PARAMETER。
        // https://learn.microsoft.com/zh-cn/windows/win32/api/minwinbase/ns-minwinbase-overlapped
        ZeroMemory(&context->overlapped, sizeof(OVERLAPPED));
        ZeroMemory(&context->senderAddr, sizeof(context->senderAddr));
        context->senderAddrLen = sizeof(sockaddr_in);
        context->flags = 0;
        numberOfBytes = 0;

        if (SOCKET_ERROR == ::WSARecvFrom(
            _socket, &(context->wsaBuf), 1, &numberOfBytes, &context->flags,
            reinterpret_cast<sockaddr*>(&context->senderAddr),
            &context->senderAddrLen, &context->overlapped, nullptr))
        {
            DWORD lastError = ::GetLastError();
            if (lastError != ERROR_IO_PENDING)
            {
                lastError = ::GetLastError();
                zWarning(UDPWinReceiverLog) << "was recv from error:" << lastError;
                //TODO 如何做处理？
            }
        }
    }

    zInfo(UDPWinReceiverLog) << "UDP Receive Server Loop quit";
}


