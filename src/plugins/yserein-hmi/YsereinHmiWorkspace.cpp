#include "YsereinHmiWorkspace.h"

#include "ControlSchema.h"
#include "ChangeBuilder.h"
#include "DesignerCanvas.h"
#include "HmiToolchain.h"
#include "SchemaInspector.h"

#include <serein/scope/WorkspaceCanvas.h>

#include <QtCore/QSignalBlocker>
#include <QtCore/QSize>
#include <QtCore/QHash>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QFileInfo>
#include <QtCore/QDir>
#include <QtCore/QRegularExpression>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QInputDialog>
#include <QtWidgets/QMessageBox>
#include <QtGui/QShortcut>
#include <QtGui/QKeySequence>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QColorDialog>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QStyle>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>

#include <utility>
#include <algorithm>
#include <type_traits>

namespace serein::scope::yserein_hmi {
namespace {

QLabel *makeSectionTitle(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("hmiSectionTitle"));
    return label;
}

QPushButton *makeToolButton(const QString &text, QWidget *parent, const QString &kind = {})
{
    auto *button = new QPushButton(text, parent);
    button->setObjectName(kind == QStringLiteral("primary")
                              ? QStringLiteral("hmiPrimaryButton")
                              : QStringLiteral("hmiToolButton"));
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

// Canvas-level type tokens keep the legacy hyphen spelling for the input
// control (text-input); the schema registry keys are camelCase (textInput).
QString schemaKeywordForType(const QString &type)
{
    if (type == QStringLiteral("text-input")) return QStringLiteral("textInput");
    return type;
}

// Human-readable label for a canvas control type; used by the layer list
// and the component library. Falls back to the schema registry so the new
// library entries do not drift from the declarative schema.
QString controlLabelForType(const QString &type)
{
    if (const ControlSchema *schema =
            ControlSchemaRegistry::instance().findByKeyword(schemaKeywordForType(type))) {
        return schema->displayName;
    }
    return type;
}

// Short human label for an undo/redo command, derived from the first operation
// in the change. Only used by the undo/redo toolbar button tooltips for now.
QString changeLabel(const Change &change)
{
    if (change.ops.isEmpty()) return QStringLiteral("编辑");
    const ChangeOp &first = change.ops.constFirst();
    return std::visit([](const auto &o) -> QString {
        using T = std::decay_t<decltype(o)>;
        if constexpr (std::is_same_v<T, ControlAdds>) return QStringLiteral("添加控件");
        else if constexpr (std::is_same_v<T, ControlRemoves>) return QStringLiteral("删除控件");
        else if constexpr (std::is_same_v<T, ControlUpdates>) return QStringLiteral("修改属性");
        else if constexpr (std::is_same_v<T, ControlMoves>) return QStringLiteral("调整图层");
        else if constexpr (std::is_same_v<T, PageAdds>) return QStringLiteral("新建页面");
        else if constexpr (std::is_same_v<T, PageRemoves>) return QStringLiteral("删除页面");
        else if constexpr (std::is_same_v<T, PageRenames>) return QStringLiteral("重命名页面");
        else return QStringLiteral("编辑");
    }, first);
}

void applyColorSwatch(QPushButton *button, const QColor &color)
{
    if (!button) return;
    const QColor fg = color.lightness() < 128 ? QColor(Qt::white) : QColor(Qt::black);
    button->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: %1; color: %2; border: 1px solid #94a3b8; "
        "border-radius: 3px; padding: 2px 6px; }")
        .arg(color.name(QColor::HexRgb), fg.name()));
}

} // namespace

YsereinHmiWorkspace::YsereinHmiWorkspace(IHostServices *host, QWidget *parent)
    : QWidget(parent), m_host(host)
{
    setObjectName(QStringLiteral("hmiDesignerWorkspace"));
    setProperty("shellRole", QStringLiteral("workspace-content"));

    setStyleSheet(QString::fromUtf8(R"QSS(
QWidget#hmiContextPanel, QFrame#hmiInspectorPanel, QFrame#hmiOutputPanel {
    background: transparent; border: 0;
}
QLabel#hmiSubtle { color: #7a8699; font-size: 11px; }
QLabel#hmiSectionTitle { color: #344054; font-size: 12px; font-weight: 700; }
QLabel#hmiSelectionBadge { color: #2f6feb; background: #edf3ff; border: 1px solid #c8d9ff; border-radius: 9px; padding: 2px 8px; font-size: 10px; }
QPushButton#hmiToolButton, QPushButton#hmiPrimaryButton { min-height: 28px; padding: 0 10px; border-radius: 6px; font-size: 11px; }
QPushButton#hmiToolButton { background: #ffffff; border: 1px solid #d6dde5; color: #526171; }
QPushButton#hmiToolButton:hover { background: #f4f7fb; border-color: #b9c8da; color: #1d2939; }
QPushButton#hmiPrimaryButton { background: #2f6feb; border: 1px solid #2f6feb; color: #ffffff; font-weight: 700; }
QPushButton#hmiPrimaryButton:hover { background: #245bc4; border-color: #245bc4; }
QToolButton#hmiPreviewButton { min-height: 28px; padding: 0 12px; border: 1px solid #b8cbed; border-radius: 6px; color: #2f6feb; background: #edf3ff; font-weight: 700; }
QToolButton#hmiPreviewButton[preview="true"] { color: #ffffff; background: #2f6feb; border-color: #2f6feb; }
QComboBox#hmiPropertyField, QLineEdit#hmiPropertyField,
QSpinBox#hmiPropertyField, QDoubleSpinBox#hmiPropertyField {
    min-height: 28px; border: 1px solid #d6dde5; border-radius: 6px;
    background: #ffffff; padding: 0 8px; color: #344054;
}
QLineEdit#hmiPropertyField:focus { border-color: #8caee7; }
QListWidget#hmiList { border: 0; background: transparent; outline: 0; }
QListWidget#hmiList::item { min-height: 30px; padding: 0 8px; border-radius: 6px; color: #526171; }
QListWidget#hmiList::item:hover { background: #f2f5f9; color: #1d2939; }
QListWidget#hmiList::item:selected { background: #edf3ff; color: #245bc4; font-weight: 700; }
QFrame#hmiFieldGroup { background: #fafbfc; border: 1px solid #e7ebef; border-radius: 8px; }
QLabel#hmiPropertyLabel { color: #7a8699; font-size: 11px; }
)QSS"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 6, 0, 0);
    root->setSpacing(0);
    root->addWidget(buildCanvasPanel(), 1);

    m_contextPanel = buildContextPanel();
    m_inspectorPanel = buildInspectorPanel();
    m_outputPanel = buildOutputPanel();
    // Provider panels are temporarily reparented into Scope's host docks.
    // Keep the HMI rules on each contribution root so styling does not depend
    // on the original workspace parent after the host mounts them.
    const QString panelStyle = styleSheet();
    m_contextPanel->setStyleSheet(panelStyle);
    m_inspectorPanel->setStyleSheet(panelStyle);
    m_outputPanel->setStyleSheet(panelStyle);
    updateInspectorState();
    refreshAssetList();
    refreshAssetCombo();
    refreshLayerList();
    connect(m_canvas, &DesignerCanvas::documentChanged,
            this, &YsereinHmiWorkspace::markDocumentDirty);
    connect(m_canvas, &DesignerCanvas::documentChanged,
            this, &YsereinHmiWorkspace::onCanvasDocumentChanged);

    // 撤销/重做快捷键：Ctrl+Z / Ctrl+Y（或平台 Redo 序列）。
    auto *undoShortcut = new QShortcut(QKeySequence::Undo, this);
    undoShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(undoShortcut, &QShortcut::activated, this, &YsereinHmiWorkspace::undo);
    auto *redoShortcut = new QShortcut(QKeySequence::Redo, this);
    redoShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(redoShortcut, &QShortcut::activated, this, &YsereinHmiWorkspace::redo);
    updateUndoRedoActions();

    m_uiProvider = new WorkspaceUiProvider(this);
    m_uiProvider->setPanelSides(WorkspacePanelSide::Left,
                                WorkspacePanelSide::Right);
    WorkspaceUiContext uiContext;
    uiContext.id = QStringLiteral("yserein-hmi/designer/page/%1")
                       .arg(m_canvas->pageIds().value(m_canvas->currentPage()));
    uiContext.layoutProfile = WorkspaceLayoutProfile::Authoring;
    uiContext.contextPanel = {m_contextPanel, tr("工程与页面"), nullptr, {}};
    uiContext.inspectorPanel = {m_inspectorPanel, tr("属性与绑定"), nullptr, {}};
    uiContext.outputPanel = {m_outputPanel, tr("HMI 状态"), nullptr, {}};
    uiContext.componentLibrary.items = {
        {QStringLiteral("number"), tr("数值显示"),
         QStringLiteral(":/serein/channels.svg")},
        {QStringLiteral("text"), tr("文本"),
         QStringLiteral(":/serein/channels.svg")},
        {QStringLiteral("button"), tr("按钮"),
         QStringLiteral(":/serein/channels.svg")},
        {QStringLiteral("progress"), tr("进度条"),
         QStringLiteral(":/serein/channels.svg")},
        {QStringLiteral("status"), tr("状态灯"),
         QStringLiteral(":/serein/channels.svg")},
        {QStringLiteral("switch"), tr("开关"),
         QStringLiteral(":/serein/channels.svg")},
        {QStringLiteral("image"), tr("图片"),
         QStringLiteral(":/serein/channels.svg")},
        {QStringLiteral("chart"), tr("曲线"),
         QStringLiteral(":/serein/channels.svg")},
        {QStringLiteral("text-input"), tr("文本输入"),
         QStringLiteral(":/serein/channels.svg")},
        {QStringLiteral("gauge"), tr("圆盘仪表"),
         QStringLiteral(":/serein/channels.svg")},
        {QStringLiteral("sevenSegment"), tr("数码管"),
         QStringLiteral(":/serein/channels.svg")},
        {QStringLiteral("slider"), tr("滑动条"),
         QStringLiteral(":/serein/channels.svg")},
        {QStringLiteral("knob"), tr("旋钮"),
         QStringLiteral(":/serein/channels.svg")},
        {QStringLiteral("alarm"), tr("报警指示"),
         QStringLiteral(":/serein/channels.svg")},
        {QStringLiteral("alarmList"), tr("报警列表"),
         QStringLiteral(":/serein/channels.svg")},
        {QStringLiteral("trend"), tr("历史趋势"),
         QStringLiteral(":/serein/channels.svg")},
        {QStringLiteral("table"), tr("数据表格"),
         QStringLiteral(":/serein/channels.svg")},
        {QStringLiteral("timeDisplay"), tr("时间显示"),
         QStringLiteral(":/serein/channels.svg")},
        {QStringLiteral("dateDisplay"), tr("日期显示"),
         QStringLiteral(":/serein/channels.svg")},
        {QStringLiteral("multiLamp"), tr("多态指示灯"),
         QStringLiteral(":/serein/channels.svg")},
    };
    uiContext.componentLibrary.activationTarget = this;
    uiContext.componentLibrary.activationMethod =
        QByteArrayLiteral("activateLibraryComponent");
    WorkspaceUiPresentationOptions uiOptions;
    m_uiProvider->setContext(std::move(uiContext), uiOptions);
    updateProjectStatus();

    if (m_host) m_host->setStatus(tr("Y-Serein HMI · 离线作者工作区"));
}

void YsereinHmiWorkspace::shutdown()
{
    if (m_host) m_host->log(QStringLiteral("info"), tr("Y-Serein HMI 离线作者工作区已关闭"));
    m_host = nullptr;
}

