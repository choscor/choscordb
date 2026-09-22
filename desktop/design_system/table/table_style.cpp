#include "design_system/table/table_style.h"
#include "design_system/style/style_resource.h"
#include <QApplication>
#include <QStyledItemDelegate>
#include <QTableView>

namespace {
class ResultTableDelegate final : public QStyledItemDelegate {
  public:
    explicit ResultTableDelegate(QTableView& table) : QStyledItemDelegate(&table) {}
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        QStyleOptionViewItem rowOption(option);
        initStyleOption(&rowOption, index);
        // Qt 6.8 switches a hovered stylesheet item to a Windows-style painter,
        // which changes the native macOS text geometry. Suppress hover styling
        // to preserve both the row background and text geometry.
        rowOption.state &= ~QStyle::State_MouseOver;
        auto* style = rowOption.widget ? rowOption.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &rowOption, painter, rowOption.widget);
    }
};
} // namespace

namespace choscordb::design {
QString tableStyleSheet() {
    return loadStyleSheet(QStringLiteral("table/table_style_sheet.qss"));
}

QString tableItemStyleSheet() {
    return loadStyleSheet(QStringLiteral("table/table_item_style_sheet.qss"));
}

void configureResultTable(QTableView& table, bool showGrid) {
    table.setShowGrid(showGrid);
    table.setGridStyle(Qt::SolidLine);
    table.setItemDelegate(new ResultTableDelegate(table));
}
} // namespace choscordb::design
