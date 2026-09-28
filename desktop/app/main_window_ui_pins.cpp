#include "app/main_window_ui_pins.h"

#include "design_system/text/text.h"
#include "design_system/theme_manager.h"
#include "widgets/sidebar_section/sidebar_section.h"
#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QListWidget>
#include <QPalette>
#include <QVBoxLayout>
#include <algorithm>

namespace choscordb {
PinnedSidebar buildPinnedSidebar(QWidget* parent, QVBoxLayout* layout,
                                 design::ThemeManager* theme) {
    const auto label = [](const char* text) {
        return QCoreApplication::translate("MainWindow", text);
    };
    auto* section = new SidebarSection(label("Pinned"), parent);
    section->setObjectName("pinnedSection");
    section->setAccessibleName(label("Pinned shortcuts"));
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
    auto* list = new QListWidget(section);
    list->setObjectName("pinnedList");
    list->setAccessibleName(label("Pinned schemas and database objects"));
    list->setAccessibleDescription(label("Activate a shortcut to reveal its original object"));
    list->setProperty("designSurface", "sidebar");
    list->setSpacing(design::spacing(design::Spacing::Half));
    list->setWordWrap(true);
    list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    list->setContextMenuPolicy(Qt::CustomContextMenu);
    list->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    const auto sizeList = [theme, list] {
        const int visibleRows = std::min(list->count(), 4);
        const int rowHeight =
            std::max(theme->metrics().navigationRowHeight, list->sizeHintForRow(0));
        list->setMaximumHeight(visibleRows * (rowHeight + 2 * list->spacing()) +
                               2 * list->frameWidth());
    };
    QObject::connect(theme, &design::ThemeManager::metricsChanged, list, sizeList);
    sizeList();
    section->contentLayout()->addWidget(list);
    const auto updateEmpty = [list, empty, sizeList] {
        const bool isEmpty = list->count() == 0;
        empty->setVisible(isEmpty);
        list->setVisible(!isEmpty);
        sizeList();
    };
    QObject::connect(list->model(), &QAbstractItemModel::rowsInserted, empty, updateEmpty);
    QObject::connect(list->model(), &QAbstractItemModel::rowsRemoved, empty, updateEmpty);
    QObject::connect(list->model(), &QAbstractItemModel::modelReset, empty, updateEmpty);
    QObject::connect(list->model(), &QAbstractItemModel::dataChanged, list, sizeList);
    updateEmpty();
    return {section, list, empty};
}
} // namespace choscordb
