#pragma once

#include <QPersistentModelIndex>
#include <QTreeView>

namespace choscordb::design {

class NavigationTreeView final : public QTreeView {
  public:
    explicit NavigationTreeView(QWidget* parent = nullptr);

  protected:
    void mouseMoveEvent(QMouseEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;
    bool viewportEvent(QEvent* event) override;
    void drawRow(QPainter* painter, const QStyleOptionViewItem& option,
                 const QModelIndex& index) const override;

  private:
    void updateHoveredIndex();
    QPoint hoveredPosition_{-1, -1};
    QPersistentModelIndex hoveredIndex_;
};

} // namespace choscordb::design
