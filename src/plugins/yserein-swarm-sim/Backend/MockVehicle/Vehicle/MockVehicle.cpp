#include "MockVehicle.h"
#include "ZSMLoggingCategory.h"
#include "LinkManager.h"
#include "UDPWinLink.h"
#include "MockVehicleWorker.h"
#include "MockVehicleManager.h"
#include "ZSMGeo.h"
#include "DroneShowPlayerController.h"
#include "MavlinkProtocol.h"
#include "StandardMavlinkCodec.h"

#include <QFile>
#include <QRandomGenerator>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cmath>
#include <vector>

#include "Geo/ZSMGeo.h"
#include "Show3D/DroneShowPlayerController.h"

ZSM_LOGGING_CATEGORY(MockVehicleLog, "Backend.MockVehicle.MockVehicle")
ZSM_LOGGING_CATEGORY(MockVehicleVerboseLog,"Backend.MockVehicle.MockVehicleVerbose")

namespace {

constexpr uint8_t kGcsToDroneData16Type = 0x5C;
constexpr uint8_t kDroneToGcsData16Type = 0x5B;
constexpr uint8_t kStartConfigOpcode = 1;
constexpr uint8_t kAuthorizationGrantedLive = 1;
constexpr uint8_t kShowStageOff = 0;
constexpr uint8_t kShowStageWaitForStart = 2;
constexpr uint8_t kShowStageTakingOff = 3;
constexpr uint8_t kShowStagePerforming = 4;
constexpr qint32 kGpsWeekSeconds = 7 * 24 * 60 * 60;
constexpr qint64 kSimulatorTakeoffDurationMs = 1000;
constexpr uint32_t kDroneShowCustomMode = 127;

qint32 currentGpsTimeOfWeek()
{
    const auto now = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
    const auto gpsTime = std::chrono::clock_cast<std::chrono::gps_clock>(now);
    const auto gpsWeek = std::chrono::floor<std::chrono::weeks>(gpsTime);
    return static_cast<qint32>(std::chrono::duration_cast<std::chrono::seconds>(gpsTime - gpsWeek).count());
}

qint32 secondsUntilGpsTime(qint32 target, qint32 current)
{
    qint32 difference = target - current;
    if (difference > kGpsWeekSeconds / 2) {
        difference -= kGpsWeekSeconds;
    } else if (difference < -kGpsWeekSeconds / 2) {
        difference += kGpsWeekSeconds;
    }
    return difference;
}

} // namespace

namespace {

struct SharedParameters {
    QMap<int, QMap<QString, QVariant>> values;
    QMap<int, QMap<QString, MAV_PARAM_TYPE>> types;
    QList<int> componentIds;
    QMap<int, QStringList> names;
};

const SharedParameters& sharedParameters()
{
    // Every mock vehicle uses the same APM Copter parameter defaults. The
    // function-local static is initialized once, even when workers start in
    // parallel, and stays immutable after construction.
    static const SharedParameters parameters = [] {
        SharedParameters result;
        QFile paramFile(QStringLiteral(":/APM/Copter.OfflineEditing.params"));
        const bool success = paramFile.open(QFile::ReadOnly);
        Q_ASSERT(success);
        if (!success) return result;

        QTextStream paramStream(&paramFile);
        while (!paramStream.atEnd()) {
            const QString line = paramStream.readLine();
            if (line.startsWith("#")) continue;

            const QStringList paramData = line.split("\t");
            assert(paramData.size() == 5);
            const int compId = paramData.at(1).toInt();
            const QString paramName = paramData.at(2);
            const QString valStr = paramData.at(3);
            const uint paramType = paramData.at(4).toUInt();

            QVariant paramValue;
            switch (paramType) {
                case MAV_PARAM_TYPE_REAL32: paramValue = QVariant(valStr.toFloat()); break;
                case MAV_PARAM_TYPE_UINT32: paramValue = QVariant(valStr.toUInt()); break;
                case MAV_PARAM_TYPE_INT32: paramValue = QVariant(valStr.toInt()); break;
                case MAV_PARAM_TYPE_UINT16: paramValue = QVariant((quint16)valStr.toUInt()); break;
                case MAV_PARAM_TYPE_INT16: paramValue = QVariant((qint16)valStr.toInt()); break;
                case MAV_PARAM_TYPE_UINT8: paramValue = QVariant((quint8)valStr.toUInt()); break;
                case MAV_PARAM_TYPE_INT8: paramValue = QVariant((qint8)valStr.toInt()); break;
                default:
                    qCCritical(MockVehicleVerboseLog) << "Unknown type" << paramType;
                    paramValue = QVariant(valStr.toInt());
                    break;
            }

            zDebug(MockVehicleVerboseLog) << "Loading param" << paramName << paramValue;
            result.values[compId][paramName] = paramValue;
            result.types[compId][paramName] = static_cast<MAV_PARAM_TYPE>(paramType);
        }
        for (auto it = result.values.cbegin(); it != result.values.cend(); ++it) {
            result.componentIds.append(it.key());
            result.names.insert(it.key(), it.value().keys());
        }
        return result;
    }();
    return parameters;
}

} // namespace

