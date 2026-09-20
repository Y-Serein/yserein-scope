#pragma once

#include "NumericDisplayControl.h"

#include <QtCore/QVector>
#include <QtCore/QHash>
#include <QtCore/QRectF>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtGui/QImage>
#include <QtGui/QColor>
#include <QtGui/QDragEnterEvent>
#include <QtGui/QDropEvent>
#include <QtGui/QMouseEvent>
#include <QtWidgets/QWidget>
#include <optional>

class QPainter;

namespace serein::scope::yserein_hmi {

// Page backdrop. Supported fill types mirror Penpot artboard backgrounds so a
// designer can pick a flat color, a linear/radial gradient, or a raster image.
struct PageBackground {
    QString type = QStringLiteral("solid");   // solid | linear | radial | image
    QColor color = QColor(QStringLiteral("#ffffff"));
    QColor color2 = QColor(QStringLiteral("#dbe4ee"));
    int angle = 0;                            // gradient angle, degrees
    QString assetId;                          // image asset for type == image
    QString imageMode = QStringLiteral("stretch");  // stretch|cover|contain|tile|center

    [[nodiscard]] QJsonObject toJson() const
    {
        return {
            {QStringLiteral("type"), type},
            {QStringLiteral("color"), color.name(QColor::HexArgb)},
            {QStringLiteral("color2"), color2.name(QColor::HexArgb)},
            {QStringLiteral("angle"), angle},
            {QStringLiteral("assetId"), assetId},
            {QStringLiteral("imageMode"), imageMode},
        };
    }

    static PageBackground fromJson(const QJsonObject &obj)
    {
        PageBackground bg;
        bg.type = obj.value(QStringLiteral("type")).toString(QStringLiteral("solid"));
        const QColor c1(obj.value(QStringLiteral("color")).toString());
        if (c1.isValid()) bg.color = c1;
        const QColor c2(obj.value(QStringLiteral("color2")).toString());
        if (c2.isValid()) bg.color2 = c2;
        bg.angle = obj.value(QStringLiteral("angle")).toInt(0);
        bg.assetId = obj.value(QStringLiteral("assetId")).toString();
        bg.imageMode = obj.value(QStringLiteral("imageMode")).toString(QStringLiteral("stretch"));
        return bg;
    }
};

class DesignerCanvas final : public QWidget {
    Q_OBJECT

public:
    explicit DesignerCanvas(QWidget *parent = nullptr);

    [[nodiscard]] QString selectedId() const;
    [[nodiscard]] QString selectedType() const;
    [[nodiscard]] QString selectedBinding() const;
    [[nodiscard]] QString selectedTextValue() const;
    [[nodiscard]] double selectedControlValue() const;
    [[nodiscard]] QRectF selectedGeometry() const;
    [[nodiscard]] QString selectedBindingMode() const;
    [[nodiscard]] QString selectedEventType() const;
    [[nodiscard]] QString selectedEventTarget() const;
    [[nodiscard]] bool selectedVisible() const;
    [[nodiscard]] bool selectedLocked() const;
    [[nodiscard]] QString selectedAssetId() const;
    [[nodiscard]] QString selectedScaleMode() const;
    [[nodiscard]] int selectedInputMaxLength() const;
    [[nodiscard]] double selectedChartMin() const;
    [[nodiscard]] double selectedChartMax() const;
    [[nodiscard]] int selectedChartWindowLength() const;
    [[nodiscard]] QJsonArray selectedChartSamples() const;
    [[nodiscard]] bool gridVisible() const;
    [[nodiscard]] bool previewMode() const;
    [[nodiscard]] int pageCount() const noexcept;
    [[nodiscard]] QStringList pageIds() const;
    [[nodiscard]] QStringList pageNames() const;
    [[nodiscard]] int currentPage() const noexcept;

    // Device surface (resolution & rotation) that drives the canvas model.
    [[nodiscard]] QSizeF deviceSize() const { return m_deviceSize; }
    [[nodiscard]] int rotation() const { return m_rotation; }
    [[nodiscard]] bool isPlacing() const { return m_placing; }
    [[nodiscard]] QString placingType() const { return m_placingType; }

    [[nodiscard]] QJsonObject pageState() const;
    bool restorePageState(const QJsonObject &state, QString *error = nullptr);

    // Assets are supplied by the host workspace (loaded from the project model).
    // The canvas only needs the decoded images so that picture controls can be
    // rendered with their real bits during authoring and preview.
    void setAssetImages(const QHash<QString, QImage> &images);

public slots:
    void setGridVisible(bool visible);
    void setPreviewMode(bool preview);
    void setPage(int pageIndex);
    bool addPage(const QString &name);
    bool duplicatePage(int pageIndex, const QString &name);
    bool renamePage(int pageIndex, const QString &name);
    bool removePage(int pageIndex);
    void addControl(const QString &type, const QString &label = {});
    void beginPlacing(const QString &type, const QString &label = {});
    void cancelPlacing();
    void setDeviceSize(const QSizeF &size);
    void setRotation(int degrees);
    [[nodiscard]] QJsonObject backgroundState() const;
    void setBackground(const QJsonObject &background);
    void alignSelected();
    void refreshSelection();
    void removeSelectedControl();
    bool setSelectedNumericId(const QString &id);
    bool setSelectedNumericBinding(const QString &binding);
    bool setSelectedNumericPreviewValue(double value);
    bool setSelectedTextValue(const QString &value);
    bool setSelectedProgressValue(double value);
    bool setSelectedControlPreviewValue(double value);
    bool setSelectedAssetId(const QString &assetId);
    bool setSelectedScaleMode(const QString &scaleMode);
    bool setSelectedInputMaxLength(int maxLength);
    bool setSelectedChartRange(double minValue, double maxValue);
    bool setSelectedChartWindowLength(int length);
    bool setSelectedChartSamples(const QJsonArray &samples);
    bool setPreviewTextInput(const QString &value);
    bool setSelectedGeometry(double x, double y, double width, double height);
    bool setSelectedBindingMode(const QString &mode);
    bool setSelectedEvent(const QString &eventType, const QString &eventTarget);
    bool setSelectedVisible(bool visible);
    bool setSelectedLocked(bool locked);
    void setSelectedNumericDecimals(int decimals);
    void setSelectedNumericUnit(const QString &unit);
    void setSelectedNumericFontPixelSize(int pixelSize);
    void setSelectedNumericAlignment(int alignmentIndex);
    bool setSelectedProperty(const QString &key, const QJsonValue &value);

