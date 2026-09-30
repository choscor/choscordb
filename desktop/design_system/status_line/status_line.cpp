#include "design_system/status_line/status_line.h"

#include "design_system/dialog_shell/dialog_shell.h"
#include "design_system/fonts/fonts.h"
#include "design_system/icons.h"
#include "design_system/metrics/metrics.h"
#include "design_system/theme.h"

#include <QAccessible>
#include <QDialogButtonBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPalette>
#include <QPlainTextEdit>
#include <QResizeEvent>
#include <QToolButton>
#include <QVBoxLayout>

namespace choscordb::design {

StatusLine::StatusLine(QWidget* parent) : QWidget(parent) {
    const auto metrics = resolveMetrics(Density::Compact, true);
    setFont(resolveTypography(TypographyRole::Ui));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    content_ = new QHBoxLayout(this);
    content_->setContentsMargins(metrics.spacingMedium, metrics.spacingSmall, metrics.spacingMedium,
                                 metrics.spacingSmall);
    content_->setSpacing(metrics.spacingMedium);
    loading_ = new QLabel(this);
    loading_->setObjectName("statusLoadingIcon");
    loading_->setAccessibleName(tr("Loading"));
    loading_->hide();
    content_->addWidget(loading_);
    message_ = new QLabel(this);
    message_->setObjectName("statusMessage");
    message_->setTextFormat(Qt::PlainText);
    message_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    message_->setWordWrap(true);
    message_->setMinimumWidth(0);
    message_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    message_->setMaximumHeight(metrics.dataRowHeight * 3);
    message_->hide();
    content_->addWidget(message_, 1);
    details_ = new QToolButton(this);
    details_->setObjectName("statusDetails");
    details_->setText(tr("Details…"));
    details_->setAccessibleName(tr("Read full status details"));
    details_->hide();
    content_->addWidget(details_);
    connect(details_, &QToolButton::clicked, this, &StatusLine::openDetails);
    setAutoFillBackground(true);
    refreshAppearance();
}

QHBoxLayout* StatusLine::contentLayout() const {
    return content_;
}

void StatusLine::setAvailable(bool available) {
    if (available_ == available && !neutral_)
        return;
    neutral_ = false;
    available_ = available;
    refreshAppearance();
}

void StatusLine::setNeutral(bool neutral) {
    neutral_ = neutral;
    refreshAppearance();
}
void StatusLine::setMessage(const QString& message) {
    if (fullMessage_ == message)
        return;
    fullMessage_ = message;
    // Diagnostics remain complete in Details and accessibility. Keep the local
    // summary compact even when backend text contains many lines or long paths.
    constexpr qsizetype SummaryCharacters = 240;
    const auto summary = message.simplified();
    const bool shortened =
        message.size() > SummaryCharacters || message.contains(QLatin1Char('\n'));
    auto length = qMin(SummaryCharacters, summary.size());
    if (length > 0 && length < summary.size() && summary.at(length - 1).isHighSurrogate())
        --length;
    message_->setText(shortened ? summary.left(length) + QStringLiteral("…") : message);
    message_->setVisible(!message.isEmpty());
    details_->setVisible(shortened);
    refreshDetailsVisibility();
    setAccessibleDescription(message);
    QAccessibleEvent changed(this, QAccessible::DescriptionChanged);
    QAccessible::updateAccessibility(&changed);
}
void StatusLine::refreshDetailsVisibility() {
    if (fullMessage_.isEmpty()) {
        details_->hide();
        return;
    }
    const bool shortened = fullMessage_.size() > 240 || fullMessage_.contains(QLatin1Char('\n'));
    // At narrow widths even a short diagnostic can exceed the compact footer.
    // Details must remain available whenever the wrapping label cannot show it all.
    const bool clipped =
        message_->heightForWidth(qMax(1, message_->width())) > message_->maximumHeight();
    details_->setVisible(shortened || clipped);
}
void StatusLine::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    refreshDetailsVisibility();
}
void StatusLine::openDetails() {
    if (detailsDialog_) {
        if (detailsText_ && detailsText_->toPlainText() != fullMessage_)
            detailsText_->setPlainText(fullMessage_);
        detailsDialog_->raise();
        detailsDialog_->activateWindow();
        return;
    }
    auto* dialog = new DialogShell(this);
    dialog->setObjectName("statusDetailsDialog");
    dialog->setWindowTitle(tr("Status details"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->resize(dialogInitialSize(DialogSize::Detail));
    auto* layout = new QVBoxLayout(dialog);
    auto* text = new QPlainTextEdit(dialog);
    text->setObjectName("statusDetailsText");
    text->setAccessibleName(tr("Full status details"));
    text->setReadOnly(true);
    text->setPlainText(fullMessage_);
    layout->addWidget(text);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    detailsDialog_ = dialog;
    detailsText_ = text;
    connect(dialog, &QDialog::finished, this, [this] {
        detailsDialog_.clear();
        detailsText_.clear();
    });
    dialog->setAppModal();
    dialog->open();
}
void StatusLine::setBusy(bool busy) {
    setProperty("busy", busy);
    loading_->setVisible(busy);
    refreshAppearance();
}

void StatusLine::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::ApplicationPaletteChange ||
        event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange ||
        event->type() == QEvent::ParentChange)
        refreshAppearance();
}

void StatusLine::paintEvent(QPaintEvent*) {
    const auto colors = resolvedThemeForWidget(*this).colors;
    QPainter painter(this);
    const bool neutral = neutral_ || property("busy").toBool();
    painter.fillRect(rect(), neutral      ? colors.muted
                             : available_ ? colors.successSurface
                                          : colors.dangerSurface);
}

void StatusLine::refreshAppearance() {
    if (refreshing_)
        return;
    refreshing_ = true;
    const auto colors = resolvedThemeForWidget(*this).colors;
    auto colorsForLine = palette();
    const bool neutral = neutral_ || property("busy").toBool();
    colorsForLine.setColor(QPalette::Window, neutral      ? colors.muted
                                             : available_ ? colors.successSurface
                                                          : colors.dangerSurface);
    colorsForLine.setColor(QPalette::WindowText, neutral      ? colors.text
                                                 : available_ ? colors.success
                                                              : colors.danger);
    setPalette(colorsForLine);
    const auto metrics = resolveMetrics(Density::Compact, true);
    message_->setMaximumHeight(metrics.dataRowHeight * 3);
    loading_->setPixmap(themedIcon(Icon::Loader, colors.text, metrics.iconSmall)
                            .pixmap(metrics.iconSmall, metrics.iconSmall));
    refreshing_ = false;
    update();
}

} // namespace choscordb::design
