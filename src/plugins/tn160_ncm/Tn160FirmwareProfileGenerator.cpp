#include "Tn160FirmwareProfileGenerator.h"

#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QHash>
#include <QtCore/QDir>
#include <QtCore/QObject>
#include <QtCore/QTextStream>

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>

namespace serein::scope::tn160ncm {
namespace {

constexpr int MinimumSamples = 20;
constexpr int MinimumTraceSeconds = 20;
constexpr int NtcStepAdu = 12;
constexpr int ShutterStepAdu = 32;
constexpr int CompensationLimitAdu = 600;
constexpr double MeanDiffTolerance = 0.5;
constexpr int AnchorMarginAdu = 32;
constexpr double Ridge = 1.0;

struct InputRow {
    double elapsedSeconds = 0.0;
    double centerAdu = 0.0;
    double ntcAdu = 0.0;
    double shutterAdu = 0.0;
    double meanDiff = 0.0;
    double anchorAdu = 0.0;
    double requiredCompAdu = 0.0;
};

struct SparseTerm {
    int index = 0;
    double value = 0.0;
};

double median(QVector<double> values)
{
    if (values.isEmpty()) return std::numeric_limits<double>::quiet_NaN();
    std::sort(values.begin(), values.end());
    const qsizetype middle = values.size() / 2;
    return values.size() % 2 == 0
        ? (values.at(middle - 1) + values.at(middle)) * 0.5
        : values.at(middle);
}

QStringList splitCsvLine(const QString &line)
{
    // Scope exports plain numeric CSV without quoted payload columns.
    return line.split(QLatin1Char(','), Qt::KeepEmptyParts);
}

int findColumn(const QHash<QString, int> &columns,
               const QStringList &candidates)
{
    for (const QString &candidate : candidates) {
        const auto it = columns.constFind(candidate);
        if (it != columns.cend()) return it.value();
    }
    return -1;
}

bool numberAt(const QStringList &cells, const int column, double *value)
{
    if (column < 0 || column >= cells.size()) return false;
    bool ok = false;
    const double parsed = cells.at(column).trimmed().toDouble(&ok);
    if (!ok || !std::isfinite(parsed)) return false;
    *value = parsed;
    return true;
}

QVector<quint16> makeNodes(const double minimum, const double maximum,
                           const int step)
{
    const int first = int(std::floor(minimum / step)) * step;
    const int last = int(std::ceil(maximum / step)) * step;
    QVector<quint16> nodes;
    for (int value = first; value <= last; value += step) {
        nodes.append(quint16(std::clamp(value, 0, 65535)));
    }
    return nodes;
}

void appendPwlTerms(QVector<SparseTerm> *terms,
                    const QVector<quint16> &nodes, const double value,
                    const int offset, const bool omitFirstNode)
{
    if (nodes.isEmpty()) return;
    auto append = [terms, offset, omitFirstNode](int node, double weight) {
        if (weight == 0.0 || (omitFirstNode && node == 0)) return;
        terms->append({offset + node - (omitFirstNode ? 1 : 0), weight});
    };
    if (value <= nodes.front()) {
        append(0, 1.0);
        return;
    }
    if (value >= nodes.back()) {
        append(nodes.size() - 1, 1.0);
        return;
    }
    int right = 1;
    while (right < nodes.size() && value > nodes.at(right)) ++right;
    const int left = right - 1;
    const double span = double(nodes.at(right) - nodes.at(left));
    const double rightWeight = (value - nodes.at(left)) / span;
    append(left, 1.0 - rightWeight);
    append(right, rightWeight);
}

bool solveNormalSystem(QVector<QVector<double>> matrix, QVector<double> rhs,
                       QVector<double> *solution)
{
    const int size = rhs.size();
    for (int column = 0; column < size; ++column) {
        int pivot = column;
        for (int row = column + 1; row < size; ++row) {
            if (std::abs(matrix.at(row).at(column))
                > std::abs(matrix.at(pivot).at(column))) {
                pivot = row;
            }
        }
        if (std::abs(matrix.at(pivot).at(column)) < 1e-10) return false;
        if (pivot != column) {
            std::swap(matrix[pivot], matrix[column]);
            std::swap(rhs[pivot], rhs[column]);
        }
        const double divisor = matrix.at(column).at(column);
        for (int index = column; index < size; ++index) {
            matrix[column][index] /= divisor;
        }
        rhs[column] /= divisor;
        for (int row = 0; row < size; ++row) {
            if (row == column) continue;
            const double factor = matrix.at(row).at(column);
            if (factor == 0.0) continue;
            for (int index = column; index < size; ++index) {
                matrix[row][index] -= factor * matrix[column][index];
            }
            rhs[row] -= factor * rhs[column];
        }
    }
    *solution = std::move(rhs);
    return true;
}

int interpolate(const QVector<FirmwareProfilePoint> &points, const int value)
{
    if (value <= points.front().stateAdu) return points.front().compensationAdu;
    if (value >= points.back().stateAdu) return points.back().compensationAdu;
    for (qsizetype index = 1; index < points.size(); ++index) {
        const FirmwareProfilePoint &right = points.at(index);
        if (value > right.stateAdu) continue;
        const FirmwareProfilePoint &left = points.at(index - 1);
        const int span = int(right.stateAdu) - int(left.stateAdu);
        const int delta = value - int(left.stateAdu);
        return int(left.compensationAdu)
            + (int(right.compensationAdu - left.compensationAdu) * delta
               + span / 2) / span;
    }
    return points.back().compensationAdu;
}

QString sanitizeIdentifier(QString value)
{
    value = value.toLower();
    QString result = QStringLiteral("tn160_profile_");
    for (const QChar character : value) {
        result += character.isLetterOrNumber() ? character
                                                : QLatin1Char('_');
    }
    while (result.endsWith(QLatin1Char('_'))) result.chop(1);
    return result;
}

template <typename T>
QString cArray(const QString &name, const QVector<T> &values)
{
    QString text;
    QTextStream stream(&text);
    stream << "static const " << (std::is_same_v<T, quint16>
                                      ? "uint16_t "
                                      : "int16_t ")
           << name << "[] = {\n    ";
    for (qsizetype index = 0; index < values.size(); ++index) {
        if (index != 0) stream << ',';
        if (index != 0 && index % 10 == 0) stream << "\n    ";
        stream << values.at(index);
    }
    stream << "\n};\n\n";
    return text;
}

QString makeCppSource(const FirmwareProfileDraft &draft)
{
    QVector<quint16> ntcStates;
    QVector<qint16> ntcCompensations;
    QVector<quint16> shutterStates;
    QVector<qint16> shutterCompensations;
    for (const FirmwareProfilePoint &point : draft.ntcPoints) {
        ntcStates.append(point.stateAdu);
        ntcCompensations.append(point.compensationAdu);
    }
    for (const FirmwareProfilePoint &point : draft.shutterPoints) {
        shutterStates.append(point.stateAdu);
        shutterCompensations.append(point.compensationAdu);
    }

    const QString prefix = draft.identifier;
    QString text;
    QTextStream stream(&text);
    stream << "/* Generated from " << QFileInfo(draft.sourcePath).fileName()
           << ". Fixed 50 C cold-start profile. */\n"
           << "/* predicted valid-sample (t >= 6 s) range: "
           << QString::number(draft.predictedMinimumC, 'f', 3) << ".."
           << QString::number(draft.predictedMaximumC, 'f', 3)
           << " C; max error "
           << QString::number(draft.predictedMaximumAbsoluteErrorC, 'f', 3)
           << " C. */\n\n";
    stream.flush();
    text += cArray(prefix + QStringLiteral("_ntc_x"), ntcStates);
    text += cArray(prefix + QStringLiteral("_ntc_comp"), ntcCompensations);
    text += cArray(prefix + QStringLiteral("_shutter_x"), shutterStates);
    text += cArray(prefix + QStringLiteral("_shutter_comp"),
                   shutterCompensations);
    QTextStream tail(&text, QIODevice::Append);
    tail << "static int32_t " << prefix
           << "_interp(const uint16_t *x, const int16_t *y, size_t count, "
              "uint16_t value)\n{\n"
           << "    if (value <= x[0]) return y[0];\n"
           << "    if (value >= x[count - 1]) return y[count - 1];\n"
           << "    for (size_t i = 1; i < count; ++i) {\n"
           << "        if (value <= x[i]) {\n"
           << "            const int32_t dx = (int32_t)x[i] - x[i - 1];\n"
           << "            const int32_t dv = (int32_t)value - x[i - 1];\n"
           << "            return y[i - 1] + ((int32_t)(y[i] - y[i - 1])"
              " * dv + dx / 2) / dx;\n"
           << "        }\n"
           << "    }\n"
           << "    return y[count - 1];\n"
           << "}\n\n"
           << "static int32_t " << prefix
           << "_comp(uint16_t ntc, uint16_t shutter_mean)\n{\n"
           << "    if (fabsf(ram_mean_diff - "
           << QString::number(draft.meanDiff, 'f', 3)
           << "f) > 0.5f || ram_temp_anchor < "
           << draft.anchorMinimum << " || ram_temp_anchor > "
           << draft.anchorMaximum
           << " || !ntc_adc_valid(ntc) || shutter_mean == 0) {\n"
           << "        return INT32_MIN;\n"
           << "    }\n"
           << "    const int32_t comp =\n"
           << "        " << prefix << "_interp(" << prefix
           << "_ntc_x, " << prefix << "_ntc_comp, sizeof(" << prefix
           << "_ntc_x) / sizeof(" << prefix << "_ntc_x[0]), ntc) +\n"
           << "        " << prefix << "_interp(" << prefix
           << "_shutter_x, " << prefix << "_shutter_comp, sizeof(" << prefix
           << "_shutter_x) / sizeof(" << prefix
           << "_shutter_x[0]), shutter_mean);\n"
           << "    return comp < -600 ? -600 : (comp > 600 ? 600 : comp);\n"
           << "}\n\n"
           << "/* Add " << prefix
           << "_comp(ntc, shutter_mean) as the next fallback branch in "
              "thermal_profile_comp(). */\n";
    tail.flush();
    return text;
}

} // namespace

FirmwareProfileDraft generateFirmwareProfileFromCsv(const QString &path)
{
    FirmwareProfileDraft draft;
    draft.sourcePath = path;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        draft.error = QObject::tr("无法读取 CSV：%1").arg(file.errorString());
        return draft;
    }