WorkspaceUiProvider *YsereinHmiWorkspace::workspaceUiProvider()
{
    return m_uiProvider;
}

bool YsereinHmiWorkspace::activateLibraryComponent(const QString &componentId)
{
    if (!m_canvas) return false;
    const ControlSchema *schema =
        ControlSchemaRegistry::instance().findByKeyword(schemaKeywordForType(componentId));
    if (!schema || schema->keyword == QStringLiteral("opaque")) return false;
    m_canvas->cancelPlacing();
    m_canvas->addControl(componentId, controlLabelForType(componentId));
    return true;
}

bool YsereinHmiWorkspace::beginLibraryComponentPlacement(const QString &componentId)
{
    if (!m_canvas) return false;
    const ControlSchema *schema =
        ControlSchemaRegistry::instance().findByKeyword(schemaKeywordForType(componentId));
    if (!schema || schema->keyword == QStringLiteral("opaque")) return false;
    m_canvas->beginPlacing(componentId, controlLabelForType(componentId));
    return true;
}

void YsereinHmiWorkspace::setDeviceSize(int width, int height)
{
    if (!m_canvas || width < 1 || height < 1) return;
    m_canvas->setDeviceSize(QSizeF(width, height));
    syncDocumentFromCanvas(false);
}

void YsereinHmiWorkspace::setRotation(int degrees)
{
    if (!m_canvas) return;
    m_canvas->setRotation(degrees);
    syncDocumentFromCanvas(false);
}

void YsereinHmiWorkspace::newProject()
{
    if (!confirmDiscardChanges(tr("新建工程"))) return;
    m_loadingDocument = true;
    m_document = HmiProjectDocument::createDefault();
    m_lastBuildDirectory.clear();
    refreshAssetList();
    refreshAssetCombo();
    if (m_canvas) m_canvas->setAssetImages(QHash<QString, QImage>{});
    const QJsonObject state{
        {QStringLiteral("currentPage"), 0},
        {QStringLiteral("pages"), QJsonArray{
            QJsonObject{{QStringLiteral("pageId"), QStringLiteral("page_1")},
                        {QStringLiteral("name"), tr("首页")},
                        {QStringLiteral("controls"), QJsonArray{}}},
        }},
    };
    QString error;
    const bool restored = m_canvas && m_canvas->restorePageState(state, &error);
    m_loadingDocument = false;
    if (!restored) {
        setStatus(tr("新建工程失败：%1").arg(error));
        return;
    }
    // 让模型与画布默认状态对齐（页面名等），但不形成撤销命令。
    syncDocumentFromCanvas(false);
    refreshPageList();
    m_documentDirty = true;
    m_undo.clear();
    updateUndoRedoActions();
    updateProjectStatus();
    setStatus(tr("已新建空白工程；尚未保存"));
}

void YsereinHmiWorkspace::openProject()
{
    if (!confirmDiscardChanges(tr("打开工程"))) return;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("打开 Y-Serein HMI 工程"), QString(),
        tr("Y-Serein HMI 工程 (*.yshm);;JSON 文件 (*.json);;所有文件 (*)"));
    if (path.isEmpty()) return;
    loadDocument(path);
}

void YsereinHmiWorkspace::saveProject()
{
    if (m_document.filePath().isEmpty()) {
        saveProjectAs();
        return;
    }
    if (!syncDocumentFromCanvas()) return;
    QString error;
    if (!m_document.saveToFileAtomic(&error)) {
        setStatus(tr("保存失败：%1").arg(error));
        return;
    }
    m_documentDirty = false;
    updateProjectStatus();
    setStatus(tr("工程已保存：%1").arg(QFileInfo(m_document.filePath()).fileName()));
}

void YsereinHmiWorkspace::saveProjectAs()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("保存 Y-Serein HMI 工程"), QStringLiteral("untitled.yshm"),
        tr("Y-Serein HMI 工程 (*.yshm);;JSON 文件 (*.json)"));
    if (path.isEmpty()) return;
    if (!syncDocumentFromCanvas()) return;
    QString error;
    if (!m_document.saveToFileAtomic(path, &error)) {
        setStatus(tr("保存失败：%1").arg(error));
        return;
    }
    m_documentDirty = false;
    updateProjectStatus();
    setStatus(tr("工程已保存：%1").arg(QFileInfo(path).fileName()));
}

void YsereinHmiWorkspace::addPage()
{
    bool accepted = false;
    const QString name = QInputDialog::getText(this, tr("新建页面"), tr("页面名称"),
                                               QLineEdit::Normal,
                                               tr("页面 %1").arg(m_canvas->pageCount() + 1),
                                               &accepted);
    if (accepted && m_canvas->addPage(name)) refreshPageList();
}

void YsereinHmiWorkspace::duplicatePage()
{
    bool accepted = false;
    const QString name = QInputDialog::getText(this, tr("复制页面"), tr("页面名称"),
                                               QLineEdit::Normal,
                                               tr("页面 %1").arg(m_canvas->pageCount() + 1),
                                               &accepted);
    if (accepted && m_canvas->duplicatePage(m_canvas->currentPage(), name)) {
        refreshPageList();
    }
}

void YsereinHmiWorkspace::renamePage()
{
    const int index = m_canvas ? m_canvas->currentPage() : -1;
    if (index < 0) return;
    bool accepted = false;
    const QString name = QInputDialog::getText(this, tr("重命名页面"), tr("页面名称"),
                                               QLineEdit::Normal,
                                               m_canvas->pageNames().value(index),
                                               &accepted);
    if (accepted && m_canvas->renamePage(index, name)) refreshPageList();
}

void YsereinHmiWorkspace::removePage()
{
    if (!m_canvas || m_canvas->pageCount() <= 1) {
        setStatus(tr("工程至少需要保留一个页面"));
        return;
    }
    const int index = m_canvas->currentPage();
    const QString name = m_canvas->pageNames().value(index);
    if (QMessageBox::question(this, tr("删除页面"),
                              tr("确定删除页面“%1”？").arg(name))
        != QMessageBox::Yes) return;
    if (m_canvas->removePage(index)) refreshPageList();
}

bool YsereinHmiWorkspace::syncDocumentFromCanvas(bool recordUndo)
{
    if (!m_canvas) {
        setStatus(tr("保存失败：设计画布不可用"));
        return false;
    }
    const QJsonObject canvasState = m_canvas->pageState();
    const QJsonArray serializedPages = canvasState.value(QStringLiteral("pages")).toArray();

    QJsonObject root = m_document.toJsonObject();
    root.insert(QStringLiteral("pages"), serializedPages);
    root.insert(QStringLiteral("currentPage"),
                canvasState.value(QStringLiteral("currentPage")));

    QString error;
    HmiProjectModel after;
    if (!after.setJsonObject(root, &error)) {
        setStatus(tr("工程模型校验失败：%1").arg(error));
        return false;
    }

    auto change = ChangeBuilder::buildModelChange(m_document.model(), after, &error);
    if (!change) {
        setStatus(tr("构建变更失败：%1").arg(error));
        return false;
    }
    if (!change->empty()) {
        if (!m_document.model().applyChange(*change, &error)) {
            setStatus(tr("应用变更失败：%1").arg(error));
            return false;
        }
        if (recordUndo) {
            m_undo.push(*change, changeLabel(*change));
        }
    }

    // 当前页属于扩展字段（不参与撤销/重做），单独回写以保持与旧整包替换一致。
    QJsonObject extensions = m_document.model().extensions();
    extensions.insert(QStringLiteral("currentPage"),
                      canvasState.value(QStringLiteral("currentPage")).toInt(0));
    extensions.insert(QStringLiteral("device"),
                      canvasState.value(QStringLiteral("device")).toObject());
    extensions.insert(QStringLiteral("background"),
                      canvasState.value(QStringLiteral("background")).toObject());
    m_document.model().setExtensions(extensions);
    return true;
}

void YsereinHmiWorkspace::onCanvasDocumentChanged()
{
    // 新建/打开工程时由 restore 灌入状态，不形成编辑命令。
    if (m_loadingDocument || m_suppressCanvasSync) return;
    if (syncDocumentFromCanvas()) {
        updateUndoRedoActions();
    }
}

bool YsereinHmiWorkspace::restoreCanvasFromModel()
{
    if (!m_canvas) return false;
    QJsonArray pages;
    for (const HmiProjectModel::Page &page : m_document.model().pages())
        pages.append(page.toJsonObject());
    QJsonObject state{
        {QStringLiteral("currentPage"),
         m_document.toJsonObject().value(QStringLiteral("currentPage")).toInt(0)},
        {QStringLiteral("pages"), pages},
    };
    const QJsonObject device =
        m_document.model().extensions().value(QStringLiteral("device")).toObject();
    if (!device.isEmpty()) state.insert(QStringLiteral("device"), device);
    const QJsonObject background =
        m_document.model().extensions().value(QStringLiteral("background")).toObject();
    if (!background.isEmpty()) state.insert(QStringLiteral("background"), background);
    QString error;
    if (!m_canvas->restorePageState(state, &error)) {
        setStatus(tr("恢复画布失败：%1").arg(error));
        return false;
    }
    m_canvas->setAssetImages(loadAssetImages());
    refreshBackgroundControls();
    return true;
}

void YsereinHmiWorkspace::undo()
{
    if (!m_undo.canUndo()) return;
    Change change = m_undo.undo();
    m_suppressCanvasSync = true;
    QString error;
    if (!m_document.model().applyChange(change, &error)) {
        setStatus(tr("撤销失败：%1").arg(error));
        m_suppressCanvasSync = false;
        return;
    }
    restoreCanvasFromModel();
    m_suppressCanvasSync = false;
    refreshPageList();
    refreshLayerList();
    updateInspectorState();
    markDocumentDirty();
    updateUndoRedoActions();
}

void YsereinHmiWorkspace::redo()
{
    if (!m_undo.canRedo()) return;
    Change change = m_undo.redo();
    m_suppressCanvasSync = true;
    QString error;
    if (!m_document.model().applyChange(change, &error)) {
        setStatus(tr("重做失败：%1").arg(error));
        m_suppressCanvasSync = false;
        return;
    }
    restoreCanvasFromModel();
    m_suppressCanvasSync = false;
    refreshPageList();
    refreshLayerList();
    updateInspectorState();
    markDocumentDirty();
    updateUndoRedoActions();
}

void YsereinHmiWorkspace::updateUndoRedoActions()
{
    if (m_undoButton) {
        m_undoButton->setEnabled(m_undo.canUndo());
        const QString label = m_undo.undoText();
        m_undoButton->setToolTip(label.isEmpty() ? tr("撤销") : tr("撤销：%1").arg(label));
    }
    if (m_redoButton) {
        m_redoButton->setEnabled(m_undo.canRedo());
        const QString label = m_undo.redoText();
        m_redoButton->setToolTip(label.isEmpty() ? tr("重做") : tr("重做：%1").arg(label));
    }
}

