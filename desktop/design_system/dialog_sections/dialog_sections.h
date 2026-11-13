#pragma once

#include <QWidget>

class QHBoxLayout;
class QVBoxLayout;
class QString;

namespace choscordb::design {
class Button;
struct ResolvedTheme;

class DialogSections : public QWidget {
    Q_OBJECT
  public:
    explicit DialogSections(QWidget* parent = nullptr);
    QHBoxLayout* headerLayout() const;
    QVBoxLayout* bodyLayout() const;
    QHBoxLayout* footerLayout() const;
    Button* addDismissButton(const QString& accessibleName);

  private:
    void applyCompactSpacing();
    QVBoxLayout* root_ = nullptr;
    QHBoxLayout* header_ = nullptr;
    QVBoxLayout* body_ = nullptr;
    QHBoxLayout* footer_ = nullptr;
};

}
