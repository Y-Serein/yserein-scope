#include "PluginCard.h"

#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QVBoxLayout>

namespace serein::scope::app {

PluginCard::PluginCard(QString installId, PluginMetadata metadata,
                       const bool loaded, const bool workspaceOpen,
                       const bool managementActions, QWidget *parent)
    : QFrame(parent), m_installId(std::move(installId)),
      m_metadata(std::move(metadata))
{
    setObjectName(QStringLiteral("pluginCard"));
    setMinimumHeight(182);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(8);

    auto *accent = new QFrame;
    accent->setObjectName(QStringLiteral("cardRule"));
    accent->setFixedHeight(1);
    layout->addWidget(accent);

    auto *heading = new QHBoxLayout;
    auto *kicker = new QLabel(m_metadata.category.toUpper()
                              + QStringLiteral("  ·  v") + m_metadata.version);
    kicker->setObjectName(QStringLiteral("cardKicker"));
    auto *state = new QLabel(loaded ? tr("已加载") : tr("已安装 · 未加载"));
    state->setObjectName(QStringLiteral("statusPill"));
    state->setProperty("state", loaded ? QStringLiteral("connected")
                                        : QStringLiteral("installed"));
    heading->addWidget(kicker);
    heading->addStretch();
    heading->addWidget(state);
    auto *title = new QLabel(m_metadata.name);
    title->setObjectName(QStringLiteral("cardTitle"));
    auto *description = new QLabel(m_metadata.description);
    description->setObjectName(QStringLiteral("cardDescription"));
    description->setWordWrap(true);
    layout->addLayout(heading);
    layout->addWidget(title);
    layout->addWidget(description);
    layout->addStretch();

    auto *chips = new QHBoxLayout;
    chips->setSpacing(6);
    for (const QString &capability : m_metadata.capabilities.mid(0, 4)) {
        auto *chip = new QLabel(capability);
        chip->setObjectName(QStringLiteral("chip"));
        chips->addWidget(chip);
    }
    chips->addStretch();
    layout->addLayout(chips);

    // Keep capability chips and actions on separate rows. The old single
    // row made a card's minimum width equal to the sum of every chip and
    // button, so the right side was clipped as soon as a side dock opened.
    auto *actions = new QHBoxLayout;
    actions->setSpacing(6);
    auto *primary = new QPushButton(loaded
        ? (workspaceOpen ? tr("进入工作区") : tr("打开工作区"))
        : tr("加载插件"));
    primary->setProperty("kind", QStringLiteral("primary"));
    connect(primary, &QPushButton::clicked, this,
            [this] { emit primaryRequested(m_installId); });
    actions->addStretch();
    actions->addWidget(primary);
    if (managementActions && loaded) {
        auto *unload = new QPushButton(tr("卸载"));
        unload->setProperty("kind", QStringLiteral("warning"));
        connect(unload, &QPushButton::clicked, this,
                [this] { emit unloadRequested(m_installId); });
        actions->addWidget(unload);
    } else if (managementActions) {
        auto *remove = new QPushButton(tr("移除"));
        remove->setProperty("kind", QStringLiteral("quiet"));
        connect(remove, &QPushButton::clicked, this,
                [this] { emit removeRequested(m_installId); });
        actions->addWidget(remove);
    }
    layout->addLayout(actions);
}

} // namespace serein::scope::app
