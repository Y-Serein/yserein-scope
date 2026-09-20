#pragma once

#include <QNetworkAccessManager>
#include <QQueue>
#include <QTimer>
#include <qcomparehelpers.h>

/// @brief	一块瓦片地图
struct OSMTileData
{
	int TileX;
	int TileY;
	int ZoomLevel;

	static int lng2TileX(double lon, int zoom) {
		return static_cast<int>((lon + 180.0) / 360.0 * (1 << zoom));
	}

	static int lat2TileY(double lat, int zoom) {
		return static_cast<int>((1.0 - std::log(std::tan(lat * M_PI / 180.0) + 1.0 / std::cos(lat * M_PI / 180.0)) / M_PI) / 2.0 * (1 << zoom));
	}

	qreal distanceTo(QPoint p) const;

	//? 友元函数
	friend bool comparesEqual(OSMTileData lhs, OSMTileData rhs) noexcept
	{
		return lhs.TileX == rhs.TileX && lhs.TileY == rhs.TileY && lhs.ZoomLevel == rhs.ZoomLevel;
	}

	//? 什么意思
	Q_DECLARE_EQUALITY_COMPARABLE(OSMTileData)
};

size_t qHash(OSMTileData data, size_t seed = 0) noexcept;

class OSMRequest : public QObject
{
	Q_OBJECT
public:
	explicit OSMRequest(QObject* parent = nullptr);
	bool isDemoToken() const;

	QString token() const;
	void setToken(const QString& token);

public slots:
	void getMapsData(const QQueue<OSMTileData>& mapsQueue);

signals:
	void mapsDataReady(const QByteArray& mapData, int tileX, int tileY, int zoomLevel);

private slots:
	void _sendMapsRequestTask();
	void _getMapsDataRequest(const OSMTileData& tile);

private:
	int _mapsNumberOfRequestsInFlight = 0;	///< 已申请的请求
	QTimer _queuesTimer;
	QQueue<OSMTileData> _mapsQueue;
	QNetworkAccessManager _networkAccessManager;
	QString _token;
};