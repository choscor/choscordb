#pragma once

#include <QWidget>

class QHBoxLayout;
class QVBoxLayout;

namespace choscordb::design {

// Three-part dialog content with compact, separated action bars.
class DialogSections : public QWidget {
    Q_OBJECT
  public:
    explicit DialogSections(QWidget* parent = nullptr);
    QHBoxLayout* headerLayout() const;
    QVBoxLayout* bodyLayout() const;
    QHBoxLayout* footerLayout() const;
    void applyCompactSpacing();

  private:
    QVBoxLayout* root_ = nullptr;
    QHBoxLayout* header_ = nullptr;
    QVBoxLayout* body_ = nullptr;
    QHBoxLayout* footer_ = nullptr;
};

} // namespace choscordb::design