MockVehicle::MockVehicle(uint16_t vehicleId, QObject* parent)
	: _mockVehicleFTP ( new MockVehicleFTP(vehicleId , MAV_COMP_ID_AUTOPILOT1, this))
	, _mapParamName2Value(sharedParameters().values)
	, _mapParamName2MavParamType(sharedParameters().types)
	, _parameterComponentIds(sharedParameters().componentIds)
	, _parameterNames(sharedParameters().names)
{
	// zInfo(MockVehicleLog) << "create Vehicle:" << vehicleId;

	_parent = qobject_cast<MockVehicleWorker*>(parent);

	// 初始化 VehicleStruct
	_struct.vehicleId = vehicleId;
	_struct.componentId = MAV_COMP_ID_AUTOPILOT1;
	_struct.type = MAV_TYPE_QUADROTOR;
	_struct.autopilot = MAV_AUTOPILOT_ARDUPILOTMEGA;
	_struct.base_mode = MAV_MODE_FLAG_MANUAL_INPUT_ENABLED | MAV_MODE_FLAG_CUSTOM_MODE_ENABLED;
	_struct.custom_mode = 0;					///> Stabilize
	_struct.system_status = MAV_STATE_STANDBY;
	// Simulation-only hardware identity. These values are deliberately stable
	// and non-zero so the production-shaped local safety gate can distinguish
	// every mock vehicle without pretending to be a real flight controller.
	_struct.boardVendorId = 0x5953;   // "YS"
	_struct.boardProductId = 0x534D;  // "SM"

	_struct.yaw = 0;

	// _struct.vehiclePosition = QGeoCoordinate(MockVehicleManager::defaultLatitude() + vehicleId * 0.0000001,
	// 										MockVehicleManager::defaultLongitude() + vehicleId * 0.0000001);
	// _struct.homePosition = _struct.vehiclePosition;

	_struct.gps.latitude = MockVehicleManager::defaultLatitude() + vehicleId * 0.0000001;
	_struct.gps.longitude = MockVehicleManager::defaultLongitude() + vehicleId * 0.0000001;
	_struct.gps.altitudeRelative = _struct.gps.altitudeAMSL = 0;

	static constexpr char kSimulationUidPrefix[] = "YSEREINSIM";
	for (int i = 0; i < 10; ++i) {
		_struct.uid2[i] = static_cast<uint8_t>(kSimulationUidPrefix[i]);
	}
	_struct.uid2[10] = 0x01;
	_struct.uid2[11] = 0x00;
	_struct.uid2[12] = static_cast<uint8_t>((vehicleId >> 8) & 0xFF);
	_struct.uid2[13] = static_cast<uint8_t>(vehicleId & 0xFF);
	_struct.uid2[14] = 'M';
	_struct.uid2[15] = 'O';
	_struct.uid2[16] = 'C';
	_struct.uid2[17] = 'K';

	// 初始化 BatteryStruct
	_struct.battery.id = 1;
	_struct.battery.battery_function = MAV_BATTERY_FUNCTION_ALL;

	// Each pack starts above 90%, with independent simulated charge.
	_struct.battery.battery_remaining = static_cast<int8_t>(QRandomGenerator::global()->bounded(91, 100));
	_struct.battery.time_remaining = -1; // no discharge model
	_struct.battery.charge_state = MAV_BATTERY_CHARGE_STATE_OK;
	_struct.battery.type = MAV_BATTERY_TYPE_LIPO;
	_struct.battery.temperature = 2100;
	_struct.battery.current_battery = 600;
	_struct.battery.current_consumed = 100;
	_struct.battery.energy_consumed = -1;							// not supported
	_struct.battery.mode = MAV_BATTERY_MODE_UNKNOWN;
	_struct.battery.fault_bitmask = 0;

	// 初始化 GPSStruct
	_struct.gps.fix_type = GPS_FIX_TYPE_NO_GPS;

	for (int i = 0; i < 10; i++) _struct.battery.voltages[i] = UINT16_MAX;
	const auto cellVoltage = static_cast<uint16_t>(4100 + (_struct.battery.battery_remaining - 91) * 10);
	_struct.battery.voltages[0] = _struct.battery.voltages[1] = _struct.battery.voltages[2] = cellVoltage;
	_runningTime.start();

	// 初始化 FTP
	;

	// connect
	connect(_parent, &MockVehicleWorker::_10HzSignal, this, &MockVehicle::run10HzTasks);
	connect(_parent, &MockVehicleWorker::_1HzSignal, this, &MockVehicle::run1HzTasks);
}

MockVehicle::~MockVehicle()
{
	// zInfo(MockVehicleLog) << "destory Vehicle:" << _struct.vehicleId;
}

void MockVehicle::sendMessage(const QByteArray &packet)
{
	_parent->addMessageToBuffer(packet);
}