    // Layer panel support: the current page's controls as a z-ordered list
    // (top-most first) plus id-based selection and z-order moves.
    [[nodiscard]] QJsonArray layerList() const;
    bool selectLayer(const QString &instanceId);
    bool moveLayer(const QString &instanceId, int offset);

signals:
    void selectionChanged(const QString &id, const QString &type,
                          const QString &binding);
    void numericPropertiesChanged(const QString &id, const QString &binding,
                                  double previewValue, int decimals, const QString &unit,
                                  int fontPixelSize, int alignmentIndex);
    void controlPropertiesChanged(const QString &id, const QString &type,
                                  const QString &binding, const QString &textValue,
                                  double value, const QString &bindingMode,
                                  const QString &eventType, const QString &eventTarget,
                                  const QRectF &geometry, bool visible, bool locked);
    void controlDetailsChanged(const QJsonObject &details);
    void controlStateChanged(const QString &type, const QJsonObject &properties,
                             const QRectF &geometry);
    void previewNavigationRequested(const QString &pageId);
    void textInputRequested(const QString &instanceId);
    void pageChanged(int pageIndex);
    void documentChanged();
    void statusMessage(const QString &message);
    void placingChanged();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    struct Control {
        QString instanceId;
        QString id;
        QString type;
        QString label;
        QString binding;
        QString bindingMode = QStringLiteral("read");
        QString eventType = QStringLiteral("none");
        QString eventTarget;
        QString textValue;
        double value = 0.0;
        QRectF rect;
        QColor fill;
        QColor text;
        bool interactive = false;
        bool visible = true;
        bool locked = false;
        std::optional<NumericDisplayControl> numeric = std::nullopt;
        QString assetId;
        QString scaleMode = QStringLiteral("fit");
        int inputMaxLength = 32;
        double chartMin = 0.0;
        double chartMax = 100.0;
        int chartWindowLength = 30;
        QVector<double> chartSamples;
        QJsonObject properties;  // full authorable property surface
    };

    [[nodiscard]] QRectF deviceRect() const;
    [[nodiscard]] QSizeF effectiveSize() const;
    [[nodiscard]] QPointF rotatePoint(const QPointF &point, double degrees) const;
    [[nodiscard]] QPointF toDevice(const QPointF &point) const;
    [[nodiscard]] QPointF toCanvas(const QPointF &point) const;
    [[nodiscard]] int hitTest(const QPointF &devicePoint) const;
    [[nodiscard]] int hitResizeHandle(const QPointF &widgetPoint) const;
    [[nodiscard]] QRectF resizeRectForHandle(const QRectF &startRect,
                                             const QPointF &startPoint,
                                             const QPointF &currentPoint,
                                             int handle) const;
    [[nodiscard]] static QVector<QPointF> handleCenters(const QRectF &rect);
    [[nodiscard]] QVector<Control> controlsForPage(int pageIndex) const;
    [[nodiscard]] NumericDisplayControl *selectedNumericControl();
    [[nodiscard]] const NumericDisplayControl *selectedNumericControl() const;
    bool ensureEditMode();
    void rebuildPage(int pageIndex);
    void emitSelection();
    void drawControl(QPainter &painter, const Control &control, bool selected) const;
    void drawImageControl(QPainter &painter, const Control &control,
                          const QRectF &rect) const;
    void drawDeviceChrome(QPainter &painter, const QRectF &bounds) const;
    void drawPageBackground(QPainter &painter, const QRectF &bounds) const;
    void drawSelectionOverlay(QPainter &painter, const Control &control) const;
    void drawPlacingGhost(QPainter &painter) const;
    [[nodiscard]] Control makeControl(const QString &type, const QString &label);
    void appendControl(Control control, const QPointF &point = QPointF(-1.0, -1.0));
    void placeControlAt(const QString &type, const QString &label, const QPointF &point);
    [[nodiscard]] QPointF findFreePlacement(const QSizeF &size) const;

    QVector<Control> m_controls;
    QVector<QVector<Control>> m_pageControls;
    QStringList m_pageIds;
    QStringList m_pageNames;
    int m_pageIndex = 0;
    int m_selectedIndex = -1;
    bool m_gridVisible = true;
    bool m_previewMode = false;
    QVector<QVector<Control>> m_previewSnapshot;
    bool m_dragging = false;
    bool m_resizing = false;
    int m_resizeHandle = -1;
    QRectF m_resizeStartRect;
    QPointF m_resizeStartPoint;
    bool m_placing = false;
    QString m_placingType;
    QString m_placingLabel;
    QPointF m_cursorPoint;
    bool m_hasCursor = false;
    int m_nextControlNumber = 1;
    QPointF m_pressDevicePoint;
    QRectF m_pressRect;
    QSizeF m_deviceSize;
    int m_rotation = 0;
    PageBackground m_background;
    QHash<QString, QImage> m_assetImages;
};

} // namespace serein::scope::yserein_hmi