bool YsereinHmiWorkspace::loadDocument(const QString &path)
{
    HmiProjectDocument loaded;
    QString error;
    if (!loaded.loadFromFile(path, &error)) {
        setStatus(tr("打开工程失败：%1").arg(error));
        return false;
    }
    QJsonArray pages;
    for (const HmiProjectModel::Page &page : loaded.model().pages()) {
        pages.append(page.toJsonObject());
    }
    QJsonObject state{
        {QStringLiteral("currentPage"),
         loaded.toJsonObject().value(QStringLiteral("currentPage")).toInt(0)},
        {QStringLiteral("pages"), pages},
    };
    const QJsonObject background =
        loaded.model().extensions().value(QStringLiteral("background")).toObject();
    if (!background.isEmpty()) state.insert(QStringLiteral("background"), background);
    m_loadingDocument = true;
    const bool restored = m_canvas && m_canvas->restorePageState(state, &error);
    m_loadingDocument = false;
    if (!restored) {
        setStatus(tr("打开工程失败：%1").arg(error));
        return false;
    }
    m_document = std::move(loaded);
    m_lastBuildDirectory.clear();
    m_documentDirty = false;
    m_undo.clear();
    updateUndoRedoActions();
    refreshAssetList();
    refreshAssetCombo();
    m_canvas->setAssetImages(loadAssetImages());
    refreshBackgroundControls();
    updateInspectorState();
    refreshPageList();
    updateProjectStatus();
    setStatus(tr("工程已打开：%1").arg(QFileInfo(path).fileName()));
    return true;
}

