#pragma once

#include "NumericDisplayControl.h"

#include <QtCore/QVector>
#include <QtCore/QRectF>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QJsonObject>
#include <QtGui/QColor>
#include <QtGui/QDragEnterEvent>
#include <QtGui/QDropEvent>
#include <QtGui/QMouseEvent>
#include <QtWidgets/QWidget>
#include <optional>

class QPainter;

namespace serein::scope::yserein_hmi {

class DesignerCanvas final : public QWidget {
    Q_OBJECT

public:
    explicit DesignerCanvas(QWidget *parent = nullptr);

    [[nodiscard]] QString selectedId() const;
    [[nodiscard]] QString selectedType() const;
    [[nodiscard]] QString selectedBinding() const;
    [[nodiscard]] bool gridVisible() const;
    [[nodiscard]] bool previewMode() const;
    [[nodiscard]] int pageCount() const noexcept;
    [[nodiscard]] QStringList pageIds() const;
    [[nodiscard]] QStringList pageNames() const;
    [[nodiscard]] int currentPage() const noexcept;

    [[nodiscard]] QJsonObject pageState() const;
    bool restorePageState(const QJsonObject &state, QString *error = nullptr);

public slots:
    void setGridVisible(bool visible);
    void setPreviewMode(bool preview);
    void setPage(int pageIndex);
    bool addPage(const QString &name);
    bool duplicatePage(int pageIndex, const QString &name);
    bool renamePage(int pageIndex, const QString &name);
    bool removePage(int pageIndex);
    void addControl(const QString &type, const QString &label = {});
    void alignSelected();
    void refreshSelection();
    void removeSelectedControl();
    bool setSelectedNumericId(const QString &id);
    bool setSelectedNumericBinding(const QString &binding);
    bool setSelectedNumericPreviewValue(double value);
    void setSelectedNumericDecimals(int decimals);
    void setSelectedNumericUnit(const QString &unit);
    void setSelectedNumericFontPixelSize(int pixelSize);
    void setSelectedNumericAlignment(int alignmentIndex);

signals:
    void selectionChanged(const QString &id, const QString &type,
                          const QString &binding);
    void numericPropertiesChanged(const QString &id, const QString &binding,
                                  double previewValue, int decimals, const QString &unit,
                                  int fontPixelSize, int alignmentIndex);
    void pageChanged(int pageIndex);
    void documentChanged();
    void statusMessage(const QString &message);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    struct Control {
        QString instanceId;
        QString id;
        QString type;
        QString label;
        QString binding;
        QRectF rect;
        QColor fill;
        QColor text;
        bool interactive = false;
        std::optional<NumericDisplayControl> numeric = std::nullopt;
    };

    [[nodiscard]] QRectF deviceRect() const;
    [[nodiscard]] QPointF toDevice(const QPointF &point) const;
    [[nodiscard]] QPointF toCanvas(const QPointF &point) const;
    [[nodiscard]] int hitTest(const QPointF &devicePoint) const;
    [[nodiscard]] QVector<Control> controlsForPage(int pageIndex) const;
    [[nodiscard]] NumericDisplayControl *selectedNumericControl();
    [[nodiscard]] const NumericDisplayControl *selectedNumericControl() const;
    void rebuildPage(int pageIndex);
    void emitSelection();
    void drawControl(QPainter &painter, const Control &control, bool selected) const;
    void drawDeviceChrome(QPainter &painter, const QRectF &bounds) const;

    QVector<Control> m_controls;
    QVector<QVector<Control>> m_pageControls;
    QStringList m_pageIds;
    QStringList m_pageNames;
    int m_pageIndex = 0;
    int m_selectedIndex = -1;
    bool m_gridVisible = true;
    bool m_previewMode = false;
    bool m_dragging = false;
    int m_nextControlNumber = 1;
    QPointF m_pressDevicePoint;
    QRectF m_pressRect;
    bool m_running = false;
};

} // namespace serein::scope::yserein_hmi
