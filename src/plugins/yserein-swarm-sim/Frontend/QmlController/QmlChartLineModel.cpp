//
// Created by Hoppz on 25-12-10.
//

#include "QmlChartLineModel.h"

const int QmlChartLineModel::TimeRole;
const int QmlChartLineModel::ValueRole;

QmlChartLineModel::QmlChartLineModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

QmlChartLineModel::~QmlChartLineModel()
{
}

void QmlChartLineModel::init(QList<std::pair<float, QVector4D>> &&list)
{
    if ( !_objectList.isEmpty() ) {
        beginRemoveRows(QModelIndex(), 0, _objectList.size()-1);
        endRemoveRows();
    }

    beginInsertRows(QModelIndex(), 0, list.size()-1);
    _objectList = std::move(list);
    endInsertRows();

    emit countChanged();
}

void QmlChartLineModel::clear()
{
    beginRemoveRows(QModelIndex(), 0, _objectList.size()-1);
    _objectList.clear();
    endRemoveRows();

    emit countChanged();
}

QVariant QmlChartLineModel::data(const QModelIndex &index, int role) const
{
    if ( !index.isValid() ) {
        return QVariant();
    }

    if (index.row() < 0 || index.row() >= _objectList.count()) {
        return QVariant();
    }

    if (role == TimeRole) {
        return QVariant::fromValue(_objectList[index.row()].first);
    } else if(role == ValueRole) {
        return QVariant::fromValue(_objectList[index.row()].second);
    } else {
        return QVariant();
    }
}

int QmlChartLineModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return _objectList.count();
}

QHash<int, QByteArray> QmlChartLineModel::roleNames() const
{
    QHash<int, QByteArray> hash;

    hash[TimeRole] = "time";
    hash[ValueRole] = "value";

    return hash;
}
