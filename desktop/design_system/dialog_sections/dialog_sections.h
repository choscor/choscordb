#pragma once

#include <QSet>
#include <QWidget>

class QFormLayout;
class QHBoxLayout;
class QVBoxLayout;
class QString;

namespace choscordb::design {
class Button;
class StatusLine;
struct ResolvedTheme;

enum class DialogActionRole { Primary, Destructive, Dismiss, Utility, DestructiveUtility };
enum class FieldNeed { Required, Optional };

class DialogSections : public QWidget {
    Q_OBJECT
  public:
    explicit DialogSections(QWidget* parent = nullptr);
    QHBoxLayout* headerLayout() const;
    QVBoxLayout* bodyLayout() const;
    Button* addDismissButton(const QString& accessibleName);
    Button* addAction(DialogActionRole role, const QString& text);
    StatusLine* addStatusLine();
    QFormLayout* addForm(QVBoxLayout* parentLayout = nullptr);
    static void configureForm(QFormLayout* form);
    void addFormRow(QFormLayout* form, const QString& label, QWidget* field,
                    FieldNeed need = FieldNeed::Required);
    void relabelFormRow(QFormLayout* form, QWidget* field, const QString& label,
                        FieldNeed need = FieldNeed::Required);

  private:
    void applyCompactSpacing();
    void refreshFooter();
    QVBoxLayout* root_ = nullptr;
    QHBoxLayout* header_ = nullptr;
    QVBoxLayout* body_ = nullptr;
    QWidget* footer_ = nullptr;
    QWidget* footerSeparator_ = nullptr;
    QHBoxLayout* utilities_ = nullptr;
    QHBoxLayout* decisions_ = nullptr;
    QSet<QChar> mnemonics_;
};

}
