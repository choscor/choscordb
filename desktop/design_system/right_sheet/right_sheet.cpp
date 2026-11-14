#include "design_system/right_sheet/right_sheet.h"

#include "design_system/button/button.h"
#include "design_system/dialog_sections/dialog_sections.h"
#include "design_system/metrics/metrics.h"
#include "design_system/text/text.h"
#include "design_system/theme.h"

#include <QFrame>
#include <QHideEvent>
#include <QPainter>
#include <QScrollArea>
#include <QShowEvent>
#include <QVBoxLayout>

namespace choscordb::design {

RightSheet::RightSheet(QWidget* owner)
    : QDialog(owner), presentation_(*this, DialogPresentation::Placement::RightEdge) {
    presentation_.makeModal();
    setAttribute(Qt::WA_WindowPropagation);
    if (owner)
        setPalette(owner->palette());
    setObjectName(QStringLiteral("rightSheet"));
    setProperty("appDialog", true);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    sections_ = new DialogSections(this);
    root->addWidget(sections_);

    title_ = new Text({}, sections_);
    title_->setObjectName(QStringLiteral("rightSheetTitle"));
    title_->setTextStyle(TextStyle::Headline);
    sections_->headerLayout()->addWidget(title_, 1);
    auto* close = sections_->addDismissButton(tr("Close"));
    close->setObjectName(QStringLiteral("rightSheetClose"));
    connect(close, &QPushButton::clicked, this, &QDialog::reject);

    body_ = new QScrollArea(this);
    body_->setObjectName(QStringLiteral("rightSheetBody"));
    body_->setFrameShape(QFrame::NoFrame);
    body_->setWidgetResizable(true);
    sections_->bodyLayout()->addWidget(body_, 1);
}

RightSheet::~RightSheet() = default;

void RightSheet::setTitle(const QString& title) {
    setWindowTitle(title);
    setAccessibleName(title);
    title_->setText(title);
    body_->setAccessibleName(title);
    if (auto* close = findChild<QPushButton*>(QStringLiteral("rightSheetClose"))) {
        close->setAccessibleName(tr("Close %1").arg(title));
        close->setToolTip(close->accessibleName());
    }
}

void RightSheet::setBody(QWidget* body) {
    if (body)
        body_->setWidget(body);
    else if (auto* previous = body_->takeWidget())
        previous->deleteLater();
}

DialogSections* RightSheet::sections() const {
    return sections_;
}

void RightSheet::open() {
    setResult(0);
    show();
}

void RightSheet::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    const auto colors = resolvedThemeForWidget(*this).colors;
    painter.fillRect(rect(), colors.base);
    painter.setPen(colors.line);
    painter.drawLine(0, 0, 0, height());
}

void RightSheet::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    presentation_.shown();
}

void RightSheet::hideEvent(QHideEvent* event) {
    QDialog::hideEvent(event);
    presentation_.hidden();
}
}
