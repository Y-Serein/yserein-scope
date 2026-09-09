#include "YsereinHmiWorkspace.h"

#include "DesignerCanvas.h"

#include <serein/scope/WorkspaceCanvas.h>

#include <QtCore/QSignalBlocker>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QFileInfo>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QInputDialog>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QCheckBox>
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
    connect(m_canvas, &DesignerCanvas::documentChanged,
            this, &YsereinHmiWorkspace::markDocumentDirty);

    m_uiProvider = new WorkspaceUiProvider(this);
    WorkspaceUiContext uiContext;
    uiContext.id = QStringLiteral("yserein-hmi/designer/page/%1")
                       .arg(m_canvas->pageIds().value(m_canvas->currentPage()));
    uiContext.layoutProfile = WorkspaceLayoutProfile::Standard;
    uiContext.contextPanel = {m_contextPanel, tr("工程与页面"), nullptr, {}};
    uiContext.inspectorPanel = {m_inspectorPanel, tr("属性与绑定"), nullptr, {}};
    uiContext.outputPanel = {m_outputPanel, tr("HMI 状态"), nullptr, {}};
    uiContext.componentLibrary.items = {
        {QStringLiteral("number"), tr("数值显示"),
         QStringLiteral(":/serein/channels.svg")},
    };
    uiContext.componentLibrary.activationTarget = this;
    uiContext.componentLibrary.activationMethod =
        QByteArrayLiteral("activateLibraryComponent");
    m_uiProvider->setContext(std::move(uiContext));
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
    if (!m_canvas || componentId != QStringLiteral("number")) return false;
    m_canvas->addControl(QStringLiteral("number"), tr("数值显示"));
    return true;
}