bool YsereinHmiWorkspace::confirmDiscardChanges(const QString &action)
{
    if (!m_documentDirty) return true;
    const QMessageBox::StandardButton choice = QMessageBox::warning(
        this, tr("未保存的工程"),
        tr("当前工程有未保存修改，是否在%1前保存？").arg(action),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (choice == QMessageBox::Cancel) return false;
    if (choice == QMessageBox::Save) {
        saveProject();
        return !m_documentDirty;
    }
    return true;
}

void YsereinHmiWorkspace::buildSimulatorTarget()
{
    if (!syncDocumentFromCanvas()) return;
    const QString parent = QFileDialog::getExistingDirectory(
        this, tr("选择本机目标包输出目录"));
    if (parent.isEmpty()) return;
    QString output = QDir(parent).filePath(
        QStringLiteral("%1-simulator").arg(m_document.model().projectId()));
    int suffix = 2;
    while (QFileInfo::exists(output)) {
        output = QDir(parent).filePath(QStringLiteral("%1-simulator-%2")
            .arg(m_document.model().projectId()).arg(suffix++));
    }
    HmiToolchain::BuildResult result;
    QString error;
    if (!HmiToolchain::buildSimulatorTarget(m_document, output, &result, &error)) {
        setStatus(tr("目标包生成失败：%1").arg(error));
        return;
    }
    m_lastBuildDirectory = result.outputDirectory;
    setStatus(tr("本机目标包已生成：%1 · %2 个资源 · %3")
        .arg(result.outputDirectory).arg(result.resourceCount, 0, 10)
        .arg(result.contentHash.left(12)));
}

void YsereinHmiWorkspace::runSimulatorRuntime()
{
    if (m_lastBuildDirectory.isEmpty()) {
        setStatus(tr("请先生成本机目标包"));
        return;
    }
    QJsonObject report;
    QString error;
    if (!HmiToolchain::exerciseRuntime(m_lastBuildDirectory, &report, &error)) {
        setStatus(tr("Runtime 自测失败：%1").arg(error));
        return;
    }
    setStatus(tr("Runtime 自测通过：%1 页 / %2 控件 / %3 次协议回环")
        .arg(report.value(QStringLiteral("pages")).toInt())
        .arg(report.value(QStringLiteral("controls")).toInt())
        .arg(report.value(QStringLiteral("frameRoundTrips")).toInt()));
}

void YsereinHmiWorkspace::downloadToSimulator()
{
    if (m_lastBuildDirectory.isEmpty()) {
        setStatus(tr("请先生成并验证本机目标包"));
        return;
    }
    const QString deviceRoot = QFileDialog::getExistingDirectory(
        this, tr("选择本机模拟设备目录"));
    if (deviceRoot.isEmpty()) return;
    QString installed;
    QString error;
    if (!HmiToolchain::installToSimulatorDevice(m_lastBuildDirectory, deviceRoot,
                                                 &installed, &error)) {
        setStatus(tr("模拟下载失败：%1").arg(error));
        return;
    }
    setStatus(tr("模拟下载并校验完成：%1").arg(installed));
}

void YsereinHmiWorkspace::importImage()
{
    const QString filePath = QFileDialog::getOpenFileName(
        this, tr("导入图片资源"), QString(),
        tr("图片文件 (*.png *.jpg *.jpeg *.bmp *.gif *.tif *.tiff *.webp);;所有文件 (*)"));
    if (filePath.isEmpty()) return;

    QImage image;
    if (!image.load(filePath)) {
        setStatus(tr("导入图片失败：文件不是可解码的图片。"));
        return;
    }

    const QFileInfo info(filePath);
    QString base = info.completeBaseName().toLower();
    base.replace(QRegularExpression(QStringLiteral("[^a-z0-9_]")), QStringLiteral("_"));
    if (base.isEmpty()) base = QStringLiteral("image");
    if (base.front().isDigit()) base = QStringLiteral("image_") + base;
    QString assetId = QStringLiteral("asset.image.") + base;
    int counter = 2;
    while (m_document.model().hasAsset(assetId)) {
        assetId = QStringLiteral("asset.image.%1_%2").arg(base).arg(counter++);
    }

    QString source = filePath;
    const QString projectDir = m_document.filePath().isEmpty()
        ? QString() : QFileInfo(m_document.filePath()).absolutePath();
    if (!projectDir.isEmpty()) {
        const QString relative = QDir(projectDir).relativeFilePath(filePath);
        if (!relative.startsWith(QStringLiteral(".."))) source = relative;
    }

    HmiProjectModel::Asset asset;
    asset.assetId = assetId;
    asset.kind = QStringLiteral("image");
    asset.source = source;
    m_document.model().addAsset(std::move(asset));

    refreshAssetList();
    refreshAssetCombo();
    m_canvas->setAssetImages(loadAssetImages());
    markDocumentDirty();
    setStatus(tr("已导入图片资源：%1").arg(assetId));
}

void YsereinHmiWorkspace::removeSelectedAsset()
{
    if (!m_assetList || m_assetList->currentRow() < 0) {
        setStatus(tr("请先选择要移除的资源"));
        return;
    }
    const auto *item = m_assetList->item(m_assetList->currentRow());
    const QString assetId = item ? item->data(Qt::UserRole).toString() : QString{};
    if (assetId.isEmpty()) return;
    if (anyControlReferencesAsset(assetId)) {
        setStatus(tr("资源“%1”仍被控件引用，无法移除").arg(assetId));
        return;
    }
    if (m_document.model().removeAsset(assetId)) {
        refreshAssetList();
        refreshAssetCombo();
        m_canvas->setAssetImages(loadAssetImages());
        markDocumentDirty();
        setStatus(tr("已移除资源：%1").arg(assetId));
    }
}

bool YsereinHmiWorkspace::anyControlReferencesAsset(const QString &assetId) const
{
    if (!m_canvas) return false;
    const QString target = assetId.trimmed();
    for (const QJsonValue &pageValue : m_canvas->pageState()
             .value(QStringLiteral("pages")).toArray()) {
        for (const QJsonValue &controlValue : pageValue.toObject()
                 .value(QStringLiteral("controls")).toArray()) {
            if (controlValue.toObject().value(QStringLiteral("properties"))
                    .toObject().value(QStringLiteral("assetId")).toString().trimmed()
                == target) {
                return true;
            }
        }
    }
    return false;
}

void YsereinHmiWorkspace::refreshAssetList()
{
    if (!m_assetList) return;
    const QSignalBlocker blocker(m_assetList);
    m_assetList->clear();
    for (const HmiProjectModel::Asset &asset : m_document.model().assets()) {
        auto *item = new QListWidgetItem(
            tr("%1  ·  %2").arg(asset.assetId, asset.kind), m_assetList);
        item->setData(Qt::UserRole, asset.assetId);
        item->setToolTip(asset.source);
    }
}

void YsereinHmiWorkspace::refreshAssetCombo()
{
    if (!m_assetSelector) return;
    const QString current = m_assetSelector->currentData().toString();
    {
        const QSignalBlocker blocker(m_assetSelector);
        m_assetSelector->clear();
        m_assetSelector->addItem(tr("请选择图片资源"), QString{});
        for (const HmiProjectModel::Asset &asset : m_document.model().assets()) {
            if (asset.kind.trimmed() != QStringLiteral("image")) continue;
            m_assetSelector->addItem(asset.assetId, asset.assetId);
        }
        const int index = m_assetSelector->findData(current);
        m_assetSelector->setCurrentIndex(index >= 0 ? index : 0);
    }
    if (m_bgAssetCombo) {
        const QSignalBlocker blocker(m_bgAssetCombo);
        const QString currentBg = m_bgAssetCombo->currentData().toString();
        m_bgAssetCombo->clear();
        m_bgAssetCombo->addItem(tr("请选择图片资源"), QString{});
        for (const HmiProjectModel::Asset &asset : m_document.model().assets()) {
            if (asset.kind.trimmed() != QStringLiteral("image")) continue;
            m_bgAssetCombo->addItem(asset.assetId, asset.assetId);
        }
        const int index = m_bgAssetCombo->findData(currentBg);
        m_bgAssetCombo->setCurrentIndex(index >= 0 ? index : 0);
    }
}

void YsereinHmiWorkspace::refreshBackgroundControls()
{
    if (!m_bgTypeCombo || !m_canvas) return;
    const QJsonObject bg = m_canvas->backgroundState();

    const QColor c1(bg.value(QStringLiteral("color")).toString());
    const QColor c2(bg.value(QStringLiteral("color2")).toString());
    if (c1.isValid()) m_bgColor = c1;
    if (c2.isValid()) m_bgColor2 = c2;

    {
        const QSignalBlocker blocker(m_bgTypeCombo);
        const QString type = bg.value(QStringLiteral("type")).toString(QStringLiteral("solid"));
        const int typeIndex = m_bgTypeCombo->findData(type);
        m_bgTypeCombo->setCurrentIndex(typeIndex >= 0 ? typeIndex : 0);
    }
    {
        const QSignalBlocker blocker(m_bgAngleSpin);
        m_bgAngleSpin->setValue(bg.value(QStringLiteral("angle")).toInt(0));
    }
    {
        const QSignalBlocker blocker(m_bgAssetCombo);
        const QString assetId = bg.value(QStringLiteral("assetId")).toString();
        const int assetIndex = m_bgAssetCombo->findData(assetId);
        m_bgAssetCombo->setCurrentIndex(assetIndex >= 0 ? assetIndex : 0);
    }
    {
        const QSignalBlocker blocker(m_bgImageModeCombo);
        const QString imageMode = bg.value(QStringLiteral("imageMode"))
                                      .toString(QStringLiteral("stretch"));
        const int modeIndex = m_bgImageModeCombo->findData(imageMode);
        m_bgImageModeCombo->setCurrentIndex(modeIndex >= 0 ? modeIndex : 0);
    }
    updateBackgroundControlState();
}

void YsereinHmiWorkspace::syncBackgroundToCanvas()
{
    if (!m_canvas || !m_bgTypeCombo) return;
    QJsonObject bg;
    bg.insert(QStringLiteral("type"), m_bgTypeCombo->currentData().toString());
    bg.insert(QStringLiteral("color"), m_bgColor.name(QColor::HexArgb));
    bg.insert(QStringLiteral("color2"), m_bgColor2.name(QColor::HexArgb));
    bg.insert(QStringLiteral("angle"), m_bgAngleSpin->value());
    bg.insert(QStringLiteral("assetId"), m_bgAssetCombo->currentData().toString());
    bg.insert(QStringLiteral("imageMode"), m_bgImageModeCombo->currentData().toString());
    m_canvas->setBackground(bg);
    updateBackgroundControlState();
}

void YsereinHmiWorkspace::pickBackgroundColor(const bool secondary)
{
    if (!m_bgColorButton) return;
    const QColor chosen = QColorDialog::getColor(secondary ? m_bgColor2 : m_bgColor,
                                                 this, tr("选择背景颜色"));
    if (!chosen.isValid()) return;
    if (secondary) m_bgColor2 = chosen;
    else m_bgColor = chosen;
    syncBackgroundToCanvas();
}

void YsereinHmiWorkspace::updateBackgroundControlState()
{
    if (!m_bgTypeCombo || !m_bgColorButton) return;
    const QString type = m_bgTypeCombo->currentData().toString();
    const bool gradient = type == QStringLiteral("linear") || type == QStringLiteral("radial");
    const bool image = type == QStringLiteral("image");
    m_bgColorButton->setEnabled(!image);
    m_bgColor2Button->setEnabled(gradient);
    m_bgAngleSpin->setEnabled(gradient);
    m_bgAssetCombo->setEnabled(image);
    m_bgImageModeCombo->setEnabled(image);
    m_bgColorButton->setText(image ? tr("图像") : tr("颜色 1"));
    applyColorSwatch(m_bgColorButton, m_bgColor);
    applyColorSwatch(m_bgColor2Button, m_bgColor2);
}

QHash<QString, QImage> YsereinHmiWorkspace::loadAssetImages() const
{
    QHash<QString, QImage> images;
    const QString base = m_document.filePath().isEmpty()
        ? QDir::currentPath() : QFileInfo(m_document.filePath()).absolutePath();
    for (const HmiProjectModel::Asset &asset : m_document.model().assets()) {
        if (asset.kind.trimmed() != QStringLiteral("image")) continue;
        const QString path = QDir::isAbsolutePath(asset.source)
            ? QDir::cleanPath(asset.source)
            : QDir(base).absoluteFilePath(asset.source);
        QImage image;
        if (image.load(path)) images.insert(asset.assetId, image);
    }
    return images;
}

void YsereinHmiWorkspace::refreshPageList()
{
    if (!m_pages || !m_canvas) return;
    const QSignalBlocker blocker(m_pages);
    m_pages->clear();
    const QStringList names = m_canvas->pageNames();
    const QStringList ids = m_canvas->pageIds();
    for (int index = 0; index < names.size(); ++index) {
        auto *item = new QListWidgetItem(
            tr("%1  %2").arg(index + 1, 2, 10, QLatin1Char('0')).arg(names.at(index)),
            m_pages);
        item->setData(Qt::UserRole, ids.value(index));
    }
    m_pages->setCurrentRow(m_canvas->currentPage());
    if (m_screenInfo) {
        const int pageIndex = m_canvas->currentPage();
        m_screenInfo->setText(tr("页面 %1  ·  800 × 480  ·  目标预览")
                                  .arg(pageIndex + 1, 2, 10, QLatin1Char('0')));
    }
    refreshEventTargets();
    publishPageContext();
}

void YsereinHmiWorkspace::updateLayerToggleState()
{
    const QSignalBlocker b1(m_layerVisible);
    const QSignalBlocker b2(m_layerLocked);
    const int row = m_layerList ? m_layerList->currentRow() : -1;
    const QListWidgetItem *item = row >= 0 ? m_layerList->item(row) : nullptr;
    if (item) {
        m_layerVisible->setChecked(item->data(Qt::UserRole + 1).toBool());
        m_layerLocked->setChecked(item->data(Qt::UserRole + 2).toBool());
    } else {
        m_layerVisible->setChecked(false);
        m_layerLocked->setChecked(false);
    }
}

void YsereinHmiWorkspace::refreshLayerList()
{
    if (!m_layerList) return;
    const QSignalBlocker blocker(m_layerList);
    m_layerList->clear();
    if (!m_canvas) return;
    const QJsonArray layers = m_canvas->layerList();
    int selectedRow = -1;
    for (int index = 0; index < layers.size(); ++index) {
        const QJsonObject layer = layers.at(index).toObject();
        const QString label = layer.value(QStringLiteral("label")).toString();
        const QString name = layer.value(QStringLiteral("id")).toString();
        const QString typeLabel =
            controlLabelForType(layer.value(QStringLiteral("type")).toString());
        const bool visible = layer.value(QStringLiteral("visible")).toBool();
        const bool locked = layer.value(QStringLiteral("locked")).toBool();
        const bool selected = layer.value(QStringLiteral("selected")).toBool();
        auto *item = new QListWidgetItem(
            tr("%1  ·  %2  ·  %3").arg(label, name, typeLabel), m_layerList);
        item->setData(Qt::UserRole, layer.value(QStringLiteral("instanceId")).toString());
        item->setData(Qt::UserRole + 1, visible);
        item->setData(Qt::UserRole + 2, locked);
        if (selected) selectedRow = index;
    }
    if (selectedRow >= 0) m_layerList->setCurrentRow(selectedRow);
    updateLayerToggleState();
}

void YsereinHmiWorkspace::removeSelectedLayer()
{
    if (!m_canvas || !m_layerList) return;
    const int row = m_layerList->currentRow();
    if (row < 0) return;
    const QListWidgetItem *item = m_layerList->item(row);
    if (!item) return;
    const QString instanceId = item->data(Qt::UserRole).toString();
    if (m_canvas->selectLayer(instanceId)) {
        m_canvas->removeSelectedControl();
    }
}

void YsereinHmiWorkspace::publishPageContext()
{
    if (!m_uiProvider || !m_canvas) return;
    const QString pageId = m_canvas->pageIds().value(m_canvas->currentPage());
    if (pageId.isEmpty()) return;
    WorkspaceUiContext context = m_uiProvider->context();
    const QString nextId = QStringLiteral("yserein-hmi/designer/page/%1").arg(pageId);
    if (context.id == nextId) return;
    context.id = nextId;
    const WorkspaceUiPresentationOptions options = m_uiProvider->presentationOptions();
    m_uiProvider->prepareContextChange();
    m_uiProvider->setContext(std::move(context), options);
}

void YsereinHmiWorkspace::markDocumentDirty()
{
    if (m_loadingDocument) return;
    m_documentDirty = true;
    updateProjectStatus();
}

void YsereinHmiWorkspace::updateProjectStatus()
{
    const QString path = m_document.filePath();
    if (m_projectPath) {
        m_projectPath->setText(path.isEmpty()
            ? tr("未保存工程") : QFileInfo(path).fileName());
        m_projectPath->setToolTip(path);
    }
    if (m_saveButton) m_saveButton->setText(m_documentDirty ? tr("保存*") : tr("保存"));
}

QWidget *YsereinHmiWorkspace::buildContextPanel()
{
    auto *panel = new QWidget(this);
    panel->setObjectName(QStringLiteral("hmiContextPanel"));
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    layout->addWidget(makeSectionTitle(tr("工程"), panel));
    auto *projectActions = new QHBoxLayout;
    auto *newButton = makeToolButton(tr("新建"), panel);
    auto *openButton = makeToolButton(tr("打开"), panel);
    m_saveButton = makeToolButton(tr("保存"), panel, QStringLiteral("primary"));
    projectActions->addWidget(newButton);
    projectActions->addWidget(openButton);
    projectActions->addWidget(m_saveButton);
    layout->addLayout(projectActions);
    m_projectPath = new QLineEdit(panel);
    m_projectPath->setObjectName(QStringLiteral("hmiPropertyField"));
    m_projectPath->setReadOnly(true);
    m_projectPath->setPlaceholderText(tr("未保存工程"));
    layout->addWidget(m_projectPath);
    auto *projectHint = new QLabel(
        tr("离线作者模式 · .yshm 工程只保存模型，不包含串口或固件数据。"), panel);
    projectHint->setObjectName(QStringLiteral("hmiSubtle"));
    projectHint->setWordWrap(true);
    layout->addWidget(projectHint);

    layout->addWidget(makeSectionTitle(tr("页面"), panel));
    m_pages = new QListWidget(panel);
    m_pages->setObjectName(QStringLiteral("hmiList"));
    refreshPageList();
    layout->addWidget(m_pages, 1);
    auto *pageActions = new QHBoxLayout;
    auto *addPageButton = makeToolButton(tr("新建页面"), panel);
    auto *duplicatePageButton = makeToolButton(tr("复制"), panel);
    auto *renamePageButton = makeToolButton(tr("重命名"), panel);
    auto *removePageButton = makeToolButton(tr("删除"), panel);
    pageActions->addWidget(addPageButton);
    pageActions->addWidget(duplicatePageButton);
    pageActions->addWidget(renamePageButton);
    pageActions->addWidget(removePageButton);
    layout->addLayout(pageActions);
    auto *pageHint = new QLabel(tr("页面属于 HMI 工程；切换不会改变 Scope 工作区。"), panel);
    pageHint->setObjectName(QStringLiteral("hmiSubtle"));
    pageHint->setWordWrap(true);
    layout->addWidget(pageHint);

    layout->addWidget(makeSectionTitle(tr("图层"), panel));
    m_layerList = new QListWidget(panel);
    m_layerList->setObjectName(QStringLiteral("hmiList"));
    m_layerList->setToolTip(tr("当前页控件，自顶向下列出；单击选中"));
    layout->addWidget(m_layerList);
    auto *layerToggles = new QHBoxLayout;
    m_layerVisible = new QCheckBox(tr("可见"), panel);
    m_layerLocked = new QCheckBox(tr("锁定"), panel);
    layerToggles->addWidget(m_layerVisible);
    layerToggles->addWidget(m_layerLocked);
    layout->addLayout(layerToggles);
    auto *layerActions = new QHBoxLayout;
    auto *layerUpButton = makeToolButton(tr("上移"), panel);
    auto *layerDownButton = makeToolButton(tr("下移"), panel);
    auto *layerDeleteButton = makeToolButton(tr("删除"), panel);
    layerActions->addWidget(layerUpButton);
    layerActions->addWidget(layerDownButton);
    layerActions->addWidget(layerDeleteButton);
    layout->addLayout(layerActions);

    layout->addWidget(makeSectionTitle(tr("设计模式"), panel));
    m_preview = new QToolButton(panel);
    m_preview->setObjectName(QStringLiteral("hmiPreviewButton"));
    m_preview->setText(tr("预览"));
    m_preview->setCheckable(true);
    m_preview->setCursor(Qt::PointingHandCursor);
    layout->addWidget(m_preview);
    auto *grid = new QCheckBox(tr("显示 8px 网格"), panel);
    grid->setChecked(true);
    grid->setObjectName(QStringLiteral("hmiGridToggle"));
    layout->addWidget(grid);

    auto *deviceControls = new QHBoxLayout;
    m_deviceSizeCombo = new QComboBox(panel);
    m_deviceSizeCombo->setObjectName(QStringLiteral("hmiPropertyField"));
    m_deviceSizeCombo->addItem(tr("800 × 480"), QSize(800, 480));
    m_deviceSizeCombo->addItem(tr("1024 × 600"), QSize(1024, 600));
    m_deviceSizeCombo->addItem(tr("1280 × 800"), QSize(1280, 800));
    m_deviceSizeCombo->addItem(tr("1920 × 1080"), QSize(1920, 1080));
    m_deviceSizeCombo->setToolTip(tr("目标屏分辨率（真实像素格数）"));
    m_rotationCombo = new QComboBox(panel);
    m_rotationCombo->setObjectName(QStringLiteral("hmiPropertyField"));
    m_rotationCombo->addItem(tr("0°"), 0);
    m_rotationCombo->addItem(tr("90°"), 90);
    m_rotationCombo->addItem(tr("180°"), 180);
    m_rotationCombo->addItem(tr("270°"), 270);
    m_rotationCombo->setToolTip(tr("屏幕安装翻转方向"));
    deviceControls->addWidget(m_deviceSizeCombo, 1);
    deviceControls->addWidget(m_rotationCombo);
    layout->addLayout(deviceControls);
    auto *deviceHint = new QLabel(
        tr("画布以目标屏的真实分辨率铺满像素格，底色默认白；翻转不影响控件坐标。"), panel);
    deviceHint->setObjectName(QStringLiteral("hmiSubtle"));
    deviceHint->setWordWrap(true);
    layout->addWidget(deviceHint);

    layout->addWidget(makeSectionTitle(tr("页面背景"), panel));
    m_bgTypeCombo = new QComboBox(panel);
    m_bgTypeCombo->setObjectName(QStringLiteral("hmiPropertyField"));
    m_bgTypeCombo->addItem(tr("纯色"), QStringLiteral("solid"));
    m_bgTypeCombo->addItem(tr("线性渐变"), QStringLiteral("linear"));
    m_bgTypeCombo->addItem(tr("径向渐变"), QStringLiteral("radial"));
    m_bgTypeCombo->addItem(tr("图片"), QStringLiteral("image"));
    m_bgTypeCombo->setToolTip(tr("页面背景填充类型"));
    layout->addWidget(m_bgTypeCombo);
    auto *bgColors = new QHBoxLayout;
    m_bgColorButton = makeToolButton(tr("颜色 1"), panel);
    m_bgColorButton->setToolTip(tr("背景主色"));
    m_bgColor2Button = makeToolButton(tr("颜色 2"), panel);
    m_bgColor2Button->setToolTip(tr("渐变结束色"));
    bgColors->addWidget(m_bgColorButton, 1);
    bgColors->addWidget(m_bgColor2Button, 1);
    layout->addLayout(bgColors);
    m_bgAngleSpin = new QSpinBox(panel);
    m_bgAngleSpin->setObjectName(QStringLiteral("hmiPropertyField"));
    m_bgAngleSpin->setRange(0, 359);
    m_bgAngleSpin->setSuffix(QStringLiteral("°"));
    m_bgAngleSpin->setToolTip(tr("渐变角度"));
    layout->addWidget(m_bgAngleSpin);
    m_bgAssetCombo = new QComboBox(panel);
    m_bgAssetCombo->setObjectName(QStringLiteral("hmiPropertyField"));
    m_bgAssetCombo->setToolTip(tr("作为背景的图片资源"));
    m_bgAssetCombo->addItem(tr("请选择图片资源"), QString{});
    layout->addWidget(m_bgAssetCombo);
    m_bgImageModeCombo = new QComboBox(panel);
    m_bgImageModeCombo->setObjectName(QStringLiteral("hmiPropertyField"));
    m_bgImageModeCombo->addItem(tr("拉伸铺满"), QStringLiteral("stretch"));
    m_bgImageModeCombo->addItem(tr("覆盖(裁剪)"), QStringLiteral("cover"));
    m_bgImageModeCombo->addItem(tr("包含(留边)"), QStringLiteral("contain"));
    m_bgImageModeCombo->addItem(tr("平铺"), QStringLiteral("tile"));
    m_bgImageModeCombo->addItem(tr("居中(原尺寸)"), QStringLiteral("center"));
    m_bgImageModeCombo->setToolTip(tr("背景图片填充方式"));
    layout->addWidget(m_bgImageModeCombo);

    if (m_canvas) {
        const int sizeIndex = m_deviceSizeCombo->findData(QSize(qRound(m_canvas->deviceSize().width()),
                                                               qRound(m_canvas->deviceSize().height())));
        m_deviceSizeCombo->setCurrentIndex(sizeIndex >= 0 ? sizeIndex : 0);
        m_rotationCombo->setCurrentIndex(
            m_rotationCombo->findData(m_canvas->rotation()));
        refreshBackgroundControls();
    }

    layout->addWidget(makeSectionTitle(tr("资源"), panel));
    m_assetList = new QListWidget(panel);
    m_assetList->setObjectName(QStringLiteral("hmiList"));
    m_assetList->setToolTip(tr("已登记的图片资源"));
    layout->addWidget(m_assetList);
    auto *assetActions = new QHBoxLayout;
    auto *importImageButton = makeToolButton(tr("导入图片"), panel);
    auto *removeAssetButton = makeToolButton(tr("移除"), panel);
    assetActions->addWidget(importImageButton);
    assetActions->addWidget(removeAssetButton);
    layout->addLayout(assetActions);
    auto *resourceHint = new QLabel(
        tr("图片控件必须绑定已登记的图片资源，保存或生成前会校验引用。"), panel);
    resourceHint->setObjectName(QStringLiteral("hmiSubtle"));
    resourceHint->setWordWrap(true);
    layout->addWidget(resourceHint);

    layout->addWidget(makeSectionTitle(tr("生成与下载"), panel));
    auto *buildButton = makeToolButton(tr("生成本机目标包"), panel, QStringLiteral("primary"));
    auto *runtimeButton = makeToolButton(tr("Runtime 自测"), panel);
    auto *downloadButton = makeToolButton(tr("下载到本机模拟器"), panel);
    layout->addWidget(buildButton);
    layout->addWidget(runtimeButton);
    layout->addWidget(downloadButton);
    auto *pipelineHint = new QLabel(
        tr("本机目标包、协议回环和目录下载可验证；不等于芯片固件或串口烧录。"), panel);
    pipelineHint->setObjectName(QStringLiteral("hmiSubtle"));
    pipelineHint->setWordWrap(true);
    layout->addWidget(pipelineHint);

    connect(m_pages, &QListWidget::currentRowChanged,
            this, &YsereinHmiWorkspace::selectPage);
    connect(newButton, &QPushButton::clicked,
            this, &YsereinHmiWorkspace::newProject);
    connect(openButton, &QPushButton::clicked,
            this, &YsereinHmiWorkspace::openProject);
    connect(m_saveButton, &QPushButton::clicked,
            this, &YsereinHmiWorkspace::saveProject);
    connect(addPageButton, &QPushButton::clicked,
            this, &YsereinHmiWorkspace::addPage);
    connect(duplicatePageButton, &QPushButton::clicked,
            this, &YsereinHmiWorkspace::duplicatePage);
    connect(renamePageButton, &QPushButton::clicked,
            this, &YsereinHmiWorkspace::renamePage);
    connect(removePageButton, &QPushButton::clicked,
            this, &YsereinHmiWorkspace::removePage);
    // 图层区：列表点击选中画布控件，勾选与按钮回写画布，画布变化刷新列表。
    connect(m_layerList, &QListWidget::currentRowChanged,
            this, [this](const int row) {
        if (!m_canvas) return;
        if (row < 0) {
            updateLayerToggleState();
            return;
        }
        const QListWidgetItem *item = m_layerList->item(row);
        const QString instanceId = item->data(Qt::UserRole).toString();
        m_canvas->selectLayer(instanceId);
        updateLayerToggleState();
    });
    connect(m_layerVisible, &QCheckBox::toggled, this, [this](const bool checked) {
        if (!m_canvas || !m_layerList) return;
        const int row = m_layerList->currentRow();
        if (row < 0) return;
        const QString instanceId = m_layerList->item(row)->data(Qt::UserRole).toString();
        if (m_canvas->selectLayer(instanceId)) {
            m_canvas->setSelectedVisible(checked);
        } else {
            updateLayerToggleState();
        }
    });
    connect(m_layerLocked, &QCheckBox::toggled, this, [this](const bool checked) {
        if (!m_canvas || !m_layerList) return;
        const int row = m_layerList->currentRow();
        if (row < 0) return;
        const QString instanceId = m_layerList->item(row)->data(Qt::UserRole).toString();
        if (m_canvas->selectLayer(instanceId)) {
            m_canvas->setSelectedLocked(checked);
        } else {
            updateLayerToggleState();
        }
    });
    connect(layerUpButton, &QPushButton::clicked, this, [this] {
        if (!m_canvas || !m_layerList) return;
        const int row = m_layerList->currentRow();
        if (row < 0) return;
        const QString instanceId = m_layerList->item(row)->data(Qt::UserRole).toString();
        m_canvas->moveLayer(instanceId, 1);
    });
    connect(layerDownButton, &QPushButton::clicked, this, [this] {
        if (!m_canvas || !m_layerList) return;
        const int row = m_layerList->currentRow();
        if (row < 0) return;
        const QString instanceId = m_layerList->item(row)->data(Qt::UserRole).toString();
        m_canvas->moveLayer(instanceId, -1);
    });
    connect(layerDeleteButton, &QPushButton::clicked,
            this, &YsereinHmiWorkspace::removeSelectedLayer);
    connect(m_preview, &QToolButton::clicked,
            this, &YsereinHmiWorkspace::togglePreview);
    connect(grid, &QCheckBox::toggled,
            m_canvas, &DesignerCanvas::setGridVisible);
    connect(m_deviceSizeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](const int index) {
        const QSize size = m_deviceSizeCombo->itemData(index).toSize();
        setDeviceSize(size.width(), size.height());
    });
    connect(m_rotationCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](const int index) {
        setRotation(m_rotationCombo->itemData(index).toInt());
    });
    connect(m_bgTypeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { syncBackgroundToCanvas(); });
    connect(m_bgAngleSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, [this](int) { syncBackgroundToCanvas(); });
    connect(m_bgAssetCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { syncBackgroundToCanvas(); });
    connect(m_bgImageModeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { syncBackgroundToCanvas(); });
    connect(m_bgColorButton, &QPushButton::clicked,
            this, [this] { pickBackgroundColor(false); });
    connect(m_bgColor2Button, &QPushButton::clicked,
            this, [this] { pickBackgroundColor(true); });
    connect(buildButton, &QPushButton::clicked,
            this, &YsereinHmiWorkspace::buildSimulatorTarget);
    connect(runtimeButton, &QPushButton::clicked,
            this, &YsereinHmiWorkspace::runSimulatorRuntime);
    connect(downloadButton, &QPushButton::clicked,
            this, &YsereinHmiWorkspace::downloadToSimulator);
    connect(importImageButton, &QPushButton::clicked,
            this, &YsereinHmiWorkspace::importImage);
    connect(removeAssetButton, &QPushButton::clicked,
            this, &YsereinHmiWorkspace::removeSelectedAsset);
    return panel;
}

