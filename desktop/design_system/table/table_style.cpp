#include "design_system/table/table_style.h"
#include "design_system/style/style_resource.h"
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
        if (index.row() == hoveredRow_)
            rowOption.state |= QStyle::State_MouseOver;
        else
            rowOption.state &= ~QStyle::State_MouseOver;
        QStyledItemDelegate::paint(painter, rowOption, index);
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

void configureResultTable(QTableView& table) {
    table.setShowGrid(true);
    table.setGridStyle(Qt::SolidLine);
    table.setItemDelegate(new RowHoverDelegate(table));
}
} // namespace choscordb::design