void YsereinHmiWorkspace::newProject()
{
    if (!confirmDiscardChanges(tr("新建工程"))) return;
    m_loadingDocument = true;
    m_document = HmiProjectDocument::createDefault();
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
    refreshPageList();
    m_documentDirty = true;
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
    syncDocumentFromCanvas();
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
    syncDocumentFromCanvas();
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

void YsereinHmiWorkspace::syncDocumentFromCanvas()
{
    if (!m_canvas) return;
    const QJsonObject canvasState = m_canvas->pageState();
    const QJsonArray serializedPages = canvasState.value(QStringLiteral("pages")).toArray();
    QJsonObject root = m_document.toJsonObject();
    root.insert(QStringLiteral("pages"), serializedPages);
    root.insert(QStringLiteral("currentPage"),
                canvasState.value(QStringLiteral("currentPage")));
    QString error;
    if (!m_document.setJsonObject(root, &error)) {
        setStatus(tr("工程模型校验失败：%1").arg(error));
        return;
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
    const QJsonObject state{
        {QStringLiteral("currentPage"),
         loaded.toJsonObject().value(QStringLiteral("currentPage")).toInt(0)},
        {QStringLiteral("pages"), pages},
    };
    m_loadingDocument = true;
    const bool restored = m_canvas && m_canvas->restorePageState(state, &error);
    m_loadingDocument = false;
    if (!restored) {
        setStatus(tr("打开工程失败：%1").arg(error));
        return false;
    }
    m_document = std::move(loaded);
    m_documentDirty = false;
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

    layout->addWidget(makeSectionTitle(tr("资源"), panel));
    auto *resourceState = new QLabel(
        tr("资源导入与生成尚未接入；当前不提供模拟入口。"), panel);
    resourceState->setObjectName(QStringLiteral("hmiSubtle"));
    resourceState->setWordWrap(true);
    layout->addWidget(resourceState);

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
    connect(m_preview, &QToolButton::clicked,
            this, &YsereinHmiWorkspace::togglePreview);
    connect(grid, &QCheckBox::toggled,
            m_canvas, &DesignerCanvas::setGridVisible);
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
    root->addWidget(selectionGroup);

    root->addWidget(makeSectionTitle(tr("数值显示"), panel));
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
    root->addWidget(m_numericProperties);

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
            setStatus(tr("已更新数值控件名称"));
        }
    });
    connect(m_selectedBinding, &QLineEdit::editingFinished, this, [this] {
        if (m_updatingInspector || !m_canvas) return;
        if (!m_canvas->setSelectedNumericBinding(m_selectedBinding->text())) {
            setStatus(tr("绑定无效：使用字母、数字、下划线和点号"));
            m_canvas->refreshSelection();
        } else {
            setStatus(tr("已更新数值控件数据绑定"));
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
        tr("P0 离线作者闭环：工程/页面/数值控件/预览/保存已接入；资源、导入、串口、屏端 Runtime 和下载尚未接入。"),
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
    m_screenInfo = new QLabel(tr("页面 01  ·  800 × 480  ·  目标预览"),
                              surface->contentHost());
    m_screenInfo->setObjectName(QStringLiteral("hmiSubtle"));
    canvasMeta->addWidget(m_screenInfo, 0, Qt::AlignRight);
    layout->addLayout(canvasMeta);
    m_canvas = new DesignerCanvas(surface->contentHost());
    layout->addWidget(m_canvas, 1);
    auto *hint = new QLabel(
        tr("拖动控件标题区域移动 · 预览模式隐藏编辑辅助线"),
        surface->contentHost());
    hint->setObjectName(QStringLiteral("hmiSubtle"));
    layout->addWidget(hint);
    connect(m_canvas, &DesignerCanvas::selectionChanged,
            this, &YsereinHmiWorkspace::updateSelection);
    connect(m_canvas, &DesignerCanvas::numericPropertiesChanged,
            this, &YsereinHmiWorkspace::updateNumericInspector);
    connect(m_canvas, &DesignerCanvas::statusMessage,
            this, &YsereinHmiWorkspace::setStatus);
    return surface;
}

void YsereinHmiWorkspace::selectPage(const int index)
{
    if (!m_canvas || index < 0 || index >= m_canvas->pageCount()) return;
    if (m_uiProvider) m_uiProvider->prepareContextChange();
    if (m_canvas) m_canvas->setPage(index);
    if (m_screenInfo) {
        const QString pageNumber = QStringLiteral("%1").arg(index + 1, 2, 10,
                                                              QLatin1Char('0'));
        m_screenInfo->setText(tr("页面 %1  ·  800 × 480  ·  目标预览")
                                  .arg(pageNumber));
    }
    publishPageContext();
}

void YsereinHmiWorkspace::updateSelection(const QString &id, const QString &type,
                                           const QString &binding)
{
    m_updatingInspector = true;
    if (m_selectedName) m_selectedName->setText(id.isEmpty() ? tr("未选择") : id);
    if (m_selectedType) {
        m_selectedType->setText(type == QStringLiteral("number")
            ? tr("数值显示") : type.isEmpty() ? QStringLiteral("—") : type);
    }
    if (m_selectedBinding) {
        m_selectedBinding->setText(binding.isEmpty() ? QStringLiteral("—") : binding);
    }
    if (m_selectionBadge) {
        m_selectionBadge->setText(id.isEmpty() ? tr("未选择") : tr("已选中"));
    }
    const bool numeric = type == QStringLiteral("number");
    if (m_numericProperties) m_numericProperties->setEnabled(numeric);
    if (m_selectedName) m_selectedName->setEnabled(numeric);
    if (m_selectedBinding) m_selectedBinding->setEnabled(numeric);
    if (!numeric) {
        const QSignalBlocker nameBlocker(m_selectedName);
        const QSignalBlocker bindingBlocker(m_selectedBinding);
        const QSignalBlocker valueBlocker(m_previewValue);
        const QSignalBlocker decimalsBlocker(m_decimals);
        const QSignalBlocker unitBlocker(m_unit);
        const QSignalBlocker fontBlocker(m_fontPixelSize);
        const QSignalBlocker alignmentBlocker(m_alignment);
        m_selectedName->clear();
        m_selectedBinding->clear();
        m_previewValue->setValue(0.0);
        m_decimals->setValue(1);
        m_unit->clear();
        m_fontPixelSize->setValue(32);
        m_alignment->setCurrentIndex(0);
    }
    m_updatingInspector = false;
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
