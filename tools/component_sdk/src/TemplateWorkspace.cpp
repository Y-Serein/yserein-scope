#include "TemplateWorkspace.h"

#include <serein/scope/WorkspaceCanvas.h>

#include <QtWidgets/QLabel>
#include <QtWidgets/QVBoxLayout>

#include <utility>

namespace templateComponent {

TemplateWorkspace::TemplateWorkspace(serein::scope::IHostServices *host,
                                     QWidget *parent)
    : QWidget(parent)
    , m_host(host)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_canvas = new serein::scope::WorkspaceCanvas(
        QStringLiteral("template-component/workspace"), this);
    layout->addWidget(m_canvas);

    auto *label = new QLabel(tr("组件内容"));
    label->setAlignment(Qt::AlignCenter);
    m_canvas->addComponent(QStringLiteral("template-component.content"),
                           tr("示例内容"), label, QRectF(0.12, 0.12, 0.76, 0.76),
                           QSize(240, 160));

    m_uiProvider = new serein::scope::WorkspaceUiProvider(this);
    serein::scope::WorkspaceUiContext context;
    context.id = QStringLiteral("template-component");
    context.componentLibrary.items = {
        {QStringLiteral("template-component.content"), tr("示例内容"), {}}
    };
    context.componentLibrary.activationTarget = this;
    context.componentLibrary.activationMethod =
        QByteArrayLiteral("activateLibraryComponent");
    m_uiProvider->setContext(std::move(context));
}

void TemplateWorkspace::shutdown()
{
    // Stop timers, transport leases and background jobs here before unload.
    m_host = nullptr;
}

serein::scope::WorkspaceUiProvider *TemplateWorkspace::workspaceUiProvider()
{
    return m_uiProvider;
}

bool TemplateWorkspace::activateLibraryComponent(const QString &id)
{
    if (id != QStringLiteral("template-component.content")) return false;
    m_canvas->setComponentVisible(id, true);
    return true;
}

} // namespace templateComponent
