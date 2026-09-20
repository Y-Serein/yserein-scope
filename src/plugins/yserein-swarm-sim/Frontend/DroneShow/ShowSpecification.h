#pragma once

#include "ShowTypes.h"
#include "DroneSpecification.h"

#include <QtCore/QObject>
#include <QJsonObject>

class ShowSpecification : public QObject
{
	Q_OBJECT

public:
	explicit ShowSpecification(QString& showJson, QObject* parent = nullptr);
	~ShowSpecification();

	/// @brief Parses the show.json file and initializes the show configuration.
	/// @param json The JSON object containing the show configuration data.
	void readFromJson(QJsonObject &json);
	
	/// @brief Loads and parses cues.json after show.json has been read.
	///
	/// The file path of cues.json needs to be determined inside readFromJson,
	/// then this function is called to load and parse cues.json.
	void readCuesFromFile();

	ShowTypes::Environment_t* environment() {return &_environment;}
	ShowTypes::ShowMetadata_t* showMetadata() {return &_showMetadata;}
	ShowTypes::ShowSettings_t* showSettings() {return &_showSettings;}
	QList<DroneSpecification*>* drones() {return &_drones;}
	float duration_s()  {return static_cast<float>(_showSettings.cues()->items.back().time);}
	qint64 duration_ms() {return _showSettings.cues()->items.back().time * 1000;}

	Q_INVOKABLE DroneSpecification* getDroneById(int id);

private:
	/// from json 
	int _version;
	ShowTypes::Environment_t _environment;
	ShowTypes::ShowMetadata_t _showMetadata;
	ShowTypes::ShowSettings_t _showSettings;
	QList<DroneSpecification*> _drones;

	/// define in qt 
	QString _showJsonPath;
	QString _cuesJsoPath;
};