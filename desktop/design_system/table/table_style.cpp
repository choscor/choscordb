#include "design_system/table/table_style.h"
#include "design_system/style/style_resource.h"
#include <QApplication>
#include <QPointer>
#include <QStyledItemDelegate>
#include <QTableView>

namespace {
class RowHoverDelegate final : public QStyledItemDelegate {
  public:
    explicit RowHoverDelegate(QTableView& table) : QStyledItemDelegate(&table), table_(&table) {
        table.setMouseTracking(true);
        QObject::connect(&table, &QTableView::entered, this, [this](const QModelIndex& index) {
            if (hoveredRow_ == index.row())
                return;
            hoveredRow_ = index.row();
            table_->viewport()->update();
        });
        QObject::connect(&table, &QTableView::viewportEntered, this, [this] {
            hoveredRow_ = -1;
            table_->viewport()->update();
        });
    }
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        QStyleOptionViewItem rowOption(option);
        initStyleOption(&rowOption, index);
        if (index.row() == hoveredRow_)
            rowOption.backgroundBrush = rowOption.palette.brush(QPalette::Highlight);
        // Qt 6.8 switches a hovered stylesheet item to a Windows-style painter,
        // which changes the native macOS text geometry. Paint the hover through
        // BackgroundRole instead and keep one painter for every item state.
        rowOption.state &= ~QStyle::State_MouseOver;
        auto* style = rowOption.widget ? rowOption.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &rowOption, painter, rowOption.widget);
    }

  private:
    QPointer<QTableView> table_;
    int hoveredRow_ = -1;
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
    table.setItemDelegate(new RowHoverDelegate(table));
}
} // namespace choscordb::design
