#pragma once

#include <QWidget>

class QHBoxLayout;

namespace choscordb::design {

// The shared bottom line for workspace results and object panes.
class StatusLine final : public QWidget {
    Q_OBJECT
  public:
    explicit StatusLine(QWidget* parent = nullptr);
    QHBoxLayout* contentLayout() const;
    void setAvailable(bool available);

  protected:
    void changeEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

  private:
    void refreshAppearance();
    QHBoxLayout* content_ = nullptr;
    bool available_ = false;
    bool refreshing_ = false;
};

} // namespace choscordb::design