void MockVehicle::sendMessage(const mavlink_message_t &message)
{
	_sendMessage(message);
}

void MockVehicle::setCommLost(bool commLost)
{
	_struct.commLost = commLost;
}

void MockVehicle::setBatteryRemaining(int remaining)
{
	if ( remaining > 100 || remaining < -1 ) return ;
	if ( remaining == _struct.battery.battery_remaining ) return ;
	zInfo(MockVehicleLog) << "get battery" << remaining;
	_struct.battery.battery_remaining = static_cast<int8_t>(remaining);
}

void MockVehicle::setHeading(float heading)
{
	if ( heading < 0. || heading > 360.0f ) return ;
	zInfo(MockVehicleLog) << "get heading" << heading;
	_struct.yaw = heading;
}

void MockVehicle::messageReceived(const mavlink_message_t &msg)
{
	// zDebug(MockVehicleLog) << "get msg:" << msg.msgid;

	switch (msg.msgid) {
		case MAVLINK_MSG_ID_HEARTBEAT:
			_handleHeartBeat(msg);
			break;
		case MAVLINK_MSG_ID_COMMAND_LONG:
			_handleCommandLong(msg);
			break;
		case MAVLINK_MSG_ID_PARAM_REQUEST_LIST:
			_handleParamRequestList(msg);
			break;
		case MAVLINK_MSG_ID_REQUEST_DATA_STREAM:
			_handleRequestDataStream(msg);
			break;
		case MAVLINK_MSG_ID_FILE_TRANSFER_PROTOCOL:
			_handleFTP(msg);
			break;
		case MAVLINK_MSG_ID_DATA16:
			_handleData16(msg);
			break;
		default:
			break;
	}
}

void MockVehicle::updatePosition(const QVector3D &position)
{
	QGeoCoordinate coor = ZSMGeo::convertXYZToGeoFromOrigin(position.x(),position.y(),position.z(),
		MockVehicleManager::defaultLocation());

	_struct.gps.x = position.x();
	_struct.gps.y = position.y();
	_struct.gps.z = position.z();

	// zInfo(MockVehicleLog) << "x:" << _struct.gps.x << "y:" << _struct.gps.y << "z:" << _struct.gps.z;

	_struct.gps.latitude = coor.latitude();
	_struct.gps.longitude = coor.longitude();

	// 先让这两个相等
	_struct.gps.altitudeAMSL = _struct.gps.altitudeRelative = coor.altitude();
	_struct.gps.fix_type = GPS_FIX_TYPE_DGPS;

	if ( !_getPosition ) _getPosition = true;
}

void MockVehicle::run10HzTasks()
{
	_updateShowExecution();
}

void MockVehicle::run1HzTasks()
{
	_messagePackage.clear();
	_sendHeartBeat();
	_sendShowStatus();

	if ( _sendRequestData ) {

		_sendBatteryStatus();
		_sendSysStatus();			///< 要在 Battery 之后（会用到 Battery 的信息）
		_sendAttitude();

		if ( _getPosition ) {
			// _movePositionForTest();

			_sendGpsRawInt();
			_sendGlobalPositionInt();

		}
	}


	sendMessage(_messagePackage);
}

void MockVehicle::run500HzTasks()
{
	_paramRequestListWorker();
}

void MockVehicle::_addToPackage(const mavlink_message_t &msg)
{
	if (_struct.commLost) return;

	if (zsm::currentProtocolDomain() == zsm::ProtocolDomain::Standard8) {
		std::vector<uint8_t> bytes;
		if (!zsm::encodeStandard8Packet(msg, zsm::WireVersion::Mavlink2, bytes)) {
			zWarning(MockVehicleLog) << "standard8 encode rejected periodic message" << msg.msgid
			                         << "for vehicle" << _struct.vehicleId;
			return;
		}
		_messagePackage.append(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size()));
		return;
	}

	uint8_t buffer[MAVLINK_MAX_PACKET_LEN];
	const uint16_t cBuffer = mavlink_msg_to_send_buffer(buffer, &msg);
	_messagePackage.append(reinterpret_cast<const char*>(buffer), cBuffer);
}

void MockVehicle::_sendMessage(const mavlink_message_t &msg)
{
	if (_struct.commLost) return;

	QByteArray data;
	if (zsm::currentProtocolDomain() == zsm::ProtocolDomain::Standard8) {
		std::vector<uint8_t> bytes;
		if (zsm::encodeStandard8Packet(msg, zsm::WireVersion::Mavlink2, bytes)) {
			data = QByteArray(reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()));
		} else {
			zWarning(MockVehicleLog) << "standard8 encode rejected message" << msg.msgid << "for vehicle" << _struct.vehicleId;
			return;
		}
	} else {
		uint8_t buffer[MAVLINK_MAX_PACKET_LEN];
		uint16_t cBuffer = mavlink_msg_to_send_buffer(buffer, &msg);
		data = QByteArray(reinterpret_cast<char*>(buffer), cBuffer);
	}

	sendMessage(data);
}

