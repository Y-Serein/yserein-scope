#include "MockVehicleWorker.h"
#include "ZSMLoggingCategory.h"
#include "ZSMBackend.h"
#include "LinkManager.h"
#include "UDPWinLink.h"
#include "ZSMApplication.h"
#include "MockVehicleController.h"

#include <windows.h>
#include <QElapsedTimer>
#include <utility>


ZSM_LOGGING_CATEGORY(MockVehicleWorkerLog, "Backend.MockVehicle.MockVehicleWorker")

MockVehicleWorker::MockVehicleWorker(QString name,QObject *parent)
    : QObject(parent)
    , _name(name)
{
    zInfo(MockVehicleWorkerLog)  << "create";
}

MockVehicleWorker::~MockVehicleWorker()
{
    stop();
    zInfo(MockVehicleWorkerLog)  << "destroy";
}

void MockVehicleWorker::init()
{
    // init thread
    _isRunning = false;
    _thread = new QThread(this);
    _thread->setObjectName(_name);

    connect(_thread, &QThread::started, this, &MockVehicleWorker::_threadStart);
    connect(_thread, &QThread::finished, this, &MockVehicleWorker::_threadFinish);
}

void MockVehicleWorker::start()
{
    _isRunning = true;
    
    this->moveToThread(_thread);
    _thread->start();
}

void MockVehicleWorker::stop()
{
    if ( !_thread ) return ;

    zDebug(MockVehicleWorkerLog) << "start stopping";

    _isRunning = false;

    // 发送退出事件循环请求
    _thread->quit();

    // 发出 finish 信号

    // give the thread a bounded exit window; timeout rather than block shutdown forever
    if ( !_thread->wait(3000) ) {
        zWarning(MockVehicleWorkerLog) << "mock vehicle worker thread did not stop within 3s; continuing shutdown";
    }

    _thread->deleteLater();

    _thread = nullptr;

    zDebug(MockVehicleWorkerLog) << "start stopped";
}

bool MockVehicleWorker::contains(uint16_t vehicleId)
{
    std::lock_guard<std::mutex> _guard(_vehiclesMutex);

    return _vehicles.contains(vehicleId);
}

MockVehicle* MockVehicleWorker::getVehicle(int vehicleId)
{
    //TODO 读写锁
    std::lock_guard<std::mutex> _guard(_vehiclesMutex);
    auto it = _vehicles.find(vehicleId);
    if ( it == _vehicles.end() ) return nullptr;
    return it->second;
}

std::vector<MockVehicle *> MockVehicleWorker::getAllVehicles()
{
    std::lock_guard<std::mutex> _guard(_vehiclesMutex);
    std::vector<MockVehicle*> te_vehicles;
    te_vehicles.reserve(_vehicles.size());
    for (auto [_, vehicle] : _vehicles) {
        te_vehicles.push_back(vehicle);
    }
    return te_vehicles;
}

void MockVehicleWorker::addTask(std::function<void()> task)
{
    _taskQueue.enqueue(task);
}

void MockVehicleWorker::clearVehiclesTask()
{
    // Enqueued by the backend after previous create/update tasks from that producer.
    addTask([this]() {
        _snapshotGeneration->fetch_add(1);
        MavlinkMessage_t message;
        while (_msgRecvQueue.try_dequeue(message)) {}
        {
            std::lock_guard<std::mutex> guard(_vehiclesMutex);
            for (const auto& [id, vehicle] : _vehicles) delete vehicle;
            _vehicles.clear();
        }
        _bytesBuffer.clear();
        emit vehiclesCleared();
    });
}

bool MockVehicleWorker::addVehicleTask(uint16_t vehicleId)
{
    return _addVehicleTask(vehicleId, std::nullopt);
}

bool MockVehicleWorker::addVehicleTask(uint16_t vehicleId, const QVector3D& position)
{
    return _addVehicleTask(vehicleId, position);
}

bool MockVehicleWorker::_addVehicleTask(uint16_t vehicleId, const std::optional<QVector3D>& position)
{
    {   // 避免死锁，限制作用域
        std::lock_guard<std::mutex> _guard(_vehiclesMutex);
        if (_vehicles.size() >= MAX_VEHICLE_NUM) return false;
    }

    _taskQueue.enqueue([this, vehicleId, position]() {
        std::lock_guard<std::mutex> _guard(_vehiclesMutex);

        if (_vehicles.contains(vehicleId)) {
			zCritical(MockVehicleWorkerLog) << "contains same VehicleId:" << vehicleId;
            return;
        }

		MockVehicle* vehicle = new MockVehicle(vehicleId, this);
        if (position.has_value()) {
            vehicle->updatePosition(*position);
        }
        _vehicles[vehicleId] = vehicle;
        zDebug(MockVehicleWorkerLog) <<  "added new vehicle:" << vehicleId;
    });

    return true;
}

void MockVehicleWorker::messageReceived(const mavlink_message_t &message, uint16_t vehicleId)
{
    _msgRecvQueue.enqueue({message, vehicleId});
}

void MockVehicleWorker::messageReceived(const mavlink_message_t &message)
{
    _msgRecvQueue.enqueue({message,0});
}

void MockVehicleWorker::addUpdateVehiclePositionTask(const Pair_IdVector3D_t &pair_)
{
    // Resolve by ID in the worker; a queued raw pointer could outlive a clear.
    _taskQueue.enqueue([this, pair_]() {
        const auto it = _vehicles.find(pair_.first);
        if (it != _vehicles.end()) it->second->updatePosition(pair_.second);
    });
}

