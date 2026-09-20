#include "OSMRequest.h"

#include <QPoint>
#include <QNetworkReply>
#include <QFileInfo>
#include <QDir>
#include <QStringLiteral>
#include <QString>
#include "OSMManager.h"

size_t qHash(OSMTileData data, size_t seed) noexcept
{
    return qHashMulti(seed, data.TileX, data.TileY, data.ZoomLevel);
}

qreal OSMTileData::distanceTo(QPoint p) const
{
    const qreal deltaX = p.x() - TileX;
    const qreal deltaY = p.y() - TileY;
    return qSqrt(deltaX * deltaX + deltaY * deltaY);
}

//%1 = zoom level(is dynamic), %2 = x tile number, %3 = y tile number
static const QString URL_OSMB_MAP = QStringLiteral("https://tile-a.openstreetmap.fr/hot/%1/%2/%3.png");

OSMRequest::OSMRequest(QObject* parent)
    : QObject{ parent }
{
    connect(&_queuesTimer, &QTimer::timeout, this, &OSMRequest::_sendMapsRequestTask);
    _queuesTimer.setInterval(0);
}


bool OSMRequest::isDemoToken() const
{
    return _token.isEmpty();
}

QString OSMRequest::token() const
{
    return _token;
}

void OSMRequest::setToken(const QString& token)
{
    if (token == _token) return;

    _token = token;
}


void OSMRequest::getMapsData(const QQueue<OSMTileData>& mapsQueue)
{
    if (mapsQueue.isEmpty()) return;
    
    _mapsQueue += mapsQueue;

    if (!_queuesTimer.isActive()) {
        _queuesTimer.start();
    }
}

void OSMRequest::_sendMapsRequestTask()
{
    if (_mapsQueue.isEmpty()) {
        _queuesTimer.stop();
    }
    else {
        //TODO Settings 
        static const int numConcurrentRequests = 1;
        
        if (!_mapsQueue.isEmpty() && _mapsNumberOfRequestsInFlight < numConcurrentRequests) {
            _getMapsDataRequest(_mapsQueue.dequeue());
            ++_mapsNumberOfRequestsInFlight;
        }

    }
}

static QString tileKey(const OSMTileData& tile)
{
    return QString::number(tile.ZoomLevel) + u'_' + QString::number(tile.TileX)
        + u'_' + QString::number(tile.TileY);
}

void OSMRequest::_getMapsDataRequest(const OSMTileData& tile)
{
    /// 看看之前是否请求后，找一下本地的缓存

    const QString fileName = QStringLiteral("data/") + tileKey(tile) + QStringLiteral(".png");
    QFileInfo file(fileName);
    

    if (file.size() > 0) {
        QFile file(fileName);
        if (file.open(QFile::ReadOnly)) {
            QByteArray data = file.readAll();
            file.close();
            qDebug() << "find store" << fileName;
            emit mapsDataReady(data, tile.TileX, tile.TileY, tile.ZoomLevel);
            --_mapsNumberOfRequestsInFlight;
            return;
        }
    }
    
    /// 本地没有就只有通过网络请求获取了
    qDebug() << "request" << fileName;
    QUrl url = QUrl(QString(URL_OSMB_MAP).arg(QString::number(tile.ZoomLevel),
        QString::number(tile.TileX),
        QString::number(tile.TileY)));

    QNetworkReply* reply = _networkAccessManager.get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [&,reply, fileName]() {
        qDebug() << "get reply" << fileName;

        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();

            // 保存到本地
            QDir dir;
            if (!dir.exists("data")) {
                dir.mkpath("data");
            }
            
            QFile file_save(fileName);
            if (file_save.open(QFile::WriteOnly)) {
                file_save.write(data);
                file_save.close();
                qDebug() << "Saved to " << fileName;
            }
            else {
                qWarning() << "failed save to" << fileName;
            }

            emit mapsDataReady(data, tile.TileX, tile.TileY, tile.ZoomLevel);
        }
        else {
            const QByteArray message = reply->readAll();
            static QByteArray lastMessage;
            if (message != lastMessage) {
                lastMessage = message;
                qWarning() << reply->error() << reply->url() << message;
            }
        }
        --_mapsNumberOfRequestsInFlight;
    });
}