void MockVehicle::_500HzTaskAdd()
{
	if ( _500HzTaskRef++ == 0 ) {
		// qDebug(MockVehicleLog) << "500Hz Task connect";
		connect(_parent, &MockVehicleWorker::_500HzSignal, this, &MockVehicle::run500HzTasks);
	}
}

void MockVehicle::_500HzTaskDelete()
{
	if ( --_500HzTaskRef == 0 ) {
		// qDebug(MockVehicleLog) << "500Hz Task disconnect";
		disconnect(_parent, &MockVehicleWorker::_500HzSignal, this, &MockVehicle::run500HzTasks);
	}
}


void MockVehicle::_sendHeartBeat()
{
	mavlink_message_t msg{};

	mavlink_msg_heartbeat_pack_chan(
		_struct.vehicleId,
		_struct.componentId,
		ZSM_MAVLINK_COMM,
		&msg,
		_struct.type,			/// MAV_TYPE
		_struct.autopilot,		/// MAV_AUTOPILOT
		_struct.base_mode,		/// MAV_MODE
		_struct.custom_mode,	/// custom mode
		_struct.system_status	/// MAV_STATE
	);
	_addToPackage(msg);
}

void MockVehicle::_sendBatteryStatus()
{
	if (_struct.battery.battery_remaining < 0) {
		_struct.battery.charge_state = MAV_BATTERY_CHARGE_STATE_UNDEFINED;
	} else if (_struct.battery.battery_remaining > 50) {
		_struct.battery.charge_state = MAV_BATTERY_CHARGE_STATE_OK;
	} else if (_struct.battery.battery_remaining > 30) {
		_struct.battery.charge_state = MAV_BATTERY_CHARGE_STATE_LOW;
	} else if (_struct.battery.battery_remaining > 20) {
		_struct.battery.charge_state = MAV_BATTERY_CHARGE_STATE_CRITICAL;
	} else {
		_struct.battery.charge_state = MAV_BATTERY_CHARGE_STATE_EMERGENCY;
	}

	mavlink_message_t msg{};

	mavlink_msg_battery_status_pack_chan(
		_struct.vehicleId,
		_struct.componentId,
		ZSM_MAVLINK_COMM,
		&msg,
		_struct.battery.id,
		_struct.battery.battery_function,
		_struct.battery.type,
		_struct.battery.temperature,
		_struct.battery.voltages,
		_struct.battery.current_battery,
		_struct.battery.current_consumed,
		_struct.battery.energy_consumed,
		_struct.battery.battery_remaining,
		_struct.battery.time_remaining,
		_struct.battery.charge_state,
		_struct.battery.voltages_ext,
		_struct.battery.mode,
		_struct.battery.fault_bitmask
	);

	_addToPackage(msg);
}

void MockVehicle::_sendSysStatus()
{
	mavlink_message_t msg{};

	static const uint32_t _all = 0xffffffff;

	mavlink_msg_sys_status_pack_chan(
		_struct.vehicleId,
		_struct.componentId,
		ZSM_MAVLINK_COMM,
		&msg,
		_all,  // onboard_control_sensors_present
		_all,                          // onboard_control_sensors_enabled
		_all,                          // onboard_control_sensors_health
		250,                        // load
		static_cast<uint16_t>(_struct.battery.voltages[0] + _struct.battery.voltages[1] + _struct.battery.voltages[2]),
		_struct.battery.current_battery,                       // current_battery
		_struct.battery.battery_remaining,      // battery_remaining
		0,0,0,0,0,0,0,0,0
	);

	_addToPackage(msg);
}

void MockVehicle::_sendGpsRawInt()
{
	mavlink_message_t msg{};
	mavlink_msg_gps_raw_int_pack_chan(
		_struct.vehicleId,
		_struct.componentId,
		ZSM_MAVLINK_COMM,
		&msg,
		static_cast<uint64_t>(_runningTime.elapsed()) * 1000ULL,			///> microseconds since simulator start
		_struct.gps.fix_type,
		static_cast<int32_t>(_struct.gps.latitude * 1E7),
		static_cast<int32_t>(_struct.gps.longitude * 1E7),
		static_cast<int32_t>(_struct.gps.altitudeAMSL * 1000),
		3 * 100,												///> hdop
		3 * 100,												///> vdop
		UINT16_MAX,												///> velocity not known
		UINT16_MAX,												///> course over ground not known
		32,														///> sat visible
		//-- Extension
		0,                                      // Altitude (above WGS84, EGM96 ellipsoid), in meters * 1000 (positive for up).
		0,                                      // Position uncertainty in meters * 1000 (positive for up).
		0,                                      // Altitude uncertainty in meters * 1000 (positive for up).
		0,                                      // Speed uncertainty in meters * 1000 (positive for up).
		0,                                      // Heading / track uncertainty in degrees * 1e5.
		65535                                   // Yaw not provided
		);

	_addToPackage(msg);
}