QWidget *YsereinHmiWorkspace::buildInspectorPanel()
{
    auto *panel = new QFrame(this);
    panel->setObjectName(QStringLiteral("hmiInspectorPanel"));
    auto *root = new QVBoxLayout(panel);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(8);
    auto *header = new QHBoxLayout;
    header->addWidget(makeSectionTitle(tr("选中控件"), panel));
    m_selectionBadge = new QLabel(tr("已选中"), panel);
    m_selectionBadge->setObjectName(QStringLiteral("hmiSelectionBadge"));
    header->addWidget(m_selectionBadge, 0, Qt::AlignRight);
    root->addLayout(header);

    auto *selectionGroup = new QFrame(panel);
    selectionGroup->setObjectName(QStringLiteral("hmiFieldGroup"));
    auto *selectionLayout = new QFormLayout(selectionGroup);
    selectionLayout->setContentsMargins(8, 8, 8, 8);
    selectionLayout->setSpacing(6);
    auto makeField = [selectionGroup](const bool readOnly) {
        auto *field = new QLineEdit(selectionGroup);
        field->setObjectName(QStringLiteral("hmiPropertyField"));
        field->setReadOnly(readOnly);
        return field;
    };
    m_selectedName = makeField(false);
    m_selectedType = makeField(true);
    m_selectedBinding = makeField(false);
    selectionLayout->addRow(tr("名称"), m_selectedName);
    selectionLayout->addRow(tr("类型"), m_selectedType);
    selectionLayout->addRow(tr("数据绑定"), m_selectedBinding);
    m_bindingMode = new QComboBox(selectionGroup);
    m_bindingMode->setObjectName(QStringLiteral("hmiPropertyField"));
    m_bindingMode->addItem(tr("无绑定"), QStringLiteral("none"));
    m_bindingMode->addItem(tr("只读"), QStringLiteral("read"));
    m_bindingMode->addItem(tr("只写"), QStringLiteral("write"));
    m_bindingMode->addItem(tr("读写"), QStringLiteral("readWrite"));
    selectionLayout->addRow(tr("绑定方向"), m_bindingMode);
    m_geometryX = new QDoubleSpinBox(selectionGroup);
    m_geometryY = new QDoubleSpinBox(selectionGroup);
    m_geometryWidth = new QDoubleSpinBox(selectionGroup);
    m_geometryHeight = new QDoubleSpinBox(selectionGroup);
    for (QDoubleSpinBox *field : {m_geometryX, m_geometryY, m_geometryWidth, m_geometryHeight}) {
        field->setObjectName(QStringLiteral("hmiPropertyField"));
        field->setRange(0.0, 800.0);
        field->setDecimals(0);
        field->setSingleStep(8.0);
    }
    m_geometryWidth->setRange(8.0, 800.0);
    m_geometryHeight->setRange(8.0, 480.0);
    selectionLayout->addRow(tr("位置 X"), m_geometryX);
    selectionLayout->addRow(tr("位置 Y"), m_geometryY);
    selectionLayout->addRow(tr("宽度"), m_geometryWidth);
    selectionLayout->addRow(tr("高度"), m_geometryHeight);
    m_visible = new QCheckBox(tr("可见"), selectionGroup);
    m_locked = new QCheckBox(tr("锁定位置"), selectionGroup);
    selectionLayout->addRow(m_visible, m_locked);
    root->addWidget(selectionGroup);

    m_numericProperties = new QFrame(panel);
    m_numericProperties->setObjectName(QStringLiteral("hmiFieldGroup"));
    auto *numericLayout = new QFormLayout(m_numericProperties);
    numericLayout->setContentsMargins(8, 8, 8, 8);
    numericLayout->setSpacing(6);
    m_previewValue = new QDoubleSpinBox(m_numericProperties);
    m_previewValue->setObjectName(QStringLiteral("hmiPropertyField"));
    m_previewValue->setRange(-1000000000.0, 1000000000.0);
    m_previewValue->setDecimals(6);
    m_previewValue->setKeyboardTracking(false);
    m_decimals = new QSpinBox(m_numericProperties);
    m_decimals->setObjectName(QStringLiteral("hmiPropertyField"));
    m_decimals->setRange(0, 6);
    m_unit = new QLineEdit(m_numericProperties);
    m_unit->setObjectName(QStringLiteral("hmiPropertyField"));
    m_unit->setMaxLength(12);
    m_fontPixelSize = new QSpinBox(m_numericProperties);
    m_fontPixelSize->setObjectName(QStringLiteral("hmiPropertyField"));
    m_fontPixelSize->setRange(12, 72);
    m_fontPixelSize->setSuffix(QStringLiteral(" px"));
    m_alignment = new QComboBox(m_numericProperties);
    m_alignment->setObjectName(QStringLiteral("hmiPropertyField"));
    m_alignment->addItems({tr("左对齐"), tr("居中"), tr("右对齐")});
    numericLayout->addRow(tr("模拟值"), m_previewValue);
    numericLayout->addRow(tr("小数位"), m_decimals);
    numericLayout->addRow(tr("单位"), m_unit);
    numericLayout->addRow(tr("字号"), m_fontPixelSize);
    numericLayout->addRow(tr("对齐"), m_alignment);
    m_numericProperties->hide();

    m_genericProperties = new QFrame(panel);
    m_genericProperties->setObjectName(QStringLiteral("hmiFieldGroup"));
    auto *genericLayout = new QFormLayout(m_genericProperties);
    genericLayout->setContentsMargins(8, 8, 8, 8);
    genericLayout->setSpacing(6);
    m_textValueLabel = new QLabel(tr("显示文本"), m_genericProperties);
    m_textValue = new QLineEdit(m_genericProperties);
    m_textValue->setObjectName(QStringLiteral("hmiPropertyField"));
    m_textValue->setMaxLength(64);
    m_controlValueLabel = new QLabel(tr("模拟值"), m_genericProperties);
    m_controlValue = new QDoubleSpinBox(m_genericProperties);
    m_controlValue->setObjectName(QStringLiteral("hmiPropertyField"));
    m_controlValue->setRange(0.0, 1.0);
    m_controlValue->setDecimals(3);
    m_controlValue->setSingleStep(0.05);
    genericLayout->addRow(m_textValueLabel, m_textValue);
    genericLayout->addRow(m_controlValueLabel, m_controlValue);
    m_assetSelector = new QComboBox(m_genericProperties);
    m_assetSelector->setObjectName(QStringLiteral("hmiPropertyField"));
    m_assetSelector->addItem(tr("请选择图片资源"), QString{});
    m_scaleMode = new QComboBox(m_genericProperties);
    m_scaleMode->setObjectName(QStringLiteral("hmiPropertyField"));
    m_scaleMode->addItem(tr("适应"), QStringLiteral("fit"));
    m_scaleMode->addItem(tr("裁切填充"), QStringLiteral("fill"));
    m_scaleMode->addItem(tr("拉伸"), QStringLiteral("stretch"));
    m_scaleMode->addItem(tr("原尺寸"), QStringLiteral("none"));
    m_inputMaxLength = new QSpinBox(m_genericProperties);
    m_inputMaxLength->setObjectName(QStringLiteral("hmiPropertyField"));
    m_inputMaxLength->setRange(1, 256);
    m_chartMin = new QDoubleSpinBox(m_genericProperties);
    m_chartMax = new QDoubleSpinBox(m_genericProperties);
    for (QDoubleSpinBox *field : {m_chartMin, m_chartMax}) {
        field->setObjectName(QStringLiteral("hmiPropertyField"));
        field->setRange(-1000000000.0, 1000000000.0);
    }
    m_chartWindowLength = new QSpinBox(m_genericProperties);
    m_chartWindowLength->setObjectName(QStringLiteral("hmiPropertyField"));
    m_chartWindowLength->setRange(2, 4096);
    m_chartSamples = new QLineEdit(m_genericProperties);
    m_chartSamples->setObjectName(QStringLiteral("hmiPropertyField"));
    m_chartSamples->setPlaceholderText(tr("例如 20,24.5,23"));
    genericLayout->addRow(tr("图片资源"), m_assetSelector);
    genericLayout->addRow(tr("缩放模式"), m_scaleMode);
    genericLayout->addRow(tr("最大输入长度"), m_inputMaxLength);
    genericLayout->addRow(tr("曲线下限"), m_chartMin);
    genericLayout->addRow(tr("曲线上限"), m_chartMax);
    genericLayout->addRow(tr("曲线窗口"), m_chartWindowLength);
    genericLayout->addRow(tr("模拟采样"), m_chartSamples);
    m_genericProperties->hide();

    root->addWidget(makeSectionTitle(tr("属性"), panel));
    m_schemaInspector = new SchemaInspector(panel);
    root->addWidget(m_schemaInspector);

    root->addWidget(makeSectionTitle(tr("事件"), panel));
    auto *eventGroup = new QFrame(panel);
    m_eventProperties = eventGroup;
    eventGroup->setObjectName(QStringLiteral("hmiFieldGroup"));
    auto *eventLayout = new QFormLayout(eventGroup);
    eventLayout->setContentsMargins(8, 8, 8, 8);
    eventLayout->setSpacing(6);
    m_eventType = new QComboBox(eventGroup);
    m_eventType->setObjectName(QStringLiteral("hmiPropertyField"));
    m_eventType->addItem(tr("无动作"), QStringLiteral("none"));
    m_eventType->addItem(tr("跳转页面"), QStringLiteral("gotoPage"));
    m_eventType->addItem(tr("提交输入"), QStringLiteral("submit"));
    m_eventTarget = new QComboBox(eventGroup);
    m_eventTarget->setObjectName(QStringLiteral("hmiPropertyField"));
    m_eventTarget->setEditable(false);
    refreshEventTargets();
    eventLayout->addRow(tr("触发动作"), m_eventType);
    eventLayout->addRow(tr("目标"), m_eventTarget);
    root->addWidget(eventGroup);

    root->addWidget(makeSectionTitle(tr("显示规则"), panel));
    auto *ruleGroup = new QFrame(panel);
    ruleGroup->setObjectName(QStringLiteral("hmiFieldGroup"));
    auto *ruleLayout = new QVBoxLayout(ruleGroup);
    ruleLayout->setContentsMargins(8, 8, 8, 8);
    ruleLayout->setSpacing(5);
    auto *validRule = new QLabel(tr("有效数据  →  数值 + 单位"), ruleGroup);
    validRule->setObjectName(QStringLiteral("hmiPropertyLabel"));
    auto *invalidRule = new QLabel(
        tr("实时数据  →  尚未接入；预览只使用模拟值"), ruleGroup);
    invalidRule->setObjectName(QStringLiteral("hmiPropertyLabel"));
    ruleLayout->addWidget(validRule);
    ruleLayout->addWidget(invalidRule);
    root->addWidget(ruleGroup);

    auto *actions = new QHBoxLayout;
    auto *alignButton = makeToolButton(tr("自动对齐"), panel);
    auto *removeButton = makeToolButton(tr("删除"), panel);
    actions->addWidget(alignButton, 1);
    actions->addWidget(removeButton);
    root->addLayout(actions);
    root->addStretch(1);

    connect(alignButton, &QPushButton::clicked,
            this, &YsereinHmiWorkspace::alignSelectedControl);
    connect(removeButton, &QPushButton::clicked,
            m_canvas, &DesignerCanvas::removeSelectedControl);
    connect(m_selectedName, &QLineEdit::editingFinished, this, [this] {
        if (m_updatingInspector || !m_canvas) return;
        if (!m_canvas->setSelectedNumericId(m_selectedName->text())) {
            setStatus(tr("名称无效或重复：使用字母、数字和下划线，且不能以数字开头"));
            m_canvas->refreshSelection();
        } else {
            setStatus(tr("已更新控件名称"));
        }
    });
    connect(m_selectedBinding, &QLineEdit::editingFinished, this, [this] {
        if (m_updatingInspector || !m_canvas) return;
        if (!m_canvas->setSelectedNumericBinding(m_selectedBinding->text())) {
            setStatus(tr("绑定无效：使用字母、数字、下划线和点号"));
            m_canvas->refreshSelection();
        } else {
            setStatus(tr("已更新控件数据绑定"));
        }
    });
    connect(m_previewValue, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](const double value) {
        if (!m_updatingInspector && m_canvas) {
            m_canvas->setSelectedNumericPreviewValue(value);
        }
    });
    connect(m_decimals, qOverload<int>(&QSpinBox::valueChanged),
            this, [this](const int value) {
        if (!m_updatingInspector && m_canvas) m_canvas->setSelectedNumericDecimals(value);
    });
    connect(m_unit, &QLineEdit::textChanged, this, [this](const QString &value) {
        if (!m_updatingInspector && m_canvas) m_canvas->setSelectedNumericUnit(value);
    });
    connect(m_fontPixelSize, qOverload<int>(&QSpinBox::valueChanged),
            this, [this](const int value) {
        if (!m_updatingInspector && m_canvas) {
            m_canvas->setSelectedNumericFontPixelSize(value);
        }
    });
    connect(m_alignment, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](const int index) {
        if (!m_updatingInspector && m_canvas) m_canvas->setSelectedNumericAlignment(index);
    });
    connect(m_textValue, &QLineEdit::editingFinished, this, [this] {
        if (!m_updatingInspector && m_canvas
            && !m_canvas->setSelectedTextValue(m_textValue->text())) {
            setStatus(tr("文本内容不能为空"));
            m_canvas->refreshSelection();
        }
    });
    connect(m_controlValue, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](const double value) {
        if (!m_updatingInspector && m_canvas) {
            if (!m_canvas->setSelectedProgressValue(value)) {
                // Status lamps and switches use the same normalized preview value.
                m_canvas->setSelectedControlPreviewValue(value);
            }
        }
    });
    connect(m_assetSelector, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](const int) {
        if (!m_updatingInspector && m_canvas) {
            m_canvas->setSelectedAssetId(m_assetSelector->currentData().toString());
        }
    });
    connect(m_scaleMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](const int) {
        if (!m_updatingInspector && m_canvas)
            m_canvas->setSelectedScaleMode(m_scaleMode->currentData().toString());
    });
    connect(m_inputMaxLength, qOverload<int>(&QSpinBox::valueChanged), this, [this](const int value) {
        if (!m_updatingInspector && m_canvas) m_canvas->setSelectedInputMaxLength(value);
    });
    const auto updateChartRange = [this] {
        if (!m_updatingInspector && m_canvas
            && !m_canvas->setSelectedChartRange(m_chartMin->value(), m_chartMax->value())) {
            setStatus(tr("曲线范围无效：上限必须大于下限"));
            m_canvas->refreshSelection();
        }
    };
    connect(m_chartMin, &QDoubleSpinBox::editingFinished, this, updateChartRange);
    connect(m_chartMax, &QDoubleSpinBox::editingFinished, this, updateChartRange);
    connect(m_chartWindowLength, qOverload<int>(&QSpinBox::valueChanged), this, [this](const int value) {
        if (!m_updatingInspector && m_canvas) m_canvas->setSelectedChartWindowLength(value);
    });
    connect(m_chartSamples, &QLineEdit::editingFinished, this, [this] {
        if (m_updatingInspector || !m_canvas) return;
        QJsonArray samples;
        bool valid = true;
        for (const QString &part : m_chartSamples->text().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            bool ok = false;
            const double value = part.trimmed().toDouble(&ok);
            if (!ok) { valid = false; break; }
            samples.append(value);
        }
        if (!valid || !m_canvas->setSelectedChartSamples(samples)) {
            setStatus(tr("曲线采样无效：请输入逗号分隔的数值"));
            m_canvas->refreshSelection();
        }
    });
    connect(m_bindingMode, &QComboBox::currentTextChanged, this, [this] {
        if (!m_updatingInspector && m_canvas)
            m_canvas->setSelectedBindingMode(m_bindingMode->currentData().toString());
    });
    const auto updateGeometry = [this] {
        if (!m_updatingInspector && m_canvas
            && !m_canvas->setSelectedGeometry(m_geometryX->value(), m_geometryY->value(),
                                              m_geometryWidth->value(), m_geometryHeight->value())) {
            m_canvas->refreshSelection();
            setStatus(tr("几何超出 800 × 480 画布或控件已锁定"));
        }
    };
    for (QDoubleSpinBox *field : {m_geometryX, m_geometryY, m_geometryWidth, m_geometryHeight})
        connect(field, &QDoubleSpinBox::editingFinished, this, updateGeometry);
    connect(m_visible, &QCheckBox::toggled, this, [this](const bool value) {
        if (!m_updatingInspector && m_canvas) m_canvas->setSelectedVisible(value);
    });
    connect(m_locked, &QCheckBox::toggled, this, [this](const bool value) {
        if (!m_updatingInspector && m_canvas) m_canvas->setSelectedLocked(value);
    });
    connect(m_eventType, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](const int) {
        const bool needsTarget = m_eventType->currentData().toString()
            == QStringLiteral("gotoPage");
        m_eventTarget->setEnabled(needsTarget);
        if (needsTarget && m_eventTarget->currentData().toString().isEmpty() && m_canvas) {
            const QString currentPageId = m_canvas->pageIds().value(m_canvas->currentPage());
            int targetIndex = 0;
            for (int index = 1; index < m_eventTarget->count(); ++index) {
                if (m_eventTarget->itemData(index).toString() != currentPageId) {
                    targetIndex = index;
                    break;
                }
            }
            const QSignalBlocker blocker(m_eventTarget);
            m_eventTarget->setCurrentIndex(targetIndex);
        }
        if (!m_updatingInspector && m_canvas
            && !m_canvas->setSelectedEvent(m_eventType->currentData().toString(),
                                           needsTarget ? m_eventTarget->currentData().toString()
                                                       : QString{})) {
            setStatus(tr("事件目标无效"));
            m_canvas->refreshSelection();
        }
    });
    connect(m_eventTarget, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](const int) {
        if (!m_updatingInspector && m_canvas
            && m_eventType->currentData().toString() == QStringLiteral("gotoPage")) {
            m_canvas->setSelectedEvent(m_eventType->currentData().toString(),
                                        m_eventTarget->currentData().toString());
        }
    });
    connect(m_schemaInspector, &SchemaInspector::propertyEdited,
            this, [this](const QString &key, const QJsonValue &value) {
        if (m_updatingInspector || !m_canvas) return;
        m_schemaInspectorEditing = true;
        const bool applied = m_canvas->setSelectedProperty(key, value);
        m_schemaInspectorEditing = false;
        if (!applied) setStatus(tr("属性更新失败：%1").arg(key));
    });
    return panel;
}

