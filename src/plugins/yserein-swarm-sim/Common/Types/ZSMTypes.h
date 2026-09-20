//
// Created by Hoppz on 25-6-29.
//
#pragma once

#include <QVector3D>

/// typedef 命名规则：
///
/// -   类型(容器)_名称_t
/// -   Qt 的类型带 QXXX
/// -   std 的类型不带 Q, 直接 XXX

/// the data transmitted between frontend and backend
typedef std::pair<int, QVector3D> Pair_IdVector3D_t;            ///<  传递无人机的局部位置坐标 (vehicleId, position)