void MockVehicle::_sendGlobalPositionInt()
{
	mavlink_message_t msg{};

	mavlink_msg_global_position_int_pack_chan(
		_struct.vehicleId,
		_struct.componentId,
		ZSM_MAVLINK_COMM,
		&msg,
		static_cast<uint64_t>(_runningTime.elapsed()),			///> unit : ms 先就这样吧
		static_cast<int32_t>(_struct.gps.latitude * 1E7),
		static_cast<int32_t>(_struct.gps.longitude * 1E7),
		static_cast<int32_t>(_struct.gps.altitudeAMSL * 1000),
		static_cast<int32_t>(_struct.gps.altitudeRelative * 1000),
		0,0,0,															///> no speed sent
		UINT16_MAX														///> not
		);

	_addToPackage(msg);
}

void MockVehicle::_sendAttitude()
{
	mavlink_message_t msg{};

	// update yaw
	// _struct.yaw += 15;
	// if ( _struct.yaw >= 360 ) {
	// 	_struct.yaw = 0;
	// }

	float yawRad = qDegreesToRadians(_struct.yaw);

	mavlink_msg_attitude_pack_chan(
		_struct.vehicleId,
		_struct.componentId,
		ZSM_MAVLINK_COMM,
		&msg,
		static_cast<uint64_t>(_runningTime.elapsed()),
		0.0,
		0.0,
		yawRad,
		0.0,
		0.0,
		0.0
		);

	_addToPackage(msg);
}

void MockVehicle::_sendShowStatus()
{
	uint8_t packet[16]{}; // DATA16 packer copies all 16 data bytes.
	std::memcpy(packet, &_showStartTimeGpsSec, sizeof(_showStartTimeGpsSec));

	uint8_t flags = 0;
	if (_mockVehicleFTP->hasUploadedShowFile()) flags |= 1U << 7U;
	if (_showStartTimeGpsSec >= 0) flags |= 1U << 6U;
	if (_showAuthorization != 0) flags |= 1U << 2U;
	packet[6] = flags;
	packet[7] = _showStage & 0x0FU;
	packet[8] = static_cast<uint8_t>((10U << 3U) | GPS_FIX_TYPE_DGPS);
	packet[9] = static_cast<uint8_t>((_showAuthorization & 0x03U) << 2U);

	qint32 elapsedTime = 0;
	if (_showStartTimeGpsSec >= 0) {
		elapsedTime = -secondsUntilGpsTime(_showStartTimeGpsSec, currentGpsTimeOfWeek());
	}
	const qint16 encodedElapsedTime = static_cast<qint16>(std::clamp(elapsedTime, qint32(-32768), qint32(32767)));
	std::memcpy(&packet[10], &encodedElapsedTime, sizeof(encodedElapsedTime));

	mavlink_message_t msg{};
	mavlink_msg_data16_pack_chan(
		_struct.vehicleId,
		_struct.componentId,
		ZSM_MAVLINK_COMM,
		&msg,
		kDroneToGcsData16Type,
		14, // used status bytes; the remaining DATA16 bytes are zero padding
		packet);
	_addToPackage(msg);
}

void MockVehicle::_movePositionForTest()
{
	// 每秒向北移动1米
    // 北向移动意味着纬度增加
    // 1度纬度约等于111,320米，所以1米约等于1/111320度
    static const double metersToLatitude = 1.0 / 111320.0;

	_struct.gps.latitude += metersToLatitude;
}

void MockVehicle::_handleHeartBeat(const mavlink_message_t &msg)
{
	Q_UNUSED(msg);
	// zDebug(MockVehicleLog) << "HeartBeat" << _struct.vehicleId;
}

