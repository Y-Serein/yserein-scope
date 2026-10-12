#pragma once

#include <QVector3D>
#include <algorithm>
#include <cmath>

// Local simulator preview only. Coordinates are metres: x/y ground plane, z up.
struct BoxDancePlan {
    int aircraftCount = 0;
    int rows = 3;
    int cols = 4;
    double boxSpacing = 12.0;
    double layerSpacing = 2.0;
    double horizontalSpeed = 2.0;
    double verticalSpeed = 1.5;
    double travel = 50.0;

    int boxCount() const { return rows * cols; }
    int maxLayers() const { return (aircraftCount + boxCount() - 1) / boxCount(); }
    int minLayers() const { return aircraftCount / boxCount(); }
    int layersInBox(int box) const {
        return aircraftCount / boxCount() + (box < aircraftCount % boxCount() ? 1 : 0);
    }
    double waveSeconds() const { return layerSpacing / verticalSpeed; }
    double launchSeconds() const { return maxLayers() * waveSeconds(); }
    double travelSeconds() const { return travel / horizontalSpeed; }
    double durationSeconds() const { return 2.0 * launchSeconds() + 2.0 * travelSeconds(); }

    QVector3D position(int index, double seconds) const {
        const int box = index % boxCount();
        const int layer = index / boxCount();
        const double homeX = ((box % cols) - (cols - 1) * 0.5) * boxSpacing;
        const double homeY = ((box / cols) - (rows - 1) * 0.5) * boxSpacing;
        const double finalHeight = (layersInBox(box) - layer) * layerSpacing;
        const double launchEnd = launchSeconds();
        const double forwardEnd = launchEnd + travelSeconds();
        const double returnEnd = forwardEnd + travelSeconds();
        const double t = std::clamp(seconds, 0.0, durationSeconds());
        double x = homeX;
        double height = 0.0;
        if (t < launchEnd) {
            height = std::clamp((t - layer * waveSeconds()) * verticalSpeed, 0.0, finalHeight);
        } else if (t < returnEnd) {
            height = finalHeight;
            x += t < forwardEnd ? (t - launchEnd) * horizontalSpeed
                                : travel - (t - forwardEnd) * horizontalSpeed;
        } else {
            height = std::max(0.0, finalHeight - (t - returnEnd) * verticalSpeed);
        }
        return QVector3D(static_cast<float>(x), static_cast<float>(homeY),
                         static_cast<float>(height));
    }
};
