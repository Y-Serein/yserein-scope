#include "ShowSpecification.h"
#include "ZSMLoggingCategory.h"
#include "ZSMApplication.h"
#include "DroneShowController.h"
#include "ZSMJsonHelper.h"

#include <QRegularExpression>
#include <QJsonArray>

ZSM_LOGGING_CATEGORY(ShowSpecificationLog,"Frontend.DroneShow.ShowSpacification")

ShowSpecification::ShowSpecification(QString& showJson, QObject* parent)
	: QObject(parent)
	, _showJsonPath(showJson)
{
	zInfo(ShowSpecificationLog) << "create";
}

ShowSpecification::~ShowSpecification()
{
	zInfo(ShowSpecificationLog) << "destory";
}

void ShowSpecification::readFromJson(QJsonObject& json)
{
	_version = json["version"].toInt();
	// read validation
	QJsonObject settingsJson = json["settings"].toObject();
	QJsonObject validationJson = settingsJson["validation"].toObject();

	_showSettings.validation()->maxAltitude		= validationJson["maxAltitude"].toDouble();
	_showSettings.validation()->maxVelocityXY	= validationJson["maxVelocityXY"].toDouble();
	_showSettings.validation()->maxVelocityZ = validationJson["maxVelocityZ"].toDouble();
	_showSettings.validation()->minDistance = validationJson["minDistance"].toDouble();
	
	using namespace ShowTypes;

	// read environment 
	_environment.type = json["environment"].toObject()["type"].toString() == "outdoor"
		? EnvironmentType::Outdoor : EnvironmentType::Indoor;
	
	// read meta 
	QJsonObject metadataJson = json["meta"].toObject();
	QJsonObject segmengsJson = metadataJson["segments"].toObject();
	_showMetadata.title = metadataJson["title"].toString();
	QJsonArray takeoffArray = segmengsJson["takeoff"].toArray();
	QJsonArray showArray = segmengsJson["show"].toArray();
	QJsonArray landingArray = segmengsJson["landing"].toArray();
	
	if (takeoffArray.size() < 2 || showArray.size() < 2
		|| landingArray.size() < 2) {
		zWarning(ShowSpecificationLog) << 
			QString("meta segments < 2, takeoff: %1, show: %2, landing: %3")
			.arg(takeoffArray.size()).arg(showArray.size()).arg(landingArray.size());

		return;
	}

	_showMetadata.segments[ShowSegmentId::Takeoff] = ShowSegment_t{ takeoffArray[0].toDouble(), takeoffArray[1].toDouble() };
	_showMetadata.segments[ShowSegmentId::Show] = ShowSegment_t{ showArray[0].toDouble(), showArray[1].toDouble() };
	_showMetadata.segments[ShowSegmentId::Landing] = ShowSegment_t{ landingArray[0].toDouble(), landingArray[1].toDouble() };

	//TODO create GeoFence

	using namespace DroneTypes;

	// read Swarm
	QJsonArray swarmArray = json["swarm"].toObject()["drones"].toArray();
	for (int i = 0; i < swarmArray.size(); i++) {
		QJsonObject droneJson = swarmArray[i].toObject();

		auto te_drone = new DroneSpecification(this);
		QString droneType = droneJson["type"].toString();
		if (droneType == "generic") {
			te_drone->_type = DroneType::Generic;
		}

		QJsonObject droneSettingsJson = droneJson["settings"].toObject();
		QJsonArray droneHomeArray = droneSettingsJson["home"].toArray();
		QJsonArray droneLandAtArray = droneSettingsJson["landAt"].toArray();
		
		if (droneHomeArray.size() < 3 || droneLandAtArray.size() < 3) {
			zWarning(ShowSpecificationLog) << "Invalid home or landAt array size.";
			return;
		}

		te_drone->_settings.home = QVector3D(droneHomeArray[0].toDouble(), droneHomeArray[1].toDouble(), droneHomeArray[2].toDouble());
		te_drone->_settings.landAt = QVector3D(droneLandAtArray[0].toDouble(), droneLandAtArray[1].toDouble(), droneLandAtArray[2].toDouble());
		te_drone->_settings.name = droneSettingsJson["name"].toString();

		// 使用正则表达式提取 id
		static QRegularExpression re(R"((?:[Dd]rone[_ ]?)(\d+))");
		QRegularExpressionMatch match = re.match(te_drone->_settings.name);
		if ( match.hasMatch() ) {
			te_drone->_settings.id = match.captured(1).toInt();
		} else {
			te_drone->_settings.id = -1;
		}

		// 读取轨迹和灯光
		QString te_trajectoryPath = droneSettingsJson["trajectory"].toObject()["$ref"].toString();
		QString te_lightsPath = droneSettingsJson["lights"].toObject()["$ref"].toString();

		te_drone->_trajectoryPath = ZSMApplication::applicationDirPath() + 
			DroneShowController::showZDSFileDir() + te_trajectoryPath.mid(1, te_trajectoryPath.length() - 2 );
		te_drone->_lightsPath = ZSMApplication::applicationDirPath() + 
			DroneShowController::showZDSFileDir() + te_lightsPath.mid(1, te_lightsPath.length() - 2);

		//zDebug(ShowSpecificationLog) << "trajectory path :" << te_drone->_trajectoryPath;
		//zDebug(ShowSpecificationLog) << "lightsPath path :" << te_drone->_lightsPath;

		_drones.append(te_drone);

		// read drone json file
		te_drone->readFromJson();
	}

	// determine the path of cues.json
	_cuesJsoPath =  ZSMApplication::applicationDir() + DroneShowController::showZDSFileDir() + "/" + DroneShowController::cuesJsonName();
	readCuesFromFile();
}

void ShowSpecification::readCuesFromFile()
{
	QString errorStr;
	QJsonObject jsonObject = ZSMJsonHelper::openShowJsonFile(_cuesJsoPath, errorStr);

	if (!errorStr.isEmpty()) {
		zWarning(ShowSpecificationLog) << errorStr;
		return;
	}
	if (jsonObject.isEmpty()) {
		zWarning(ShowSpecificationLog) << "cues json file is empty!";
		return;
	}

	_showSettings.cues()->version = jsonObject["version"].toInt();
	QJsonArray itemsArray = jsonObject["items"].toArray();
	auto& cues_items = _showSettings.cues()->items;
	
	using namespace ShowTypes;

	for (int i = 0; i < itemsArray.size(); i++) {
		QJsonObject item = itemsArray[i].toObject();
		cues_items.append(Cue_t{ item["name"].toString(), item["time"].toDouble() });

		//qDebug(ShowSpecificationLog) << "name:" << cues_items[i].name << "time:" << cues_items[i].time;
	}
}

DroneSpecification* ShowSpecification::getDroneById(int id)
{
	id --;
	if ( id < 0 || id >= _drones.size() ) return nullptr;

	return _drones[id];
}