QWidget *YsereinHmiWorkspace::buildOutputPanel()
{
    auto *panel = new QFrame(this);
    panel->setObjectName(QStringLiteral("hmiOutputPanel"));
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    m_status = new QLabel(tr("准备就绪 · 设计模式"), panel);
    m_status->setObjectName(QStringLiteral("hmiSubtle"));
    m_status->setWordWrap(true);
    layout->addWidget(m_status);
    auto *boundary = new QLabel(
        tr("离线闭环：工程/页面/九类控件/绑定/预览/保存及本机目标包、Runtime 自测、模拟下载已接入；图片资源导入、真实串口、芯片 Runtime 和烧录尚未接入。"),
        panel);
    boundary->setObjectName(QStringLiteral("hmiSubtle"));
    boundary->setWordWrap(true);
    layout->addWidget(boundary);
    layout->addStretch(1);
    return panel;
}

QWidget *YsereinHmiWorkspace::buildCanvasPanel()
{
    auto *surface = new ComponentSurface(tr("画布"), this);
    auto *layout = new QVBoxLayout(surface->contentHost());
    layout->setContentsMargins(12, 8, 12, 10);
    layout->setSpacing(8);
    auto *canvasMeta = new QHBoxLayout;
    canvasMeta->addStretch(1);
    m_undoButton = makeToolButton(tr("撤销"), surface->contentHost());
    m_redoButton = makeToolButton(tr("重做"), surface->contentHost());
    m_undoButton->setEnabled(false);
    m_redoButton->setEnabled(false);
    canvasMeta->addWidget(m_undoButton);
    canvasMeta->addWidget(m_redoButton);
    m_screenInfo = new QLabel(tr("页面 01  ·  800 × 480  ·  目标预览"),
                              surface->contentHost());
    m_screenInfo->setObjectName(QStringLiteral("hmiSubtle"));
    canvasMeta->addWidget(m_screenInfo, 0, Qt::AlignRight);
    layout->addLayout(canvasMeta);
    m_canvas = new DesignerCanvas(surface->contentHost());
    layout->addWidget(m_canvas, 1);
    auto *hint = new QLabel(
        tr("拖动控件标题区域移动 · Ctrl+Z 撤销 · Ctrl+Y 重做 · 预览模式隐藏编辑辅助线"),
        surface->contentHost());
    hint->setObjectName(QStringLiteral("hmiSubtle"));
    layout->addWidget(hint);
    connect(m_canvas, &DesignerCanvas::selectionChanged,
            this, &YsereinHmiWorkspace::updateSelection);
    connect(m_canvas, &DesignerCanvas::controlPropertiesChanged,
            this, &YsereinHmiWorkspace::updateControlInspector);
    connect(m_canvas, &DesignerCanvas::previewNavigationRequested,
            this, &YsereinHmiWorkspace::selectPageById);
    connect(m_canvas, &DesignerCanvas::numericPropertiesChanged,
            this, &YsereinHmiWorkspace::updateNumericInspector);
    connect(m_canvas, &DesignerCanvas::controlDetailsChanged,
            this, &YsereinHmiWorkspace::updateControlDetails);
    connect(m_canvas, &DesignerCanvas::textInputRequested,
            this, &YsereinHmiWorkspace::handleTextInputRequested);
    connect(m_canvas, &DesignerCanvas::statusMessage,
            this, &YsereinHmiWorkspace::setStatus);
    connect(m_canvas, &DesignerCanvas::controlStateChanged,
            this, &YsereinHmiWorkspace::updateSchemaInspector);
    connect(m_canvas, &DesignerCanvas::controlStateChanged,
            this, &YsereinHmiWorkspace::refreshLayerList);
    connect(m_canvas, &DesignerCanvas::selectionChanged,
            this, &YsereinHmiWorkspace::refreshLayerList);
    connect(m_canvas, &DesignerCanvas::documentChanged,
            this, &YsereinHmiWorkspace::refreshLayerList);
    connect(m_canvas, &DesignerCanvas::pageChanged,
            this, &YsereinHmiWorkspace::refreshLayerList);
    connect(m_undoButton, &QPushButton::clicked,
            this, &YsereinHmiWorkspace::undo);
    connect(m_redoButton, &QPushButton::clicked,
            this, &YsereinHmiWorkspace::redo);
    return surface;
}

