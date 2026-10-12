#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QMap>
#include <QStringList>

#include "VehicleStruct.h"
#include "BatteryStruct.h"
#include "GPSStruct.h"
#include "MavlinkLib.h"
#include "MockVehicleFTP.h"

class MockVehicleWorker;


class MockVehicle : public QObject
{
	Q_OBJECT
public:
	enum FailureMode_t
	{
		FailNone,													///< 没有失败
		FailParamNoResponseToRequestList,							///< 无法响应 PARAM_REQUEST_LIST
		FailMissingParamOnInitialRequest,							///< 在参数初始化请求中有参数未发送, 应该视为成功, 因为QGC将重新查询缺失的参数
		FailMissingParamOnAllRequests,								///< 在参数初始化请求中有参数未发送, QGC重试也会失败
		FailInitialConnectRequestMessageAutopilotVersionFailure,	///< REQUEST_MESSAGE:AUTOPILOT_VERSION 返回失败
		FailInitialConnectRequestMessageAutopilotVersionLost,		///< REQUEST_MESSAGE:AUTOPILOT_VERSION 成功, AUTOPILOT_VERSION 没有发送
		FailInitialConnectRequestMessageProtocolVersionFailure,     ///< REQUEST_MESSAGE:PROTOCOL_VERSION 返回失败
		FailInitialConnectRequestMessageProtocolVersionLost,		///<  REQUEST_MESSAGE:PROTOCOL_VERSION 成功, AUTOPILOT_VERSION 没有发送
	};

public:

	explicit MockVehicle(uint16_t vehicleId, QObject* parent = nullptr);
	~MockVehicle();

	VehicleStruct* vehicleStruct() { return &_struct; }

	void sendMessage(const QByteArray& packet);
	void sendMessage(const mavlink_message_t& message);

	/// getter ------------------------------------------------------------

	uint16_t vehicleId() const { return _struct.vehicleId; }
	uint8_t componentId() const { return _struct.componentId; }
	uint8_t autopilot() const { return _struct.autopilot; }
	MockVehicleFTP* mockVehicleFTP() const {return _mockVehicleFTP;}

	/// setter ------------------------------------------------------------

	void setCommLost(bool commLost);
	void setBatteryRemaining(int remaining);
	void setHeading(float  heading);

	/// function ----------------------------------------------------------

	/// called by worker
	void messageReceived(const mavlink_message_t& msg);

	void updatePosition(const QVector3D& position);							///< 单位: m

public slots:
	void run10HzTasks();
	void run1HzTasks();

	/// only get task will connect this Loop
	void run500HzTasks();

private:

	void _addToPackage(const mavlink_message_t& msg);
	void _sendMessage(const mavlink_message_t& msg);
	void _500HzTaskAdd();
	void _500HzTaskDelete();

	/// send msg ----------------------------------------------------------
	void _sendHeartBeat();
	void _sendBatteryStatus();
	void _sendSysStatus();
	void _sendGpsRawInt();
	void _sendGlobalPositionInt();
	void _sendAttitude();
	void _sendShowStatus();

	void _movePositionForTest();

	/// handle incoming msg -----------------------------------------------
	void _handleHeartBeat(const mavlink_message_t &msg);
	void _handleCommandLong(const mavlink_message_t &msg);
	void _handleParamRequestList(const mavlink_message_t &msg);
	bool _handleTakeoff(const mavlink_command_long_t &request);
	void _handleRequestDataStream(const mavlink_message_t &msg);
	void _handleFTP(const mavlink_message_t &msg);
	void _handleData16(const mavlink_message_t &msg);
	void _updateShowExecution();

	/// MAV_CMD -----------------------------------------------------------
	bool _handleRequestMessage(const mavlink_command_long_t& request , bool& noAck);

	/// MAV_CMD response---------------------------------------------------
	void _respondWithAutopilotVersion();
	void _paramRequestListWorker();
	float _floatUnionForParam(int componentId, const QString &paramName);
	


private:

	VehicleStruct _struct{};
	MockVehicleFTP *const _mockVehicleFTP = nullptr;

	const FailureMode_t _failureMode = FailNone;
	int _currentParamRequestListComponentIndex = -1;			///< 参数列表请求工作流的当前组件索引, -1 没有运行的工作
	int _currentParamRequestListParamIndex = -1;				///< 参数列表请求工作流的当前参数索引
	int _500HzTaskRef = 0;

	static constexpr const char *_failParam = "COM_FLTMODE6";
	bool _getPosition = false;
	bool _sendRequestData = true;								///< 由地面站请求 1Hz 的数据
	qint32 _showStartTimeGpsSec = -1;
	quint8 _showAuthorization = 0;
	quint8 _showStage = 0;
	qint64 _showTakeoffStartedAtMs = -1;

	const QMap<int, QMap<QString, QVariant>>& _mapParamName2Value;
	const QMap<int, QMap<QString, MAV_PARAM_TYPE>>& _mapParamName2MavParamType;
	const QList<int>& _parameterComponentIds;
	const QMap<int, QStringList>& _parameterNames;
	QByteArray _messagePackage;
	QElapsedTimer _runningTime;
	MockVehicleWorker* _parent;


};
