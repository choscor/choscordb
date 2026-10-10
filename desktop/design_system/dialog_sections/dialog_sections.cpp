#include "design_system/dialog_sections/dialog_sections.h"
#include "design_system/theme.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace choscordb::design {

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
    footerSeparator->setFrameShape(QFrame::HLine);
    root_->addWidget(footerSeparator);
    auto* footer = new QWidget(this);
    footer->setObjectName(QStringLiteral("dialogSectionsFooter"));
    footer_ = new QHBoxLayout(footer);
    root_->addWidget(footer);
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