void MockVehicleWorker::addUpdateVehiclePositionsTask(std::vector<Pair_IdVector3D_t> positions)
{
    _taskQueue.enqueue([this, positions = std::move(positions)]() {
        int missing = 0;
        // All map mutations and this task execute in this worker's event loop.
        for (const auto& [id, position] : positions) {
            const auto it = _vehicles.find(static_cast<uint16_t>(id));
            if (it == _vehicles.end()) {
                ++missing;
                continue;
            }
            it->second->updatePosition(position);
        }
        if (missing) zWarning(MockVehicleWorkerLog) << "position update missing vehicles:" << missing;
    });
}

void MockVehicleWorker::addMessageToBuffer(const QByteArray &bytes)
{
    // _messageBuffer.push_back(bytes);
    // LinkManager::instance()->udpWinLink()->writeBytesThreadSafe(bytes);
    if ( _bytesBuffer.size() > 2048 )
    {
        LinkManager::instance()->udpWinLink()->writeBytesThreadSafe(_bytesBuffer);
        _bytesBuffer.clear();
    }

    _bytesBuffer.append(bytes);
}

void MockVehicleWorker::_threadStart()
{
    // 初始化 QTimer
    _500HzTimer = new QTimer(this);
    _500HzTimer->setInterval(2);
    connect(_500HzTimer, &QTimer::timeout, this, &MockVehicleWorker::_500HzLoop);
    _500HzTimer->start();

    _10HzTimer = new QTimer(this);
    _10HzTimer->setInterval(100);
    connect(_10HzTimer, &QTimer::timeout, this, &MockVehicleWorker::_10HzLoop);
    _10HzTimer->start();

    _1HzTimer = new QTimer(this);
    _1HzTimer->setInterval(1000);
    connect(_1HzTimer, &QTimer::timeout, this, &MockVehicleWorker::_1HzLoop);
    _1HzTimer->start();

    // _pToken = new UDPWinSendQueue_PToken(*LinkManager::instance()->udpWinLink()->sendMsgQueue());

    zDebug(MockVehicleWorkerLog) << "thread started";
}

void MockVehicleWorker::_threadFinish()
{
    // 析构 QTimer
    _500HzTimer->stop();
    _10HzTimer->stop();
    _1HzTimer->stop();

    // delete _pToken;

    zDebug(MockVehicleWorkerLog) << "thread finished";
}

void MockVehicleWorker::_10HzLoop()
{
    // 清理队列任务

    _handleMessageReceived();
    emit _10HzSignal();
    // Command responses are usually smaller than the bulk-send threshold.
    // Flush them on the same 10 Hz loop so vehicle initialization does not
    // wait for the 1 Hz telemetry flush.
    _sendBufferTask();
}

void MockVehicleWorker::_1HzLoop()
{
    emit _1HzSignal();
    _sendBufferTask();
    _pushVehicleToFrontendTask();
}

void MockVehicleWorker::_500HzLoop()
{
    emit _500HzSignal();
    _handleQueueTask();
}

void MockVehicleWorker::_handleQueueTask()
{
    QElapsedTimer budget;
    budget.start();
    std::function<void()> task;
    while (_isRunning && _taskQueue.try_dequeue(task)) {
        task();
        if (budget.elapsed() >= 4) break;
    }
}

void MockVehicleWorker::_sendBufferTask()
{
    if ( _bytesBuffer.isEmpty() ) return ;
    LinkManager::instance()->udpWinLink()->writeBytesThreadSafe(_bytesBuffer);
    _bytesBuffer.clear();
}

void MockVehicleWorker::_handleMessageReceived()
{
    std::vector<MavlinkMessage_t> buffer;
    {
        // 使用 vector 做缓存, 为了释放锁, MockVehicle 处理 message 的时间不占用锁
        std::lock_guard<std::mutex> _guard(_vehiclesMutex);
        MavlinkMessage_t entry;
        while (_msgRecvQueue.try_dequeue(entry)) {
            buffer.push_back(entry);
        }
    }

    for (const auto& entry : buffer) {
        if ( entry.sysid == 0 ) {
            // broadcast
            for (const auto& [id, vehicle] : _vehicles) {
                vehicle->messageReceived(entry.msg);
            }

        } else {
            auto it = _vehicles.find(entry.sysid);
            if ( it != _vehicles.end() ) {
                it->second->messageReceived(entry.msg);
            } else {
                zWarning(MockVehicleWorkerLog) << "distributor send a message to single vehicle which not store in this thread" << "vehicleId:" << entry.sysid;
            }
        }
    }
}

void MockVehicleWorker::_pushVehicleToFrontendTask()
{
    // Keep at most one pending full snapshot per worker if the UI is busy.
    const auto pending = _snapshotPending;
    if (pending->exchange(true)) return;
    std::vector<MockVehicle*> te_vehicles = getAllVehicles();
    std::vector<VehicleStruct> list;
    list.reserve(te_vehicles.size());

    for (const auto& vehicle : te_vehicles) {
        list.push_back(*vehicle->vehicleStruct());
    }

    //! 用 std::move 避免 list 拷贝
    const auto generation = _snapshotGeneration;
    const auto revision = generation->load();
    guiApp()->addTask([list_ = std::move(list), pending, generation, revision]() {
        if (generation->load() == revision)
            MockVehicleController::instance()->updataVehicleSnapshot(list_);
        pending->store(false);
    });
}