void YsereinHmiWorkspace::selectPage(const int index)
{
    if (!m_canvas || index < 0 || index >= m_canvas->pageCount()) return;
    if (m_canvas) m_canvas->setPage(index);
    if (m_screenInfo) {
        const QString pageNumber = QStringLiteral("%1").arg(index + 1, 2, 10,
                                                              QLatin1Char('0'));
        m_screenInfo->setText(tr("页面 %1  ·  800 × 480  ·  目标预览")
                                  .arg(pageNumber));
    }
    publishPageContext();
}

void YsereinHmiWorkspace::refreshEventTargets()
{
    if (!m_eventTarget || !m_canvas) return;
    const QString selectedTarget = m_canvas->selectedEventTarget();
    const QSignalBlocker blocker(m_eventTarget);
    m_eventTarget->clear();
    m_eventTarget->addItem(tr("选择目标页面"), QString{});
    const QStringList ids = m_canvas->pageIds();
    const QStringList names = m_canvas->pageNames();
    for (int index = 0; index < ids.size(); ++index) {
        m_eventTarget->addItem(names.value(index), ids.value(index));
    }
    const int selectedIndex = m_eventTarget->findData(selectedTarget);
    m_eventTarget->setCurrentIndex(selectedIndex >= 0 ? selectedIndex : 0);
}

void YsereinHmiWorkspace::selectPageById(const QString &pageId)
{
    if (!m_canvas) return;
    const int index = m_canvas->pageIds().indexOf(pageId);
    if (index < 0) {
        setStatus(tr("预览跳转失败：页面不存在"));
        return;
    }
    if (m_pages && m_pages->currentRow() != index) {
        m_pages->setCurrentRow(index);
    } else {
        selectPage(index);
    }
}

