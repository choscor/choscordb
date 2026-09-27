#pragma once

#include <QWidget>

class QHBoxLayout;
class QVBoxLayout;
class QString;

namespace choscordb::design {
struct ResolvedTheme;

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

QString dialogSectionsApplicationStyleSheet(const ResolvedTheme& theme);

} // namespace choscordb::design
