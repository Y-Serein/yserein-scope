#pragma once

#include <serein/scope/PluginInterface.h>
#include "HmiProjectModel.h"

#include <QtWidgets/QWidget>

class QComboBox;
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
    void newProject();
    void openProject();
    void saveProject();
    void saveProjectAs();
    void addPage();
    void duplicatePage();
    void renamePage();
    void removePage();

private slots:
    void selectPage(int index);
    void updateSelection(const QString &id, const QString &type, const QString &binding);
    void updateNumericInspector(const QString &id, const QString &binding,
                                double previewValue, int decimals, const QString &unit,
                                int fontPixelSize, int alignmentIndex);
    void togglePreview();
    void alignSelectedControl();
    void markDocumentDirty();

private:
    QWidget *buildContextPanel();
    QWidget *buildInspectorPanel();
    QWidget *buildCanvasPanel();
    QWidget *buildOutputPanel();
    void setStatus(const QString &message);
    void updateInspectorState();
    void syncDocumentFromCanvas();
    bool loadDocument(const QString &path);
    bool confirmDiscardChanges(const QString &action);
    void refreshPageList();
    void publishPageContext();
    void updateProjectStatus();

    IHostServices *m_host = nullptr;
    WorkspaceUiProvider *m_uiProvider = nullptr;
    DesignerCanvas *m_canvas = nullptr;
    QWidget *m_contextPanel = nullptr;
    QWidget *m_inspectorPanel = nullptr;
    QWidget *m_outputPanel = nullptr;
    QListWidget *m_pages = nullptr;
    QLabel *m_screenInfo = nullptr;
    QToolButton *m_preview = nullptr;
    QLineEdit *m_projectPath = nullptr;
    QPushButton *m_saveButton = nullptr;
    QLineEdit *m_selectedName = nullptr;
    QLineEdit *m_selectedType = nullptr;
    QLineEdit *m_selectedBinding = nullptr;
    QFrame *m_numericProperties = nullptr;
    QDoubleSpinBox *m_previewValue = nullptr;
    QSpinBox *m_decimals = nullptr;
    QLineEdit *m_unit = nullptr;
    QSpinBox *m_fontPixelSize = nullptr;
    QComboBox *m_alignment = nullptr;
    QLabel *m_selectionBadge = nullptr;
    QLabel *m_status = nullptr;
    HmiProjectDocument m_document = HmiProjectDocument::createDefault();
    bool m_documentDirty = false;
    bool m_loadingDocument = false;
    bool m_updatingInspector = false;
};

} // namespace serein::scope::yserein_hmi
