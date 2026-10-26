#pragma once

#include <QWidget>

class QHBoxLayout;
class QVBoxLayout;
class QString;

namespace choscordb::design {
class Button;
struct ResolvedTheme;

// Three-part dialog content with compact, separated action bars.
class DialogSections : public QWidget {
    Q_OBJECT
  public:
    explicit DialogSections(QWidget* parent = nullptr);
    QHBoxLayout* headerLayout() const;
    QVBoxLayout* bodyLayout() const;
    // The footer and its separator are hidden while the footer layout is empty.
    QHBoxLayout* footerLayout() const;
    // Ends the header with the shared dismiss: a ghost close icon button.
    Button* addDismissButton(const QString& accessibleName);

  private:
    void applyCompactSpacing();
    QVBoxLayout* root_ = nullptr;
    QHBoxLayout* header_ = nullptr;
    QVBoxLayout* body_ = nullptr;
    QHBoxLayout* footer_ = nullptr;
};

} // namespace choscordb::design
