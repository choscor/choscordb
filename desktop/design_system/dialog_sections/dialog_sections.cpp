#include "design_system/dialog_sections/dialog_sections.h"
#include "design_system/button/button.h"
#include "design_system/icons.h"
#include "design_system/theme.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QPointer>
#include <QVBoxLayout>

namespace choscordb::design {
namespace {
// Shows the footer bar and its separator only while the footer holds items.
class FooterLayout final : public QHBoxLayout {
  public:
    FooterLayout(QWidget* footer, QWidget* separator)
        : QHBoxLayout(footer), separator_(separator) {}
    void invalidate() override {
        QHBoxLayout::invalidate();
        // Showing the footer activates this layout again; the isHidden() guards end that.
        const bool used = count() > 0;
        if (parentWidget()->isHidden() == used)
            parentWidget()->setVisible(used);
        if (separator_ && separator_->isHidden() == used)
            separator_->setVisible(used);
    }

  private:
    QPointer<QWidget> separator_;
};
} // namespace

DialogSections::DialogSections(QWidget* parent) : QWidget(parent) {
    root_ = new QVBoxLayout(this);
    auto* header = new QWidget(this);
    header_ = new QHBoxLayout(header);
    root_->addWidget(header);
    auto* headerSeparator = new QFrame(this);
    headerSeparator->setFrameShape(QFrame::HLine);
    root_->addWidget(headerSeparator);
    auto* body = new QWidget(this);
    body_ = new QVBoxLayout(body);
    root_->addWidget(body, 1);
    auto* footerSeparator = new QFrame(this);
    footerSeparator->setObjectName(QStringLiteral("dialogSectionsFooterSeparator"));
    footerSeparator->setFrameShape(QFrame::HLine);
    root_->addWidget(footerSeparator);
    auto* footer = new QWidget(this);
    footer->setObjectName(QStringLiteral("dialogSectionsFooter"));
    footer_ = new FooterLayout(footer, footerSeparator);
    root_->addWidget(footer);
    footerSeparator->hide();
    footer->hide();
    applyCompactSpacing();
}

QHBoxLayout* DialogSections::headerLayout() const {
    return header_;
}
QVBoxLayout* DialogSections::bodyLayout() const {
    return body_;
}
QHBoxLayout* DialogSections::footerLayout() const {
    return footer_;
}

Button* DialogSections::addDismissButton(const QString& accessibleName) {
    header_->addStretch();
    auto* dismiss = createDismissButton(accessibleName, header_->parentWidget());
    // Escape dismisses from the keyboard. Kept out of the tab chain and auto-default,
    // the X never takes initial focus or Enter from the dialog's content.
    dismiss->setFocusPolicy(Qt::ClickFocus);
    header_->addWidget(dismiss);
    return dismiss;
}

void DialogSections::applyCompactSpacing() {
    const int horizontalInset = spacing(Spacing::Three);
    root_->setContentsMargins(0, 0, 0, 0);
    root_->setSpacing(0);
    body_->setContentsMargins(horizontalInset, spacing(Spacing::Two), horizontalInset,
                              spacing(Spacing::Two));
    body_->setSpacing(0);
    header_->setContentsMargins(horizontalInset, spacing(Spacing::One), spacing(Spacing::Two),
                                spacing(Spacing::One));
    footer_->setContentsMargins(horizontalInset, spacing(Spacing::Three), horizontalInset,
                                spacing(Spacing::Three));
}

} // namespace choscordb::design
