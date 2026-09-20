#pragma once

#include <QObject>
#include <QTimer>
#include <QMetaType>
#include <QThread>

#include <atomic>
#include <unordered_set>
#include <vector>

#include "NetUtils.h"
// #include "ThreadSafeQueue.h"
#include "concurrentqueue.h"
#include "UDPWinSenderWorker.h"

class  UDPWinLink;

// struct MessageSend_t {
//     QByteArray bytes;
//     SOCKADDR_IN target;
// };

class UDPWinSender : public QObject
{
    Q_OBJECT
public:
    explicit UDPWinSender(QObject *parent = nullptr);
    ~UDPWinSender() override;

    void init();
    void stop();
    void start();

    void sendData(QByteArray &data, const SOCKADDR_IN &target);
    void sendData(std::vector<QByteArray> &vec, const SOCKADDR_IN &target);

private slots:
    void _1HzLoop();

private:
    bool _initNoError = false;

    SOCKET _socket;
    HANDLE _hIOCP = NULL;

    /// Sender Worker
    UDPWinSenderWorker* _worker{nullptr};
    QThread* _workerThread{nullptr};

    QTimer* _1HzTimer{nullptr};
    std::atomic<uint32_t> _request_package{0};
};