    QTextStream input(&file);
    QString headerLine = input.readLine();
    if (headerLine.startsWith(QChar::ByteOrderMark)) headerLine.removeFirst();
    const QStringList header = splitCsvLine(headerLine);
    QHash<QString, int> columns;
    for (qsizetype index = 0; index < header.size(); ++index) {
        columns.insert(header.at(index).trimmed(), int(index));
    }
    const int elapsedColumn = findColumn(columns, {QStringLiteral("t_s"),
                                                    QStringLiteral("elapsed_s")});
    const int centerColumn = findColumn(columns, {QStringLiteral("center_roi_adu")});
    const int ntcColumn = findColumn(columns, {QStringLiteral("ntc_adu")});
    const int shutterColumn =
        findColumn(columns, {QStringLiteral("shutter_mean_adu")});
    const int meanDiffColumn = findColumn(columns, {QStringLiteral("mean_diff")});
    const int anchorColumn = findColumn(columns, {QStringLiteral("anchor_adu"),
                                                   QStringLiteral("anchor")});
    if (elapsedColumn < 0 || centerColumn < 0 || ntcColumn < 0
        || shutterColumn < 0 || meanDiffColumn < 0 || anchorColumn < 0) {
        draft.error = QObject::tr(
            "CSV 缺少 t_s/elapsed_s、center_roi_adu、ntc_adu、"
            "shutter_mean_adu、mean_diff 或 anchor_adu 字段");
        return draft;
    }

