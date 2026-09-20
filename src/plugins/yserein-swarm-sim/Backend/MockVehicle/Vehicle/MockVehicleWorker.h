
#pragma once

#include <QThread>
#include <mutex>
#include <unordered_map>
#include <QObject>
#include <QTimer>
#include <functional>
#include <vector>
#include <QByteArray>
#include <qloggingcategory.h>

#include "MockVehicle.h"
// #include "QueueNonBlocking.h"

#include <readerwriterqueue.h>

#include "concurrentqueue.h"
#include "ZSMTypes.h"
#include <optional>
// #include "UDPWinLink.h"

struct MavlinkMessage_t
{
    mavlink_message_t msg;
    uint16_t sysid;
};

Q_DECLARE_LOGGING_CATEGORY(MockVehicleWorkerLog)

class MockVehicleWorker : public QObject
{
    Q_OBJECT
public:

    explicit MockVehicleWorker(QString name, QObject* parent = nullptr);
    ~MockVehicleWorker();

    void init();
    void start();
    void stop();

    static int getMaxVehicleNum() { return MAX_VEHICLE_NUM; }

    /// Vehicle Map function (modify map must use those func) ----
    /// to protect map in multi threads --------------------------
    
    bool contains(uint16_t vehicleId);

    /// getter ---------------------------------------------------

    QString name() const {return _name;}
    bool isRunning() const {return _isRunning.load();}
    int vehicleNums() const {return _vehicleNums.load();}

    MockVehicle* getVehicle(int vehicleId);

    /// 在对所有的飞机进行操作时避免频繁加锁, 获取整个 list
    std::vector<MockVehicle*> getAllVehicles();

    /// setter --------------------------------------------------

    /// task ----------------------------------------------------

    void addTask(std::function<void()> task);

    /// only allow called by MockVehiclePool
    /// @return true : success ; false : encountered MAX_VEHICLE_NUM
    bool addVehicleTask(uint16_t vehicleId);

    /// only allow called by MockVehiclePool
    /// @brief add vehicle with initial local position (x,y,z meters)
    /// @return true : success ; false : encountered MAX_VEHICLE_NUM
    bool addVehicleTask(uint16_t vehicleId, const QVector3D& position);

    /// message has target system only send to the match vehicle
    void messageReceived(const mavlink_message_t& message, uint16_t vehicleId);

    /// broadcast message all vehicle need to handle
    void messageReceived(const mavlink_message_t& message);

    ///TODO 在大批量更新的时候会不断加锁可以优化
    /// receive task from pool
    void addUpdateVehiclePositionTask(const Pair_IdVector3D_t& pair_);

    /// send message buffer -------------------------------------

    void addMessageToBuffer(const QByteArray& bytes);

signals:
    void _10HzSignal();
    void _1HzSignal();
    void _500HzSignal();

private slots:

    void _threadStart();
    void _threadFinish();
    
    void _10HzLoop();
    void _1HzLoop();
    void _500HzLoop();

    void _handleQueueTask();
    void _sendBufferTask();
    void _handleMessageReceived();

private:
    /// shared create task body; position has value -> set initial position
    bool _addVehicleTask(uint16_t vehicleId, const std::optional<QVector3D>& position);

    /// 让前端线程发送更新 MockVehiclePropertys
    void _pushVehicleToFrontendTask();

private:
    QThread* _thread = nullptr;
    QTimer* _10HzTimer = nullptr;
    QTimer* _1HzTimer = nullptr;
    QTimer* _500HzTimer = nullptr;

    QString _name;
    std::atomic_bool _isRunning;
    std::atomic_int _vehicleNums = 0;

    std::mutex _vehiclesMutex;                                      ///< 跨线程访问（比如是否已添加了这个 Ids）
    std::unordered_map<uint16_t/* id */, MockVehicle*> _vehicles;

    moodycamel::ConcurrentQueue<std::function<void()>> _taskQueue;
    moodycamel::ReaderWriterQueue<MavlinkMessage_t> _msgRecvQueue;
    QByteArray _bytesBuffer;

    static constexpr int MAX_VEHICLE_NUM = 1010;        ///TODO Settings
};



