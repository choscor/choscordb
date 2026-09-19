#include "design_system/table/table_style.h"
#include <QPointer>
#include <QStyledItemDelegate>
#include <QTableView>

namespace {
class RowHoverDelegate final : public QStyledItemDelegate {
  public:
    explicit RowHoverDelegate(QTableView& table) : QStyledItemDelegate(&table), table_(&table) {
        table.setMouseTracking(true);
        QObject::connect(&table, &QTableView::entered, this, [this](const QModelIndex& index) {
            if (hoveredRow_ == index.row()) return;
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
}

namespace choscordb::design {
QString tableStyleSheet() {
    return QStringLiteral(R"(QTableView { font-size: 12px; }
QTableView QHeaderView { background: @muted; }
QTableView QHeaderView::section:horizontal { border-right: 1px solid @border; }
QTableView QHeaderView:vertical { border-right: 1px solid @border; }
QTableView QHeaderView::section:vertical { border-right: 1px solid @border; }
QTableView QTableCornerButton::section { background: @muted; border-right: 1px solid @border; }
)");
}

QString tableItemStyleSheet() {
    return QStringLiteral(R"(QTableView::item { padding: 0 12px; }
)");
}

void configureResultTable(QTableView& table) {
    table.setShowGrid(true);
    table.setGridStyle(Qt::SolidLine);
    table.setItemDelegate(new RowHoverDelegate(table));
}
} // namespace choscordb::design
