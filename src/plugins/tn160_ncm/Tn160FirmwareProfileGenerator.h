#pragma once

#include <QtCore/QString>
#include <QtCore/QVector>
#include <QtCore/QtGlobal>

namespace serein::scope::tn160ncm {

struct FirmwareProfilePoint {
    quint16 stateAdu = 0;
    qint16 compensationAdu = 0;
};

struct FirmwareProfileDraft {
    bool valid = false;
    QString error;
    QString sourcePath;
    QString identifier;
    qsizetype sampleCount = 0;
    double meanDiff = 0.0;
    qint32 anchorMinimum = 0;
    qint32 anchorMaximum = 0;
    QVector<FirmwareProfilePoint> ntcPoints;
    QVector<FirmwareProfilePoint> shutterPoints;
    double predictedMinimumC = 0.0;
    double predictedMaximumC = 0.0;
    double predictedPeakToPeakC = 0.0;
    double predictedMaximumAbsoluteErrorC = 0.0;
    QString cppSource;
};

[[nodiscard]] FirmwareProfileDraft generateFirmwareProfileFromCsv(
    const QString &path);

} // namespace serein::scope::tn160ncm
