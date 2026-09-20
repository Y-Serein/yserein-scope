//
// Created by Hoppz on 25-10-10.
//

#include "UDPWinSenderWorker.h"

UDPWinSenderWorker::UDPWinSenderWorker(HANDLE hIOCP, QObject *parent)
    : QObject(parent)
    , _hIOCP(hIOCP)
{
    init();
    _successPackage.store(0, std::memory_order_release);
}

UDPWinSenderWorker::~UDPWinSenderWorker()
{
    stop();
}

void UDPWinSenderWorker::init()
{
    // do nothing
}

void UDPWinSenderWorker::stop()
{
    _isRunning.store(false, std::memory_order_release);

    // 通知 Loop 停止
    if (0 == ::PostQueuedCompletionStatus(_hIOCP, 0, 0, 0)) {
        qWarning() << "PostQueuedCompletionStatus error";
    }
}

void UDPWinSenderWorker::workLoop()
{
    _isRunning.store(true, std::memory_order_release);
    qInfo() << "UDP Send Worker Loop start wait";

    DWORD numberOfBytes = 0;
    ULONG_PTR completionKey = 0;
    LPOVERLAPPED pOverlapped = nullptr;
    BOOL result;

    while (true) {

        if (!GetQueuedCompletionStatus(_hIOCP, &numberOfBytes, &completionKey, &pOverlapped, INFINITE)) {

            const DWORD lastError = ::GetLastError();

            // 忽略 ERROR_PORT_UNREACHABLE
            //TODO 在多个 port 都 报错时会不会影响性能？
            if (lastError == 1234) continue;

            qWarning() << "sender iocp wait error:" << lastError;
            continue;
        }

        if (!_isRunning || completionKey == 0) { break; }

        SendContext* pSendContext = reinterpret_cast<SendContext*>(pOverlapped);
        delete pSendContext;

        // qDebug() << "[Send Worker]" << "send completed.";
        _successPackage.fetch_add(1, std::memory_order_release);
    }
    qDebug() << "[Send Worker]" << "loop quit";
}
