#pragma once

#include <QVector3D>
#include <QGeoCoordinate>
#include <QQuaternion>
#include <vector>

namespace ShowTypes {
	Q_NAMESPACE

	///===========================================================
	/// Enum define

	/// 表示表演环境类型：室内或室外
	enum class EnvironmentType {
		Outdoor,	///< 室外环境
		Indoor		///< 室内环境
	};
	Q_ENUM_NS(EnvironmentType)

	/// 支持的摄像头类型
	enum class CameraType {
		Perspective	///< 透视摄像头
	};
	Q_ENUM_NS(CameraType)

	/// 表演阶段标识
	enum class ShowSegmentId {
		Takeoff,	///< 起飞阶段
		Show,		///< 表演阶段
		Landing		///< 降落阶段
	};
	Q_ENUM_NS(ShowSegmentId)

	/// 围栏违规时的应对动作
	enum class GeofenceAction {
		Report,		///< 报告违规
		Land,		///< 降落
		Return,		///< 返回
		SmartLand,	///< 智能降落
		SmartReturn,///< 智能返回
		Stop,		///< 停止飞行
		ShutDown	///< 关闭无人机
	};
	Q_ENUM_NS(GeofenceAction)

	///===========================================================
	/// Struct define

	/// 摄像头参数
	struct Camera_t {
		Q_GADGET
		Q_PROPERTY(ShowTypes::CameraType type MEMBER type)
		Q_PROPERTY(QString name MEMBER name)
		Q_PROPERTY(QVector3D position MEMBER position)
		Q_PROPERTY(QQuaternion orientation MEMBER orientation)
		Q_PROPERTY(int focalLength MEMBER focalLength)
		Q_PROPERTY(bool isDefault MEMBER isDefault)
	
	public:
		CameraType type{ CameraType::Perspective };			///< 摄像头类型，默认透视
		QString name;										///< 摄像头名称
		QVector3D position{ 0.0f, 0.0f, 0.0f };				///< 摄像头位置
		QQuaternion orientation{ 1.0f, 0.0f, 0.0f, 0.0f };	/// 摄像头朝向（四元数），默认面向 -Z 轴，右手坐标系，默认值 [1,0,0,0]
		int focalLength = 23;								///< 焦距，单位毫米，默认 23mm
		bool isDefault = false;								///< 是否为默认摄像头
	};
	/// 表演位置及朝向（真实的地理位置）
	struct Location_t {
		Q_GADGET
		Q_PROPERTY(QGeoCoordinate origin MEMBER origin)
		Q_PROPERTY(double orientation MEMBER orientation)

	public:
		QGeoCoordinate origin;		///< 表演原点的地理坐标
		double orientation = 0.0;	///< 表演X+轴与正北方向的夹角（度）
	};
	struct Environment_t {
		Q_GADGET
		Q_PROPERTY(ShowTypes::EnvironmentType type MEMBER type)
		//TODO 用到的时候再考虑如何解决
		//Q_PROPERTY(QList<ShowTypes::Camera_t> camera MEMBER camera)
		Q_PROPERTY(ShowTypes::Location_t* location READ location CONSTANT)

	public:
		Location_t* location() { return &_location; }

		EnvironmentType type;			///< 表演环境类型（室内/室外）
		QList<Camera_t> camera;			///< 相关摄像头列表
	private:
		Location_t _location;			///< 推荐表演位置及朝向
	};
	/// 表演阶段时间段
	struct ShowSegment_t {
		Q_GADGET
		Q_PROPERTY(double start MEMBER start)
		Q_PROPERTY(double end MEMBER end)
	
	public:
		double start;					///< 阶段开始时间（包含）
		double end;						///< 阶段结束时间（不包含）
	};
	/// 表演元数据
	struct ShowMetadata_t {
		Q_GADGET
		Q_PROPERTY(QString title MEMBER title)
		Q_PROPERTY(QString id MEMBER id)
		Q_PROPERTY(QStringList sources MEMBER sources)
		Q_PROPERTY(int timestampOffset MEMBER timestampOffset)
		//TODO 用到的时候再考虑如何解决
		//Q_PROPERTY(QMap<ShowTypes::ShowSegmentId, ShowTypes::ShowSegment> segments MEMBER segments)
	
	public:
		QString title;								///< 表演标题
		QString id;									///< 表演唯一ID
		QStringList sources;						///< 输入文件名及目录（.zds 文件夹路径）
		int timestampOffset;						///< 时间偏移量，用于轨迹时间对齐
		QMap<ShowSegmentId, ShowSegment_t> segments;	///< 轨迹分段信息
	};
	/// 表演提示点（用于分段标记，如起飞、图案开始、降落等）
	struct Cue_t {
		Q_GADGET
		Q_PROPERTY(QString name MEMBER name)
		Q_PROPERTY(double time MEMBER time)
	