    QVector<InputRow> rows;
    QVector<double> meanDiffs;
    double minimumNtc = std::numeric_limits<double>::infinity();
    double maximumNtc = -std::numeric_limits<double>::infinity();
    double minimumShutter = std::numeric_limits<double>::infinity();
    double maximumShutter = -std::numeric_limits<double>::infinity();
    qint32 minimumAnchor = std::numeric_limits<qint32>::max();
    qint32 maximumAnchor = std::numeric_limits<qint32>::min();
    double lastElapsed = 0.0;

    while (!input.atEnd()) {
        const QStringList cells = splitCsvLine(input.readLine());
        InputRow row;
        if (!numberAt(cells, elapsedColumn, &row.elapsedSeconds)
            || !numberAt(cells, centerColumn, &row.centerAdu)
            || !numberAt(cells, ntcColumn, &row.ntcAdu)
            || !numberAt(cells, shutterColumn, &row.shutterAdu)
            || !numberAt(cells, meanDiffColumn, &row.meanDiff)
            || !numberAt(cells, anchorColumn, &row.anchorAdu)) {
            continue;
        }
        if (row.elapsedSeconds < 6.0 || row.centerAdu <= 0.0
            || row.meanDiff <= 100.0 || row.ntcAdu <= 8.0
            || row.ntcAdu >= 4088.0 || row.shutterAdu <= 0.0
            || row.anchorAdu <= 0.0) {
            continue;
        }
        row.requiredCompAdu = row.centerAdu - row.anchorAdu - row.meanDiff;
        rows.append(row);
        meanDiffs.append(row.meanDiff);
        minimumNtc = std::min(minimumNtc, row.ntcAdu);
        maximumNtc = std::max(maximumNtc, row.ntcAdu);
        minimumShutter = std::min(minimumShutter, row.shutterAdu);
        maximumShutter = std::max(maximumShutter, row.shutterAdu);
        minimumAnchor = std::min(minimumAnchor, qint32(std::lround(row.anchorAdu)));
        maximumAnchor = std::max(maximumAnchor, qint32(std::lround(row.anchorAdu)));
        lastElapsed = std::max(lastElapsed, row.elapsedSeconds);
    }
    if (rows.size() < MinimumSamples || lastElapsed < MinimumTraceSeconds) {
        draft.error = QObject::tr("有效冷启动样本不足：需要至少 %1 个样本和 %2 秒记录")
                          .arg(MinimumSamples).arg(MinimumTraceSeconds);
        return draft;
    }

