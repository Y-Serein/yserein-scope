#pragma once

#include "CoreExport.h"

#include <QtCore/QPoint>
#include <QtCore/QRect>
#include <QtCore/QRectF>
#include <QtCore/QSize>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtWidgets/QFrame>
#include <QtWidgets/QWidget>

#include <memory>

class QLabel;
class QAction;
class QMenu;
class QToolButton;
class QDragEnterEvent;
class QDropEvent;

namespace serein::scope {

class WorkspaceCanvasPrivate;
class ComponentSurfacePrivate;

// Host-styled component surface for ordinary layout-managed content. Use
// WorkspaceCanvas/ComponentFrame when the user must move or resize instances;
// use ComponentSurface when the page layout owns geometry but Core must still
// own the title, border, radius and content clipping contract.
class SEREIN_SCOPE_CORE_EXPORT ComponentSurface final : public QFrame {
    Q_OBJECT

public:
    explicit ComponentSurface(QString title = QString(), QWidget *parent = nullptr);
    ~ComponentSurface() override;

    [[nodiscard]] QWidget *contentHost() const noexcept;
    [[nodiscard]] QString title() const;
    void setTitle(const QString &title);

private:
    std::unique_ptr<ComponentSurfacePrivate> d;
};

class SEREIN_SCOPE_CORE_EXPORT ComponentFrame final : public QFrame {
    Q_OBJECT

public:
    [[nodiscard]] QString componentId() const;
    void setEditLocked(bool locked);
    [[nodiscard]] bool editLocked() const;
    void setSelected(bool selected);
    [[nodiscard]] bool isSelected() const;
    void setMaximizedState(bool maximized);

signals:
    void activated(ComponentFrame *frame);
    void geometryPreview(ComponentFrame *frame);
    void geometryCommitted(ComponentFrame *frame);
    void toggleMaximizeRequested(ComponentFrame *frame);
    void closeRequested(ComponentFrame *frame);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
private:
    friend class WorkspaceCanvas;

    explicit ComponentFrame(QString componentId, QString title, QWidget *content,
                            QWidget *parent = nullptr);
    ~ComponentFrame() override;

    enum ResizeEdge {
        NoEdge = 0x0,
        LeftEdge = 0x1,
        TopEdge = 0x2,
        RightEdge = 0x4,
        BottomEdge = 0x8,
    };
    Q_DECLARE_FLAGS(ResizeEdges, ResizeEdge)

    enum class Interaction {
        None,
        Move,
        Resize,
    };

    [[nodiscard]] ResizeEdges hitTestEdges(const QPoint &position) const;
    void beginInteraction(Interaction interaction, const QPoint &globalPosition,
                          ResizeEdges edges = {});
    void continueInteraction(const QPoint &globalPosition);
    void finishInteraction();
    void updateCursor(const QPoint &position);
    void updateChrome();

    QString m_componentId;
    QFrame *m_titleBar = nullptr;
    QLabel *m_title = nullptr;
    QToolButton *m_menu = nullptr;
    QAction *m_maximizeAction = nullptr;
    QAction *m_closeAction = nullptr;
    bool m_editLocked = true;
    bool m_selected = false;
    bool m_hovered = false;
    bool m_maximizedState = false;
    Interaction m_interaction = Interaction::None;
    ResizeEdges m_resizeEdges;
    QPoint m_pressGlobalPosition;
    QRect m_pressGeometry;
};

class SEREIN_SCOPE_CORE_EXPORT WorkspaceCanvas final : public QWidget {
    Q_OBJECT

public:
    explicit WorkspaceCanvas(QString settingsGroup, QWidget *parent = nullptr);
    ~WorkspaceCanvas() override;

    ComponentFrame *addComponent(const QString &componentId, const QString &title,
                                 QWidget *content, const QRectF &defaultGeometry,
                                 const QSize &minimumSize);
    [[nodiscard]] ComponentFrame *componentFrame(const QString &componentId) const;
    [[nodiscard]] QStringList componentIds() const;
    [[nodiscard]] bool hasStoredLayout() const;

    void setEditLocked(bool locked);
    [[nodiscard]] bool editLocked() const;
    Q_INVOKABLE void setComponentVisible(const QString &componentId, bool visible);
    Q_INVOKABLE bool addOrShowComponentAt(const QString &componentId,
                                          const QPoint &position = QPoint(-1, -1));
    [[nodiscard]] bool isComponentVisible(const QString &componentId) const;
    void toggleComponentMaximized(const QString &componentId);
    [[nodiscard]] bool isComponentMaximized(const QString &componentId) const;
    void resetLayout();

signals:
    void editLockedChanged(bool locked);
    void componentVisibilityChanged(const QString &componentId, bool visible);
    void componentDropRequested(const QString &componentId,
                                const QPoint &position);
    void focusModeChanged(bool focused);
    void layoutChanged();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    struct ComponentEntry;
    friend class WorkspaceCanvasPrivate;

    [[nodiscard]] QRect geometryFromNormalized(const QRectF &normalized) const;
    [[nodiscard]] QRectF normalizedFromGeometry(const QRect &geometry) const;
    void applyComponentGeometries();
    void applyZOrder();
    void activateFrame(ComponentFrame *frame);
    void previewFrameGeometry(ComponentFrame *frame);
    void commitFrameGeometry(ComponentFrame *frame);
    void clearSelection();
    void setSelectedFrame(ComponentFrame *frame);
    void closeFrame(ComponentFrame *frame);
    void enterMaximized(ComponentFrame *frame);
    void leaveMaximized();
    void saveLayout() const;
    void loadEntry(ComponentEntry &entry, const QString &componentId);
    void clampAndSnapFrame(ComponentFrame *frame);

    std::unique_ptr<WorkspaceCanvasPrivate> d;
};

} // namespace serein::scope
