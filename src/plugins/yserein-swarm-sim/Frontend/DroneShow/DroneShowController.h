#pragma once

#include <QtCore/QObject>
#include <qurl.h>

class DroneSpecification;
class ShowSpecification;
class DroneShowPlayerController;

class DroneShowController : public QObject
{
	Q_OBJECT

public:
	explicit DroneShowController(QObject* parent = nullptr);
	~DroneShowController();

	static DroneShowController* instance();
	static void registerQmlTypes();

	void init();
    void clearShow();

	static QString showZDSFileDir() { return _showZDSFileDir; }
	static QString showBinarydir()  { return _showBinaryDir; }
	static QString showJsonName() { return _showJsonName; }
	static QString cuesJsonName() { return _cuesJsonName; }

	/// getter 
	bool isLoadShow() const { return _loadShow; }
	ShowSpecification* showSpecification() const;

	/// setter

	/// invoke
	Q_INVOKABLE bool droneShowFileLoad(QUrl url);
	Q_INVOKABLE DroneSpecification* getDroneById(int id);

	/// function

signals:
	//TODO change to showChanged, maybe the show file has become other
	void isLoadShowChanged(bool load);

private:
	/// Parse Show File - Step 1 : Load show.json 
	void _readShowJsonFile();

private: 
	
	bool _loadShow = false;

	ShowSpecification* _show = nullptr;

	/// static define 
	static constexpr const char* _showZDSFileDir = "/temp/show/zds";
	static constexpr const char* _showBinaryDir = "/temp/show/binary";
	static constexpr const char* _showJsonName = "show.json";
	static constexpr const char* _cuesJsonName = "cues.json";
};

