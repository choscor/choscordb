#include "design_system/right_sheet/right_sheet.h"

#include "design_system/button/button.h"
#include "design_system/metrics/metrics.h"
#include "design_system/text/text.h"
#include "design_system/theme.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QPainter>
#include <QScrollArea>
#include <QShowEvent>
#include <QVBoxLayout>

namespace choscordb::design {
namespace {
class FooterSurface final : public QWidget {
  public:
    using QWidget::QWidget;

  protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), resolvedThemeForWidget(*this).colors.muted);
    }
};
} // namespace

RightSheet::RightSheet(QWidget* owner)
    : QDialog(owner), presentation_(*this, DialogPresentation::Placement::RightEdge) {
    presentation_.makeModal();
    setAttribute(Qt::WA_WindowPropagation);
    if (owner)
        setPalette(owner->palette());
    setObjectName(QStringLiteral("rightSheet"));
    setAccessibleName(tr("Right sheet"));
    setProperty("appDialog", true);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* header = new QWidget(this);
    header->setObjectName(QStringLiteral("rightSheetHeader"));
    auto* heading = new QHBoxLayout(header);
    heading->setContentsMargins(spacing(Spacing::Four), spacing(Spacing::Two),
                                spacing(Spacing::Two), spacing(Spacing::Two));
    title_ = new Text({}, header);
    title_->setObjectName(QStringLiteral("rightSheetTitle"));
    title_->setAccessibleName(tr("Sheet title"));
    title_->setTypographyRole(TypographyRole::DialogTitle);
    heading->addWidget(title_, 1);
    auto* close = new Button({}, header);
    close->setObjectName(QStringLiteral("rightSheetClose"));
    close->setAccessibleName(tr("Close sheet"));
    close->setToolTip(tr("Close sheet"));
    close->setDesignIcon(Icon::Close);
    close->setVariant(ButtonVariant::Ghost);
    close->setButtonSize(ButtonSize::Icon);
    heading->addWidget(close);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    root->addWidget(header);

    auto* divider = new QFrame(this);
    divider->setFrameShape(QFrame::HLine);
    root->addWidget(divider);

    body_ = new QScrollArea(this);
    body_->setObjectName(QStringLiteral("rightSheetBody"));
    body_->setAccessibleName(tr("Sheet content"));
    body_->setFrameShape(QFrame::NoFrame);
    body_->setWidgetResizable(true);
    root->addWidget(body_, 1);

    auto* footerSurface = new FooterSurface(this);
    footerSurface->setObjectName(QStringLiteral("rightSheetFooter"));
    footerSurface->setAccessibleName(tr("Sheet actions"));
    footer_ = new QHBoxLayout(footerSurface);
    footer_->setContentsMargins(spacing(Spacing::Four), spacing(Spacing::Three),
                                spacing(Spacing::Four), spacing(Spacing::Three));
    footer_->addStretch();
    root->addWidget(footerSurface);
}

RightSheet::~RightSheet() = default;

void RightSheet::setTitle(const QString& title) {
    setWindowTitle(title);
    setAccessibleName(title);
    title_->setText(title);
}

void RightSheet::setBody(QWidget* body) {
    if (body)
        body_->setWidget(body);
    else if (auto* previous = body_->takeWidget())
        previous->deleteLater();
}

QHBoxLayout* RightSheet::footerLayout() const {
    return footer_;
}

void RightSheet::open() {
    setResult(0);
    show();
}

void RightSheet::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    const auto colors = resolvedThemeForWidget(*this).colors;
    painter.fillRect(rect(), colors.popover);
    painter.setPen(colors.border);
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
} // namespace choscordb::design