void MockVehicle::_handleCommandLong(const mavlink_message_t &msg)
{
	mavlink_command_long_t request{};
	mavlink_msg_command_long_decode(&msg, &request);

	uint8_t commandResult = MAV_RESULT_UNSUPPORTED;

	switch (request.command){
		case MAV_CMD_REQUEST_MESSAGE:
		{
			bool noAck = false;
			if ( _handleRequestMessage(request, noAck) ) {
				if ( noAck ) {
					return ;
				}
				commandResult = MAV_RESULT_ACCEPTED;
			}
			break;
		}
		case MAV_CMD_COMPONENT_ARM_DISARM:
			// zDebug(MockVehicleLog) << "vehicle:" << _struct.vehicleId << "get ARM_DISARM";
			if (request.param1 == 0.0f) {
				_struct.base_mode &= ~MAV_MODE_FLAG_SAFETY_ARMED;
			} else {
				_struct.base_mode |= MAV_MODE_FLAG_SAFETY_ARMED;
			}
			commandResult = MAV_RESULT_ACCEPTED;
			break;
		case MAV_CMD_DO_SET_MODE:
		{
			const MAV_MODE _base_mode = (MAV_MODE)request.param1;
			const uint32_t _custom_mode = (uint32_t)request.param2;
			if ( _base_mode &  MAV_MODE_FLAG_CUSTOM_MODE_ENABLED) {
				_struct.custom_mode = _custom_mode;
				commandResult = MAV_RESULT_ACCEPTED;
			}  else {
				commandResult = MAV_RESULT_DENIED;
			}

			if (commandResult == MAV_RESULT_ACCEPTED &&
				(_struct.custom_mode == 6 || _struct.custom_mode == 9 || _struct.custom_mode == 21))
			{
				updatePosition(QVector3D(_struct.gps.x, _struct.gps.y, 0.0f));
				_struct.base_mode &= ~MAV_MODE_FLAG_SAFETY_ARMED;
				DroneShowPlayerController::instance()->updateTakeoffVehicle(_struct.vehicleId, _struct.gps.altitudeRelative);
			}

			break;
		}
		case MAV_CMD_NAV_TAKEOFF:
			commandResult = _handleTakeoff(request) ? MAV_RESULT_ACCEPTED : MAV_RESULT_DENIED;
			break;
		default:
			break;
	}
	mavlink_message_t commandAck{};
	(void) mavlink_msg_command_ack_pack_chan(
		_struct.vehicleId,
		_struct.componentId,
		ZSM_MAVLINK_COMM,
		&commandAck,
		request.command,
		commandResult,
		0,    // progress
		0,    // result_param2
		0,    // target_system
		0     // target_component
	);
	_sendMessage(commandAck);
}

void MockVehicle::_handleParamRequestList(const mavlink_message_t &msg)
{
	if (_failureMode == FailParamNoResponseToRequestList) {
		return;
	}

	// qDebug(MockVehicleLog) << "received param request list" << "msgid:" << msg.msgid;

	mavlink_param_request_list_t request{};
	mavlink_msg_param_request_list_decode(&msg, &request);

	assert(request.target_system == _struct.vehicleId);
	assert(request.target_component == MAV_COMP_ID_ALL);

	// Start the worker routine
	_currentParamRequestListComponentIndex = 0;
	_currentParamRequestListParamIndex = 0;

	//TODO when work finished disconnect!
	_500HzTaskAdd();
}

bool MockVehicle::_handleTakeoff(const mavlink_command_long_t &request)
{
	if (!std::isfinite(request.param7) || request.param7 <= 0.0f) return false;
	const float targetAltitude = std::max(static_cast<float>(_struct.gps.z), request.param7);
	updatePosition(QVector3D(_struct.gps.x, _struct.gps.y, targetAltitude));
	_struct.base_mode |= MAV_MODE_FLAG_SAFETY_ARMED;
	DroneShowPlayerController::instance()->updateTakeoffVehicle(_struct.vehicleId, _struct.gps.altitudeRelative);
	return true;
}

void MockVehicle::_handleRequestDataStream(const mavlink_message_t &msg)
{
	mavlink_request_data_stream_t request{};
	mavlink_msg_request_data_stream_decode(&msg, &request);

	assert(request.target_system == _struct.vehicleId);
	//assert(request.target_component == MAV_COMP_ID_ALL);

	if ( request.req_stream_id == MAV_DATA_STREAM_EXTRA3 && request.start_stop == 1) {
		_sendRequestData = true;
	}

}

void MockVehicle::_handleFTP(const mavlink_message_t &msg)
{
	_mockVehicleFTP->mavlinkMessageReceived(msg);
}

void MockVehicle::_handleData16(const mavlink_message_t &msg)
{
	mavlink_data16_t data16{};
	mavlink_msg_data16_decode(&msg, &data16);
	if (data16.type != kGcsToDroneData16Type || data16.len < 6 || data16.data[0] != kStartConfigOpcode) {
		return;
	}

	qint32 requestedStartTime = _showStartTimeGpsSec;
	qint32 encodedStartTime = -1;
	std::memcpy(&encodedStartTime, &data16.data[1], sizeof(encodedStartTime));
	if (encodedStartTime < 0) {
		requestedStartTime = -1;
	} else if (encodedStartTime < kGpsWeekSeconds) {
		requestedStartTime = encodedStartTime;
	}

	const quint8 requestedAuthorization = data16.data[5];
	if (requestedAuthorization == kAuthorizationGrantedLive && !_mockVehicleFTP->hasUploadedShowFile()) {
		return;
	}
	if (requestedStartTime == _showStartTimeGpsSec && requestedAuthorization == _showAuthorization) {
		return;
	}

	_showStartTimeGpsSec = requestedStartTime;
	_showAuthorization = requestedAuthorization;
	_showTakeoffStartedAtMs = -1;

	if (_showAuthorization == kAuthorizationGrantedLive && _showStartTimeGpsSec >= 0) {
		_struct.custom_mode = kDroneShowCustomMode;
		_showStage = kShowStageWaitForStart;
		return;
	}

	_struct.base_mode &= ~MAV_MODE_FLAG_SAFETY_ARMED;
	_showStage = _showStartTimeGpsSec >= 0 ? kShowStageWaitForStart : kShowStageOff;
}

