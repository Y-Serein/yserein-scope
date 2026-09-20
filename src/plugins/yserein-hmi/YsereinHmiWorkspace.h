#pragma once

#include <serein/scope/PluginInterface.h>
#include "HmiProjectModel.h"
#include "UndoController.h"

#include <QtWidgets/QWidget>
#include <QtCore/QHash>
#include <QtCore/QRectF>
#include <QtGui/QImage>

class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;
class QSpinBox;
class QToolButton;
class QPushButton;

namespace serein::scope::yserein_hmi {

class DesignerCanvas;
class SchemaInspector;

class YsereinHmiWorkspace final : public QWidget, public IDeviceWorkspace,
                                  public IWorkspaceUiProvider {
    Q_OBJECT
    Q_INTERFACES(serein::scope::IDeviceWorkspace
                 serein::scope::IWorkspaceUiProvider)

public:
    explicit YsereinHmiWorkspace(IHostServices *host, QWidget *parent = nullptr);
    ~YsereinHmiWorkspace() override = default;

    void shutdown() override;
    [[nodiscard]] WorkspaceUiProvider *workspaceUiProvider() override;

public slots:
    bool activateLibraryComponent(const QString &componentId);
    bool beginLibraryComponentPlacement(const QString &componentId);
    void setDeviceSize(int width, int height);
    void setRotation(int degrees);
    void newProject();
    void openProject();
    void saveProject();
    void saveProjectAs();
    void addPage();
    void duplicatePage();
    void renamePage();
    void removePage();
    void buildSimulatorTarget();
    void runSimulatorRuntime();
    void downloadToSimulator();
    void importImage();
    void removeSelectedAsset();
    void undo();
    void redo();

private slots:
    void selectPage(int index);
    void updateSelection(const QString &id, const QString &type, const QString &binding);
    void updateControlInspector(const QString &id, const QString &type,
                                const QString &binding, const QString &textValue,
                                double value, const QString &bindingMode,
                                const QString &eventType, const QString &eventTarget,
                                const QRectF &geometry, bool visible, bool locked);
    void updateNumericInspector(const QString &id, const QString &binding,
                                double previewValue, int decimals, const QString &unit,
                                int fontPixelSize, int alignmentIndex);
    void updateControlDetails(const QJsonObject &details);
    void updateSchemaInspector(const QString &type, const QJsonObject &properties);
    void handleTextInputRequested(const QString &instanceId);
    void togglePreview();
    void alignSelectedControl();
    void markDocumentDirty();
    void refreshLayerList();
    void removeSelectedLayer();

private:
    QWidget *buildContextPanel();
    QWidget *buildInspectorPanel();
    QWidget *buildCanvasPanel();
    QWidget *buildOutputPanel();
    void setStatus(const QString &message);
    void updateInspectorState();
    bool syncDocumentFromCanvas(bool recordUndo = true);
    bool loadDocument(const QString &path);
    bool confirmDiscardChanges(const QString &action);
    void refreshPageList();
    void publishPageContext();
    void updateProjectStatus();
    void refreshEventTargets();
    void selectPageById(const QString &pageId);
    void refreshAssetList();
    void refreshAssetCombo();
    void refreshBackgroundControls();
    void syncBackgroundToCanvas();
    void pickBackgroundColor(bool secondary);
    void updateBackgroundControlState();
    void updateLayerToggleState();
    void onCanvasDocumentChanged();
    bool restoreCanvasFromModel();
    void updateUndoRedoActions();
    [[nodiscard]] bool anyControlReferencesAsset(const QString &assetId) const;
    [[nodiscard]] QHash<QString, QImage> loadAssetImages() const;

    IHostServices *m_host = nullptr;
    WorkspaceUiProvider *m_uiProvider = nullptr;
    DesignerCanvas *m_canvas = nullptr;
    SchemaInspector *m_schemaInspector = nullptr;
    QWidget *m_contextPanel = nullptr;
    QWidget *m_inspectorPanel = nullptr;
    QWidget *m_outputPanel = nullptr;
    QListWidget *m_pages = nullptr;
    QListWidget *m_layerList = nullptr;
    QCheckBox *m_layerVisible = nullptr;
    QCheckBox *m_layerLocked = nullptr;
    QLabel *m_screenInfo = nullptr;
    QToolButton *m_preview = nullptr;
    QPushButton *m_undoButton = nullptr;
    QPushButton *m_redoButton = nullptr;
    QLineEdit *m_projectPath = nullptr;
    QPushButton *m_saveButton = nullptr;
    QLineEdit *m_selectedName = nullptr;
    QLineEdit *m_selectedType = nullptr;
    QLineEdit *m_selectedBinding = nullptr;
    QComboBox *m_bindingMode = nullptr;
    QComboBox *m_eventType = nullptr;
    QComboBox *m_eventTarget = nullptr;
    QComboBox *m_deviceSizeCombo = nullptr;
    QComboBox *m_rotationCombo = nullptr;
    QComboBox *m_bgTypeCombo = nullptr;
    QPushButton *m_bgColorButton = nullptr;
    QPushButton *m_bgColor2Button = nullptr;
    QSpinBox *m_bgAngleSpin = nullptr;
    QComboBox *m_bgAssetCombo = nullptr;
    QComboBox *m_bgImageModeCombo = nullptr;
    QColor m_bgColor = QColor(QStringLiteral("#ffffff"));
    QColor m_bgColor2 = QColor(QStringLiteral("#dbe4ee"));
    QDoubleSpinBox *m_geometryX = nullptr;
    QDoubleSpinBox *m_geometryY = nullptr;
    QDoubleSpinBox *m_geometryWidth = nullptr;
    QDoubleSpinBox *m_geometryHeight = nullptr;
    QCheckBox *m_visible = nullptr;
    QCheckBox *m_locked = nullptr;
    QLineEdit *m_textValue = nullptr;
    QLabel *m_textValueLabel = nullptr;
    QFrame *m_numericProperties = nullptr;
    QFrame *m_genericProperties = nullptr;
    QFrame *m_eventProperties = nullptr;
    QDoubleSpinBox *m_controlValue = nullptr;
    QLabel *m_controlValueLabel = nullptr;
    QDoubleSpinBox *m_previewValue = nullptr;
    QSpinBox *m_decimals = nullptr;
    QLineEdit *m_unit = nullptr;
    QSpinBox *m_fontPixelSize = nullptr;
    QComboBox *m_alignment = nullptr;
    QComboBox *m_assetSelector = nullptr;
    QListWidget *m_assetList = nullptr;
    QComboBox *m_scaleMode = nullptr;
    QSpinBox *m_inputMaxLength = nullptr;
    QDoubleSpinBox *m_chartMin = nullptr;
    QDoubleSpinBox *m_chartMax = nullptr;
    QSpinBox *m_chartWindowLength = nullptr;
    QLineEdit *m_chartSamples = nullptr;
    QLabel *m_selectionBadge = nullptr;
    QLabel *m_status = nullptr;
    QString m_lastBuildDirectory;
    HmiProjectDocument m_document = HmiProjectDocument::createDefault();
    UndoController m_undo{200};
    bool m_documentDirty = false;
    bool m_loadingDocument = false;
    bool m_suppressCanvasSync = false;
    bool m_updatingInspector = false;
    bool m_schemaInspectorEditing = false;
};

} // namespace serein::scope::yserein_hmi
