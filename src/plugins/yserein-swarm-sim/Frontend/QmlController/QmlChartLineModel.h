//
// Created by Hoppz on 25-12-10.
//
#pragma once

#include <QtCore/QAbstractListModel>
#include <QList>
#include <QVector4D>
#include <utility>


class QmlChartLineModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    QmlChartLineModel(QObject* parent = nullptr);
    ~QmlChartLineModel() override;

    void init(QList<std::pair<float, QVector4D>>&& list);
    void clear();
    int count() const {return _objectList.count();}

signals:
    void countChanged();
private:

    QVariant data(const QModelIndex &index, int role) const override;
    int rowCount(const QModelIndex &parent) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    QList<std::pair<float, QVector4D>> _objectList;

    static const int TimeRole = Qt::UserRole;
    static const int ValueRole = Qt::UserRole + 1;
};

