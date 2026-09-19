#include "design_system/dock/dock_style.h"
#include "design_system/icons.h"
#include "design_system/style/style_resource.h"
#include "design_system/theme.h"

#include <QDockWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>

namespace choscordb::design {

QString dockApplicationStyleSheet() {
    return loadStyleSheet(QStringLiteral("dock/title.qss"));
}

void styleDockWidget(QDockWidget& dock, const ResolvedTheme& theme) {
    auto* titleBar = new QWidget(&dock);
    auto* row = new QHBoxLayout(titleBar);
    row->setContentsMargins(5, 0, 5, 0);
    row->setSpacing(4);
    auto* title = new QLabel(dock.windowTitle(), titleBar);
    title->setObjectName(QStringLiteral("dockTitleLabel"));
    QObject::connect(&dock, &QWidget::windowTitleChanged, title, &QLabel::setText);
    row->addWidget(title, 1);
    const auto addButton = [&](Icon icon, const QString& name, const QString& tooltip) {
        auto* button = new QToolButton(titleBar);
        button->setObjectName(name);
        button->setProperty("iconOnly", true);
        button->setToolTip(tooltip);
        button->setIcon(themedIcon(icon, theme.colors.text, 14));
        button->setIconSize(QSize(14, 14));
        button->setFixedSize(30, 30);
        row->addWidget(button);
        return button;
    };
    auto* floatButton =
        addButton(Icon::Square, QStringLiteral("dockFloatButton"), QStringLiteral("Float dock"));
    QObject::connect(floatButton, &QToolButton::clicked, &dock,
                     [&dock] { dock.setFloating(!dock.isFloating()); });
    auto* closeButton =
        addButton(Icon::Close, QStringLiteral("dockCloseButton"), QStringLiteral("Close dock"));
    QObject::connect(closeButton, &QToolButton::clicked, &dock, &QDockWidget::close);
    titleBar->setObjectName(QStringLiteral("dockTitleBar"));
    titleBar->setFixedHeight(34);
    dock.setTitleBarWidget(titleBar);
    dock.setStyleSheet(loadStyleSheet(QStringLiteral("dock/widget.qss"))
                           .arg(theme.colors.surface.name(), theme.colors.text.name()));
}
} // namespace choscordb::design
