#include "design_system/dialog_sections/dialog_sections.h"
#include "design_system/button/button.h"
#include "design_system/icons.h"
#include "design_system/status_line/status_line.h"
#include "design_system/theme.h"

#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QSet>
#include <QVBoxLayout>

namespace choscordb::design {
namespace {
QString withMnemonic(const QString& label, QSet<QChar>& used) {
    for (qsizetype index = 0; index < label.size(); ++index) {
        const auto letter = label.at(index).toLower();
        if (!letter.isLetter() || used.contains(letter))
            continue;
        used.insert(letter);
        auto marked = label;
        marked.insert(index, QLatin1Char('&'));
        return marked;
    }
    return label;
}
}

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
    footerSeparator_ = footerSeparator;
    root_->addWidget(footerSeparator);
    footer_ = new QWidget(this);
    footer_->setObjectName(QStringLiteral("dialogSectionsFooter"));
    auto* footer = new QHBoxLayout(footer_);
    utilities_ = new QHBoxLayout;
    decisions_ = new QHBoxLayout;
    footer->addLayout(utilities_);
    footer->addStretch();
    footer->addLayout(decisions_);
    root_->addWidget(footer_);
    footerSeparator->hide();
    footer_->hide();
    applyCompactSpacing();
}

QHBoxLayout* DialogSections::headerLayout() const {
    return header_;
}
QVBoxLayout* DialogSections::bodyLayout() const {
    return body_;
}

Button* DialogSections::addDismissButton(const QString& accessibleName) {
    header_->addStretch();
    auto* dismiss = createDismissButton(accessibleName, header_->parentWidget());
    dismiss->setFocusPolicy(Qt::ClickFocus);
    header_->addWidget(dismiss);
    return dismiss;
}

Button* DialogSections::addAction(DialogActionRole role, const QString& text) {
    auto* button = new Button(text, footer_);
    button->setAutoDefault(false);
    switch (role) {
    case DialogActionRole::Primary:
        button->setVariant(ButtonVariant::Default);
        decisions_->addWidget(button);
        break;
    case DialogActionRole::Destructive:
        button->setVariant(ButtonVariant::Destructive);
        decisions_->addWidget(button);
        break;
    case DialogActionRole::Dismiss:
        button->setVariant(ButtonVariant::Outline);
        decisions_->insertWidget(0, button);
        break;
    case DialogActionRole::Utility:
        button->setVariant(ButtonVariant::Secondary);
        utilities_->addWidget(button);
        break;
    case DialogActionRole::DestructiveUtility:
        button->setVariant(ButtonVariant::Destructive);
        utilities_->insertWidget(0, button);
        break;
    }
    button->setProperty("dialogActionRole", int(role));
    refreshFooter();
    return button;
}

void DialogSections::refreshFooter() {
    QList<Button*> actions;
    for (auto* child : footer_->findChildren<Button*>(Qt::FindDirectChildrenOnly))
        if (child->property("dialogActionRole").isValid())
            actions.append(child);
    const bool used = !actions.isEmpty();
    footer_->setVisible(used);
    footerSeparator_->setVisible(used);
    Button* primary = nullptr;
    Button* dismiss = nullptr;
    bool destructive = false;
    for (auto* action : actions) {
        const auto role = DialogActionRole(action->property("dialogActionRole").toInt());
        if (role == DialogActionRole::Primary)
            primary = action;
        if (role == DialogActionRole::Dismiss)
            dismiss = action;
        if (role == DialogActionRole::Destructive)
            destructive = true;
        action->setDefault(false);
    }
    if (primary)
        primary->setDefault(true);
    else if (destructive && dismiss)
        dismiss->setDefault(true);
}

StatusLine* DialogSections::addStatusLine() {
    auto* status = new StatusLine(this);
    status->hide();
    bool contentExpands = false;
    for (int index = 0; index < body_->count(); ++index)
        contentExpands =
            contentExpands || body_->itemAt(index)->expandingDirections().testFlag(Qt::Vertical);
    if (!contentExpands)
        body_->addStretch();
    body_->addWidget(status);
    return status;
}

QFormLayout* DialogSections::addForm(QVBoxLayout* parentLayout) {
    auto* form = new QFormLayout;
    configureForm(form);
    (parentLayout ? parentLayout : body_)->addLayout(form);
    return form;
}

void DialogSections::configureForm(QFormLayout* form) {
    form->setRowWrapPolicy(QFormLayout::DontWrapRows);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setHorizontalSpacing(spacing(Spacing::Three));
    form->setVerticalSpacing(spacing(Spacing::Two));
}

void DialogSections::addFormRow(QFormLayout* form, const QString& label, QWidget* field,
                                FieldNeed need) {
    auto* caption = new QLabel(form->parentWidget());
    caption->setProperty("designRole", "formLabel");
    caption->setMinimumHeight(dimension(Dimension::Control));
    caption->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    caption->setBuddy(field);
    form->addRow(caption, field);
    relabelFormRow(form, field, label, need);
}

void DialogSections::relabelFormRow(QFormLayout* form, QWidget* field, const QString& label,
                                    FieldNeed need) {
    auto* caption = qobject_cast<QLabel*>(form->labelForField(field));
    Q_ASSERT(caption);
    const auto previous = caption->text();
    const auto marker = previous.indexOf(QLatin1Char('&'));
    if (marker >= 0 && marker + 1 < previous.size())
        mnemonics_.remove(previous.at(marker + 1).toLower());
    const auto text = need == FieldNeed::Optional ? tr("%1 (optional)").arg(label) : label;
    caption->setText(withMnemonic(text, mnemonics_));
}

void DialogSections::applyCompactSpacing() {
    const int inset = spacing(Spacing::Six);
    const int gap = spacing(Spacing::Four);
    root_->setContentsMargins(0, 0, 0, 0);
    root_->setSpacing(0);
    body_->setContentsMargins(inset, gap, inset, inset);
    body_->setSpacing(gap);
    header_->setContentsMargins(inset, gap, inset, gap);
    auto* footer = qobject_cast<QHBoxLayout*>(footer_->layout());
    footer->setContentsMargins(inset, gap, inset, gap);
    footer->setSpacing(spacing(Spacing::Two));
    utilities_->setSpacing(spacing(Spacing::Two));
    decisions_->setSpacing(spacing(Spacing::Two));
}

}