	public:
		QString name;			///< 提示名称（用于图标或3D视图显示）
		double time;			///< 提示时间（秒）
	};
	/// 提示点列表
	struct CueSheet_t {
		Q_GADGET
		Q_PROPERTY(int version MEMBER version)
		//TODO 用到的时候再考虑如何解决
		//Q_PROPERTY(QList<ShowTypes::Cue_t> items MEMBER items)
	
	public:
		int version;			///< 提示格式版本，目前为1
		QList<Cue_t> items;		///< 提示点集合
	};
	/// 地理围栏多边形定义，分为“不可飞出”和“不可进入”
	struct GeofencePolygon_t {
		Q_GADGET
		Q_PROPERTY(bool isInclusion MEMBER isInclusion)
		//TODO 用到的时候再考虑如何解决
		//Q_PROPERTY(QList<QVector3D> points MEMBER points)
	
	public:
		bool isInclusion;				///< true表示“包含区域”（不可飞出），false表示“排除区域”（不可进入）
		QList<QVector3D> points;		///< 多边形顶点列表
	};
	/// 表演地理围栏定义
	struct Geofence_t {
		Q_GADGET
		Q_PROPERTY(int version MEMBER version)
		//TODO 用到的时候再考虑如何解决
		//Q_PROPERTY(QList<ShowTypes::GeofencePolygon_t> polygons MEMBER polygons)
		Q_PROPERTY(ShowTypes::GeofenceAction action MEMBER action)
	
	public:
		int version;										///< 地理围栏格式版本，目前为1
		QList<GeofencePolygon_t> polygons;					///< 多边形列表
		GeofenceAction action{ GeofenceAction::Report };	///< 违规动作
	};
	/// 飞行安全验证参数（由此飞行的轨迹生成出）
	struct ValidationSettings_t {
		Q_GADGET
		Q_PROPERTY(double maxAltitude MEMBER maxAltitude)
		Q_PROPERTY(double maxVelocityXY MEMBER maxVelocityXY)
		Q_PROPERTY(double maxVelocityZ MEMBER maxVelocityZ)
		Q_PROPERTY(double maxVelocityZUp MEMBER maxVelocityZUp)
		Q_PROPERTY(double minDistance MEMBER minDistance)

	public:
		double maxAltitude;			///< 最大飞行高度（米，AGL）
		double maxVelocityXY;		///< 最大水平速度（米/秒）
		double maxVelocityZ;		///< 最大垂直速度（米/秒）
		double maxVelocityZUp;		///< 最大上升速度（米/秒），优先于 maxVelocityZ
		double minDistance;			///< 无人机最小安全间距（米）
	};
	/// 无人机灯光表演通用设置
	struct ShowSettings_t {
		Q_GADGET
		Q_PROPERTY(ShowTypes::CueSheet_t* cues READ cues CONSTANT)
		Q_PROPERTY(ShowTypes::Geofence_t* geofence READ geofence CONSTANT)
		Q_PROPERTY(ShowTypes::ValidationSettings_t* validation READ validation CONSTANT)

	public:
		CueSheet_t* cues() { return &_cues; }
		Geofence_t* geofence() { return &_geofence; }
		ValidationSettings_t* validation() { return &_validation; }

	private:
		CueSheet_t _cues;				///< 提示点信息
		Geofence_t _geofence;			///< 推荐地理围栏
		ValidationSettings_t _validation;///< 安全验证参数
	};
}// namespace ShowTypes 

Q_DECLARE_METATYPE(ShowTypes::Camera_t)

Q_DECLARE_METATYPE(ShowTypes::Location_t)

Q_DECLARE_METATYPE(ShowTypes::Environment_t)

Q_DECLARE_METATYPE(ShowTypes::ShowSegment_t)

Q_DECLARE_METATYPE(ShowTypes::ShowMetadata_t)

Q_DECLARE_METATYPE(ShowTypes::Cue_t)

Q_DECLARE_METATYPE(ShowTypes::CueSheet_t)

Q_DECLARE_METATYPE(ShowTypes::GeofencePolygon_t)

Q_DECLARE_METATYPE(ShowTypes::Geofence_t)

Q_DECLARE_METATYPE(ShowTypes::ValidationSettings_t)

Q_DECLARE_METATYPE(ShowTypes::ShowSettings_t)