void MockVehicle::_updateShowExecution()
{
	if (_showAuthorization != kAuthorizationGrantedLive || _showStartTimeGpsSec < 0) {
		return;
	}

	const qint32 secondsUntilStart = secondsUntilGpsTime(_showStartTimeGpsSec, currentGpsTimeOfWeek());
	if (_showStage == kShowStageWaitForStart) {
		if (secondsUntilStart <= 8) {
			_struct.base_mode |= MAV_MODE_FLAG_SAFETY_ARMED;
		}
		if (secondsUntilStart <= 0) {
			_showStage = kShowStageTakingOff;
			_showTakeoffStartedAtMs = _runningTime.elapsed();
			DroneShowPlayerController::instance()->requestStartFromGroundStation();
		}
		return;
	}

	if (_showStage == kShowStageTakingOff
		&& _showTakeoffStartedAtMs >= 0
		&& _runningTime.elapsed() - _showTakeoffStartedAtMs >= kSimulatorTakeoffDurationMs) {
		_showStage = kShowStagePerforming;
	}
}

bool MockVehicle::_handleRequestMessage(const mavlink_command_long_t &request, bool &noAck)
{
	noAck = false;

	switch (static_cast<int>(request.param1)) {
		case MAVLINK_MSG_ID_AUTOPILOT_VERSION:
		{
			switch (_failureMode) {
				case FailNone:
					break;
				case FailInitialConnectRequestMessageAutopilotVersionFailure:
					return false;
				case FailInitialConnectRequestMessageAutopilotVersionLost:
					return true;
				default:
					break;
			}
			_respondWithAutopilotVersion();
			return true;
		}
	}
	return false;
}

void MockVehicle::_respondWithAutopilotVersion()
{
	uint32_t flightVersion = 0;
	if ( _struct.autopilot == MAV_AUTOPILOT_ARDUPILOTMEGA) {
		if (_struct.type == MAV_TYPE_FIXED_WING) {
			flightVersion |= 9 << (8*2);
		} else if (_struct.type == MAV_TYPE_SUBMARINE ) {
			flightVersion |= 5 << (8*2);
		} else if (_struct.type == MAV_TYPE_GROUND_ROVER ) {
			flightVersion |= 5 << (8*2);
		} else {
			flightVersion |= 6 << (8*2);
		}
		flightVersion |= 3 << (8*3);    // Major
		flightVersion |= 0 << (8*1);    // Patch
		flightVersion |= FIRMWARE_VERSION_TYPE_DEV << (8*0);
	}

	const uint8_t customVersion[8]{};
	const uint64_t capabilities = MAV_PROTOCOL_CAPABILITY_MAVLINK2 | MAV_PROTOCOL_CAPABILITY_MISSION_FENCE | MAV_PROTOCOL_CAPABILITY_MISSION_RALLY | MAV_PROTOCOL_CAPABILITY_MISSION_INT | ((_struct.autopilot == MAV_AUTOPILOT_ARDUPILOTMEGA) ? MAV_PROTOCOL_CAPABILITY_TERRAIN : 0);

	mavlink_message_t msg{};
	mavlink_msg_autopilot_version_pack_chan(
		_struct.vehicleId,
		_struct.componentId,
		ZSM_MAVLINK_COMM,
		&msg,
		capabilities,
		flightVersion,
		0,                                      			// middleware_sw_version,
		0,                                      			// os_sw_version,
		0,                                      			// board_version,
		reinterpret_cast<const uint8_t*>(&customVersion),   // flight_custom_version,
		reinterpret_cast<const uint8_t*>(&customVersion),   // middleware_custom_version,
		reinterpret_cast<const uint8_t*>(&customVersion),   // os_custom_version,
		_struct.boardVendorId,
		_struct.boardProductId,
		0,
		_struct.uid2);
	_sendMessage(msg);
}