    draft.meanDiff = median(meanDiffs);
    for (const double value : meanDiffs) {
        if (std::abs(value - draft.meanDiff) > MeanDiffTolerance) {
            draft.error = QObject::tr("mean_diff 在 CSV 内不一致，拒绝生成参数");
            return draft;
        }
    }
    const QVector<quint16> ntcNodes = makeNodes(minimumNtc, maximumNtc,
                                                  NtcStepAdu);
    const QVector<quint16> shutterNodes = makeNodes(minimumShutter,
                                                      maximumShutter,
                                                      ShutterStepAdu);
    const int ntcCount = ntcNodes.size();
    const int dimension = ntcCount + shutterNodes.size() - 1;
    if (dimension <= 0) {
        draft.error = QObject::tr("无法建立补偿节点");
        return draft;
    }

    QVector<QVector<double>> normal(
        dimension, QVector<double>(dimension, 0.0));
    QVector<double> rhs(dimension, 0.0);
    for (const InputRow &row : rows) {
        QVector<SparseTerm> terms;
        appendPwlTerms(&terms, ntcNodes, row.ntcAdu, 0, false);
        appendPwlTerms(&terms, shutterNodes, row.shutterAdu, ntcCount, true);
        for (const SparseTerm &left : terms) {
            rhs[left.index] += left.value * row.requiredCompAdu;
            for (const SparseTerm &right : terms) {
                normal[left.index][right.index] += left.value * right.value;
            }
        }
    }
    for (int index = 0; index + 2 < ntcCount; ++index) {
        const int ids[] = {index, index + 1, index + 2};
        const double values[] = {1.0, -2.0, 1.0};
        for (int left = 0; left < 3; ++left) {
            for (int right = 0; right < 3; ++right) {
                normal[ids[left]][ids[right]] +=
                    Ridge * values[left] * values[right];
            }
        }
    }
    for (int index = ntcCount; index < dimension; ++index) {
        normal[index][index] += 0.01;
    }

    QVector<double> coefficients;
    if (!solveNormalSystem(std::move(normal), std::move(rhs), &coefficients)) {
        draft.error = QObject::tr("补偿曲线拟合矩阵不可解");
        return draft;
    }

    for (int index = 0; index < ntcCount; ++index) {
        draft.ntcPoints.append({
            ntcNodes.at(index),
            qint16(std::clamp(int(std::lround(coefficients.at(index))),
                               -CompensationLimitAdu, CompensationLimitAdu)),
        });
    }
    for (int index = 0; index < shutterNodes.size(); ++index) {
        const int coefficientIndex = ntcCount + index - 1;
        const int value = index == 0 ? 0
            : int(std::lround(coefficients.at(coefficientIndex)));
        draft.shutterPoints.append({
            shutterNodes.at(index),
            qint16(std::clamp(value, -CompensationLimitAdu,
                               CompensationLimitAdu)),
        });
    }

    double minimumTemperature = std::numeric_limits<double>::infinity();
    double maximumTemperature = -std::numeric_limits<double>::infinity();
    double maximumAbsoluteError = 0.0;
    for (const InputRow &row : rows) {
        const int compensation =
            interpolate(draft.ntcPoints, int(std::lround(row.ntcAdu)))
            + interpolate(draft.shutterPoints,
                          int(std::lround(row.shutterAdu)));
        const double fraction =
            (row.centerAdu - row.anchorAdu - compensation) / row.meanDiff;
        const double k4 = 273.15 * 273.15 * 273.15 * 273.15
            + fraction * (323.15 * 323.15 * 323.15 * 323.15
                          - 273.15 * 273.15 * 273.15 * 273.15);
        const double temperature = std::sqrt(std::sqrt(std::max(1.0, k4)))
            - 273.15;
        minimumTemperature = std::min(minimumTemperature, temperature);
        maximumTemperature = std::max(maximumTemperature, temperature);
        maximumAbsoluteError = std::max(maximumAbsoluteError,
                                        std::abs(temperature - 50.0));
    }

    draft.sampleCount = rows.size();
    draft.anchorMinimum = minimumAnchor - AnchorMarginAdu;
    draft.anchorMaximum = maximumAnchor + AnchorMarginAdu;
    draft.predictedMinimumC = minimumTemperature;
    draft.predictedMaximumC = maximumTemperature;
    draft.predictedPeakToPeakC = maximumTemperature - minimumTemperature;
    draft.predictedMaximumAbsoluteErrorC = maximumAbsoluteError;
    draft.identifier = sanitizeIdentifier(QFileInfo(path).baseName());
    draft.valid = true;
    draft.cppSource = makeCppSource(draft);
    return draft;
}

} // namespace serein::scope::tn160ncm
