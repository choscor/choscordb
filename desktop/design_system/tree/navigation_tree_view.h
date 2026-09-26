#pragma once

#include <QTreeView>

namespace choscordb::design {

class NavigationTreeView final : public QTreeView {
  public:
    explicit NavigationTreeView(QWidget* parent = nullptr);

  protected:
    void drawRow(QPainter* painter, const QStyleOptionViewItem& option,
                 const QModelIndex& index) const override;
};

} // namespace choscordb::design