void MockVehicle::_paramRequestListWorker()
{
	if (_currentParamRequestListComponentIndex == -1) {
		// Initial request complete
		return;
	}

	const int componentId = _parameterComponentIds.at(_currentParamRequestListComponentIndex);
	const auto valueIt = _mapParamName2Value.constFind(componentId);
	const auto typeIt = _mapParamName2MavParamType.constFind(componentId);
	Q_ASSERT(valueIt != _mapParamName2Value.cend() && typeIt != _mapParamName2MavParamType.cend());
	const auto& componentValues = valueIt.value();
	const auto& componentTypes = typeIt.value();
	const auto namesIt = _parameterNames.constFind(componentId);
	Q_ASSERT(namesIt != _parameterNames.cend());
	const auto& names = namesIt.value();
	const int cParameters = names.count();
	const QString& paramName = names.at(_currentParamRequestListParamIndex);



	if (((_failureMode == FailMissingParamOnInitialRequest)
		|| (_failureMode == FailMissingParamOnAllRequests))
		&& (paramName == _failParam))
	{
		zDebug(MockVehicleLog) << "Skipping param send:" << paramName;
	} else {

		// // for test missing Param
		// if ( !paramName.startsWith("NTF") ) {

			char paramId[MAVLINK_MSG_ID_PARAM_VALUE_LEN]{};

			mavlink_message_t responseMsg{};

			assert(componentValues.contains(paramName));
			assert(componentTypes.contains(paramName));

			const MAV_PARAM_TYPE paramType = componentTypes.value(paramName);

			assert(paramName.length() <= MAVLINK_MSG_ID_PARAM_VALUE_LEN);
			(void) strncpy(paramId, paramName.toLocal8Bit().constData(), MAVLINK_MSG_ID_PARAM_VALUE_LEN);

			zDebug(MockVehicleVerboseLog) << "sending msg_param_value" << componentId << paramId << paramType << componentValues.value(paramName);
			(void) mavlink_msg_param_value_pack_chan(
				_struct.vehicleId,
				_struct.componentId,                                   // component id
				ZSM_MAVLINK_COMM,
				&responseMsg,                                  // Outgoing message
				paramId,                                       // Parameter name
				_floatUnionForParam(componentId, paramName),   // Parameter value
				paramType,                                     // MAV_PARAM_TYPE
				cParameters,                                   // Total number of parameters
				_currentParamRequestListParamIndex             // Index of this parameter
			);
			_sendMessage(responseMsg);
		// }

	}

	// Move to next param index
	if (++_currentParamRequestListParamIndex >= cParameters) {
		// We've sent the last parameter for this component, move to next component
		if (++_currentParamRequestListComponentIndex >= _parameterComponentIds.count()) {
			// We've finished sending the last parameter for the last component, request is complete
			_currentParamRequestListComponentIndex = -1;
			// disconnect
			_500HzTaskDelete();

		} else {
			_currentParamRequestListParamIndex = 0;
		}
	}


}

float MockVehicle::_floatUnionForParam(int componentId, const QString &paramName)
{
	const auto valueIt = _mapParamName2Value.constFind(componentId);
    const auto typeIt = _mapParamName2MavParamType.constFind(componentId);
    Q_ASSERT(valueIt != _mapParamName2Value.cend() && typeIt != _mapParamName2MavParamType.cend());
    const auto& componentValues = valueIt.value();
    const auto& componentTypes = typeIt.value();
    Q_ASSERT(componentValues.contains(paramName) && componentTypes.contains(paramName));

    const MAV_PARAM_TYPE paramType = componentTypes.value(paramName);
    const QVariant paramVar = componentValues.value(paramName);

    mavlink_param_union_t valueUnion{};
    switch (paramType) {
    case MAV_PARAM_TYPE_REAL32:
        valueUnion.param_float = paramVar.toFloat();
        break;
    case MAV_PARAM_TYPE_UINT32:
        if (_struct.autopilot == MAV_AUTOPILOT_ARDUPILOTMEGA) {
            valueUnion.param_float = paramVar.toUInt();
        } else {
            valueUnion.param_uint32 = paramVar.toUInt();
        }
        break;
    case MAV_PARAM_TYPE_INT32:
        if (_struct.autopilot == MAV_AUTOPILOT_ARDUPILOTMEGA) {
            valueUnion.param_float = paramVar.toInt();
        } else {
            valueUnion.param_int32 = paramVar.toInt();
        }
        break;
    case MAV_PARAM_TYPE_UINT16:
        if (_struct.autopilot == MAV_AUTOPILOT_ARDUPILOTMEGA) {
            valueUnion.param_float = paramVar.toUInt();
        } else {
            valueUnion.param_uint16 = paramVar.toUInt();
        }
        break;
    case MAV_PARAM_TYPE_INT16:
        if (_struct.autopilot == MAV_AUTOPILOT_ARDUPILOTMEGA) {
            valueUnion.param_float = paramVar.toInt();
        } else {
            valueUnion.param_int16 = paramVar.toInt();
        }
        break;
    case MAV_PARAM_TYPE_UINT8:
        if (_struct.autopilot == MAV_AUTOPILOT_ARDUPILOTMEGA) {
            valueUnion.param_float = paramVar.toUInt();
        } else {
            valueUnion.param_uint8 = paramVar.toUInt();
        }
        break;
    case MAV_PARAM_TYPE_INT8:
        if (_struct.autopilot == MAV_AUTOPILOT_ARDUPILOTMEGA) {
            valueUnion.param_float = (unsigned char)paramVar.toChar().toLatin1();
        } else {
            valueUnion.param_int8 = (unsigned char)paramVar.toChar().toLatin1();
        }
        break;
    default:
        if (_struct.autopilot == MAV_AUTOPILOT_ARDUPILOTMEGA) {
            valueUnion.param_float = paramVar.toInt();
        } else {
            valueUnion.param_int32 = paramVar.toInt();
        }
        zCritical(MockVehicleLog()) << "Invalid parameter type" << paramType;
    }

    return valueUnion.param_float;
}


