#pragma once

#include <QQuick3DTextureData>
#include <QtQmlIntegration>
#include <QVector3D>
#include <QObject>

#include "OSMRequest.h"

class OSMManager : public QObject
{
	Q_OBJECT
	QML_ELEMENT

	Q_PROPERTY(int tileSizeX READ tileSizeX CONSTANT FINAL)
	Q_PROPERTY(int tileSizeY READ tileSizeY CONSTANT FINAL)

public:
	explicit OSMManager(QObject* parent = nullptr);
	int tileSizeX() const;
	int tileSizeY() const;
	
	bool isDemoToken() const;
	void setToken(const QString& token) const;
	QString token() const;


public slots:
	void setCameraProperties(const QVector3D& position, const QVector3D& right,
		float cameraZoom, float minimumZoom, float maximumZoom,
		float cameraTilt, float minimumTilt, float maximumTilt);

signals:
	void mapsDataReady(class QByteArray mapData, int tileX, int tileY, int zoomLevel);


private slots:
	void _mapsDataReady(const QByteArray& mapData, int tileX, int tileY, int zoomLevel);

private:
	void _addMapRequestToQueue(QQueue<OSMTileData>& queue, int tileX, int tileY, int zoomLevel = 15);
	
	
private:
	OSMRequest* _request = nullptr;
	QHash<OSMTileData, bool> _mapsHash;

	const int _startTileX = 17605;		///< 开始位置
	const int _startTileY = 10746;		///< 开始位置
	const int _tileSizeX = 37;			///< 瓦片大小
	const int _tileSizeY = 37;			///< 瓦片大小
};

class CustomTextureData : public QQuick3DTextureData
{
	Q_OBJECT
		QML_ELEMENT
public slots:
	void setImageData(const QByteArray& data);
};