#include "OSMManager.h"

#include <QImage>
#include <QRgb>
#include <algorithm>

OSMManager::OSMManager(QObject* parent)
	: QObject(parent)
	, _request(new OSMRequest(this))
{
	connect(_request, &OSMRequest::mapsDataReady, this, &OSMManager::_mapsDataReady);
}

int OSMManager::tileSizeX() const
{
	return _tileSizeX;
}

int OSMManager::tileSizeY() const
{
	return _tileSizeY;
}

bool OSMManager::isDemoToken() const
{
	return _request->isDemoToken();
}

void OSMManager::setToken(const QString& token) const
{
	return _request->setToken(token);
}

QString OSMManager::token() const
{
	return _request->token();
}

void OSMManager::setCameraProperties(const QVector3D& position, const QVector3D& right,
								float cameraZoom, float minimumZoom, float maximumZoom,
								float cameraTilt, float minimumTilt, float maximumTilt)
{
	float tiltFactor = (cameraTilt - minimumTilt) / qMax(maximumTilt - minimumTilt, 1.0);
	float zoomFactor = (cameraZoom - minimumZoom) / qMax(maximumZoom - minimumZoom, 1.0);

	// Forward vector align to the XY plane
	QVector3D forwardVector = QVector3D::crossProduct(right,
		QVector3D(0.0, 0.0, -1.0)).normalized();
	QVector3D projectionOfForwardOnXY = position
		+ forwardVector * tiltFactor * zoomFactor * 50.0;

	QQueue<OSMTileData> queue;
	for (int forwardIndex = -20; forwardIndex <= 20; ++forwardIndex) {
		for (int sidewardIndex = -20; sidewardIndex <= 20; ++sidewardIndex) {
			QVector3D transferredPosition = projectionOfForwardOnXY
				+ QVector3D(float(_tileSizeX * sidewardIndex), float(_tileSizeY * forwardIndex), 0.0);
			_addMapRequestToQueue(queue, _startTileX + int(transferredPosition.x() / _tileSizeX),
				_startTileY - int(transferredPosition.y() / _tileSizeY));
		}
	}

	const QPoint projectedTile{ _startTileX + int(projectionOfForwardOnXY.x() / _tileSizeX),
							   _startTileY - int(projectionOfForwardOnXY.y() / _tileSizeY) };

	auto closer = [projectedTile](const OSMTileData& v1, const OSMTileData& v2) -> bool {
		return v1.distanceTo(projectedTile) < v2.distanceTo(projectedTile);
		};
	std::sort(queue.begin(), queue.end(), closer);

	_request->getMapsData(queue);
}

void OSMManager::_mapsDataReady(const QByteArray& mapData, int tileX, int tileY, int zoomLevel)
{
	emit mapsDataReady(mapData, tileX - _startTileX, tileY - _startTileY, zoomLevel);
}

void OSMManager::_addMapRequestToQueue(QQueue<OSMTileData>& queue, int tileX, int tileY, int zoomLevel)
{
	OSMTileData data{ tileX, tileY, zoomLevel };
	if (!_mapsHash.contains(data))
		queue.append(data);
}

void CustomTextureData::setImageData(const QByteArray& data)
{
	QImage image = QImage::fromData(data).convertToFormat(QImage::Format_RGBA8888);
	setTextureData(QByteArray(reinterpret_cast<const char*>(image.constBits()), image.sizeInBytes()));
	setSize(image.size());
	setHasTransparency(false);
	setFormat(Format::RGBA8);
}
