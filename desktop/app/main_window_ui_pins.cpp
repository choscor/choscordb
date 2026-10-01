#include "app/main_window_ui_pins.h"

#include "app/main_window_widgets.h"
#include "design_system/text/text.h"
#include "design_system/tree/navigation_tree_view.h"
#include "widgets/sidebar_section/sidebar_section.h"
#include <QCoreApplication>
#include <QPalette>
#include <QVBoxLayout>

namespace choscordb {
PinnedSidebar buildPinnedSidebar(QWidget* parent, QVBoxLayout* layout) {
    const auto label = [](const char* text) {
        return QCoreApplication::translate("MainWindow", text);
    };
    auto* section = new SidebarSection(label("Pinned"), parent);
    section->setObjectName("pinnedSection");
    section->setAccessibleName(label("Pinned shortcuts"));
    section->layout()->setSpacing(design::spacing(design::Spacing::Quarter));
    layout->addWidget(section);
    auto* empty = new design::Text(
        label("No pinned objects yet.\nPin a schema or object from its menu."), section);
    empty->setObjectName("pinnedEmpty");
    empty->setWordWrap(true);
    empty->setForegroundRole(QPalette::PlaceholderText);
    empty->setAlignment(Qt::AlignCenter);
    empty->setMargin(design::spacing(design::Spacing::Three));
    empty->setTextFormat(Qt::PlainText);
    section->contentLayout()->addWidget(empty);
    auto* list = new design::NavigationTreeView(section);
    list->setObjectName("pinnedList");
    list->setAccessibleName(label("Pinned schemas and database objects"));
    list->setAccessibleDescription(label(
        "Use the arrow to browse children; activate a pinned row to reveal its original object"));
    list->setItemDelegate(new main_window_detail::NavigatorIconDelegate(list));
    list->setHeaderHidden(true);
    list->setUniformRowHeights(false);
    list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    list->setContextMenuPolicy(Qt::CustomContextMenu);
    list->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    list->setFixedHeight(design::spacing(design::Spacing::Two));
    section->contentLayout()->addWidget(list);
    empty->hide();
    section->hide();
    return {section, list, empty};
}
} // namespace choscordb