void YsereinHmiWorkspace::updateSelection(const QString &id, const QString &type,
                                           const QString &binding)
{
    m_updatingInspector = true;
    const bool hasSelection = !id.isEmpty();
    if (m_selectedName) m_selectedName->setText(id.isEmpty() ? tr("未选择") : id);
    if (m_selectedType) {
        const QHash<QString, QString> typeNames{
            {QStringLiteral("number"), tr("数值显示")},
            {QStringLiteral("text"), tr("文本")},
            {QStringLiteral("button"), tr("按钮")},
            {QStringLiteral("progress"), tr("进度条")},
            {QStringLiteral("status"), tr("状态灯")},
            {QStringLiteral("switch"), tr("开关")},
            {QStringLiteral("image"), tr("图片")},
            {QStringLiteral("chart"), tr("曲线")},
            {QStringLiteral("text-input"), tr("文本输入")},
        };
        m_selectedType->setText(type.isEmpty() ? QStringLiteral("—")
                                                : typeNames.value(type, type));
    }
    if (m_selectedBinding) {
        m_selectedBinding->setText(binding);
        m_selectedBinding->setEnabled(hasSelection && type != QStringLiteral("text")
            && type != QStringLiteral("button") && type != QStringLiteral("image"));
    }
    if (m_selectionBadge) {
        m_selectionBadge->setText(id.isEmpty() ? tr("未选择") : tr("已选中"));
    }
    const bool numeric = type == QStringLiteral("number");
    if (m_numericProperties) m_numericProperties->hide();
    if (m_genericProperties) m_genericProperties->hide();
    if (m_selectedName) m_selectedName->setEnabled(hasSelection);
    for (QWidget *field : {static_cast<QWidget *>(m_geometryX),
                           static_cast<QWidget *>(m_geometryY),
                           static_cast<QWidget *>(m_geometryWidth),
                           static_cast<QWidget *>(m_geometryHeight),
                           static_cast<QWidget *>(m_visible),
                           static_cast<QWidget *>(m_locked)}) {
        if (field) field->setEnabled(hasSelection);
    }
    if (m_bindingMode) {
        m_bindingMode->setEnabled(hasSelection
            && (type == QStringLiteral("switch") || type == QStringLiteral("text-input")));
    }
    if (m_eventType) m_eventType->setEnabled(hasSelection
        && (type == QStringLiteral("button") || type == QStringLiteral("text-input")));
    if (m_eventProperties) m_eventProperties->setVisible(hasSelection
        && (type == QStringLiteral("button") || type == QStringLiteral("text-input")));
    if (m_eventTarget) m_eventTarget->setEnabled(false);
    if (!numeric) {
        const QSignalBlocker valueBlocker(m_previewValue);
        const QSignalBlocker decimalsBlocker(m_decimals);
        const QSignalBlocker unitBlocker(m_unit);
        const QSignalBlocker fontBlocker(m_fontPixelSize);
        const QSignalBlocker alignmentBlocker(m_alignment);
        m_previewValue->setValue(0.0);
        m_decimals->setValue(1);
        m_unit->clear();
        m_fontPixelSize->setValue(32);
        m_alignment->setCurrentIndex(0);
    }
    if (!hasSelection && m_schemaInspector) m_schemaInspector->clear();
    m_updatingInspector = false;
}

void YsereinHmiWorkspace::updateControlInspector(
    const QString &id, const QString &type, const QString &binding,
    const QString &textValue, const double value, const QString &bindingMode,
    const QString &eventType, const QString &eventTarget,
    const QRectF &geometry, const bool visible, const bool locked)
{
    if (!m_genericProperties) return;
    const bool hasSelection = !id.isEmpty();
    const bool textControl = hasSelection && type != QStringLiteral("number")
        && type != QStringLiteral("image") && type != QStringLiteral("chart");
    const bool progressControl = type == QStringLiteral("progress");
    m_updatingInspector = true;
    const QSignalBlocker textBlocker(m_textValue);
    const QSignalBlocker valueBlocker(m_controlValue);
    const QSignalBlocker bindingModeBlocker(m_bindingMode);
    const QSignalBlocker eventTypeBlocker(m_eventType);
    const QSignalBlocker eventTargetBlocker(m_eventTarget);
    const QSignalBlocker xBlocker(m_geometryX);
    const QSignalBlocker yBlocker(m_geometryY);
    const QSignalBlocker wBlocker(m_geometryWidth);
    const QSignalBlocker hBlocker(m_geometryHeight);
    const QSignalBlocker visibleBlocker(m_visible);
    const QSignalBlocker lockedBlocker(m_locked);
    if (m_selectedName) m_selectedName->setText(id);
    if (m_selectedBinding) m_selectedBinding->setText(binding);
    if (m_bindingMode) m_bindingMode->setCurrentIndex(m_bindingMode->findData(bindingMode));
    if (m_eventType) m_eventType->setCurrentIndex(m_eventType->findData(eventType));
    if (m_eventTarget) {
        const int targetIndex = m_eventTarget->findData(eventTarget);
        m_eventTarget->setCurrentIndex(targetIndex >= 0 ? targetIndex : 0);
        m_eventTarget->setEnabled(eventType == QStringLiteral("gotoPage"));
    }
    if (m_geometryX) m_geometryX->setValue(geometry.x());
    if (m_geometryY) m_geometryY->setValue(geometry.y());
    if (m_geometryWidth) m_geometryWidth->setValue(geometry.width());
    if (m_geometryHeight) m_geometryHeight->setValue(geometry.height());
    if (m_visible) m_visible->setChecked(visible);
    if (m_locked) m_locked->setChecked(locked);
    if (m_textValue) m_textValue->setText(textValue);
    if (m_controlValue) m_controlValue->setValue(std::clamp(value, 0.0, 1.0));
    if (m_textValueLabel) m_textValueLabel->setVisible(textControl);
    if (m_textValue) m_textValue->setVisible(textControl);
    if (m_controlValueLabel) m_controlValueLabel->setVisible(progressControl);
    if (m_controlValue) m_controlValue->setVisible(progressControl);
    const bool stateControl = type == QStringLiteral("status")
        || type == QStringLiteral("switch");
    m_controlValueLabel->setVisible(progressControl || stateControl);
    m_controlValue->setVisible(progressControl || stateControl);
    if (m_genericProperties) m_genericProperties->hide();
    if (auto *form = qobject_cast<QFormLayout *>(m_genericProperties->layout())) {
        form->setRowVisible(m_assetSelector, type == QStringLiteral("image"));
        form->setRowVisible(m_scaleMode, type == QStringLiteral("image"));
        form->setRowVisible(m_inputMaxLength, type == QStringLiteral("text-input"));
        form->setRowVisible(m_chartMin, type == QStringLiteral("chart"));
        form->setRowVisible(m_chartMax, type == QStringLiteral("chart"));
        form->setRowVisible(m_chartWindowLength, type == QStringLiteral("chart"));
        form->setRowVisible(m_chartSamples, type == QStringLiteral("chart"));
    }
    m_updatingInspector = false;
}

void YsereinHmiWorkspace::updateControlDetails(const QJsonObject &details)
{
    if (!m_assetSelector) return;
    m_updatingInspector = true;
    const QSignalBlocker assetBlocker(m_assetSelector);
    const QSignalBlocker scaleBlocker(m_scaleMode);
    const QSignalBlocker lengthBlocker(m_inputMaxLength);
    const QSignalBlocker minBlocker(m_chartMin);
    const QSignalBlocker maxBlocker(m_chartMax);
    const QSignalBlocker windowBlocker(m_chartWindowLength);
    const QSignalBlocker samplesBlocker(m_chartSamples);
    refreshAssetCombo();
    const QString assetId = details.value(QStringLiteral("assetId")).toString();
    const int assetIndex = m_assetSelector->findData(assetId);
    m_assetSelector->setCurrentIndex(assetIndex >= 0 ? assetIndex : 0);
    const int scaleIndex = m_scaleMode->findData(details.value(QStringLiteral("scaleMode")).toString());
    m_scaleMode->setCurrentIndex(scaleIndex >= 0 ? scaleIndex : 0);
    m_inputMaxLength->setValue(details.value(QStringLiteral("maxLength")).toInt(32));
    m_chartMin->setValue(details.value(QStringLiteral("chartMin")).toDouble(0.0));
    m_chartMax->setValue(details.value(QStringLiteral("chartMax")).toDouble(100.0));
    m_chartWindowLength->setValue(details.value(QStringLiteral("chartWindowLength")).toInt(30));
    QStringList samples;
    for (const QJsonValue &sample : details.value(QStringLiteral("chartSamples")).toArray())
        samples.append(QString::number(sample.toDouble(), 'g', 8));
    m_chartSamples->setText(samples.join(QLatin1Char(',')));
    m_updatingInspector = false;
}

void YsereinHmiWorkspace::updateSchemaInspector(const QString &type,
                                                 const QJsonObject &properties)
{
    if (!m_schemaInspector) return;
    if (m_schemaInspectorEditing) return;
    const ControlSchema *schema =
        ControlSchemaRegistry::instance().findByKeyword(schemaKeywordForType(type));
    m_schemaInspector->load(schema, properties,
                            m_canvas ? m_canvas->selectedId() : QString());
}

void YsereinHmiWorkspace::handleTextInputRequested(const QString &instanceId)
{
    Q_UNUSED(instanceId)
    if (!m_canvas || !m_canvas->previewMode()) return;
    bool accepted = false;
    const QString value = QInputDialog::getText(
        this, tr("文本输入预览"), tr("输入值"), QLineEdit::Normal,
        m_canvas->selectedTextValue(), &accepted);
    if (accepted && !m_canvas->setPreviewTextInput(value)) {
        setStatus(tr("文本输入预览失败"));
    }
}

void YsereinHmiWorkspace::updateNumericInspector(
    const QString &id, const QString &binding, const double previewValue,
    const int decimals, const QString &unit, const int fontPixelSize,
    const int alignmentIndex)
{
    m_updatingInspector = true;
    const QSignalBlocker nameBlocker(m_selectedName);
    const QSignalBlocker bindingBlocker(m_selectedBinding);
    const QSignalBlocker valueBlocker(m_previewValue);
    const QSignalBlocker decimalsBlocker(m_decimals);
    const QSignalBlocker unitBlocker(m_unit);
    const QSignalBlocker fontBlocker(m_fontPixelSize);
    const QSignalBlocker alignmentBlocker(m_alignment);
    m_selectedName->setText(id);
    m_selectedBinding->setText(binding);
    m_previewValue->setValue(previewValue);
    m_decimals->setValue(decimals);
    m_unit->setText(unit);
    m_fontPixelSize->setValue(fontPixelSize);
    m_alignment->setCurrentIndex(alignmentIndex);
    m_updatingInspector = false;
}

void YsereinHmiWorkspace::togglePreview()
{
    if (!m_canvas || !m_preview) return;
    const bool preview = m_preview->isChecked();
    m_preview->setProperty("preview", preview);
    m_preview->style()->unpolish(m_preview);
    m_preview->style()->polish(m_preview);
    m_preview->setText(preview ? tr("退出预览") : tr("预览"));
    m_canvas->setPreviewMode(preview);
    if (m_inspectorPanel) m_inspectorPanel->setEnabled(!preview);
}

void YsereinHmiWorkspace::alignSelectedControl()
{
    if (m_canvas) m_canvas->alignSelected();
}

void YsereinHmiWorkspace::setStatus(const QString &message)
{
    if (m_status) m_status->setText(message);
    if (m_host) m_host->setStatus(tr("Y-Serein HMI · %1").arg(message));
}

void YsereinHmiWorkspace::updateInspectorState()
{
    if (m_canvas) m_canvas->refreshSelection();
}

} // namespace serein::scope::yserein_hmi
