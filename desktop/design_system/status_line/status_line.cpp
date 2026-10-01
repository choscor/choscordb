#include "design_system/status_line/status_line.h"

#include "design_system/button/button.h"
#include "design_system/button_group/button_group.h"
#include "design_system/dialog_shell/dialog_shell.h"
#include "design_system/fonts/fonts.h"
#include "design_system/icons.h"
#include "design_system/metrics/metrics.h"
#include "design_system/style/style_resource.h"
#include "design_system/theme.h"
#include <QStyle>

#include <QAccessible>
#include <QDialogButtonBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPalette>
#include <QPlainTextEdit>
#include <QResizeEvent>
#include <QStringList>
#include <QToolButton>
#include <QVBoxLayout>
#include <array>

namespace choscordb::design {

QString statusLineStyleSheet(const ResolvedTheme& theme) {
    auto sheet = loadStyleSheet(QStringLiteral("status_line/status_line.qss"));
    sheet.replace(QStringLiteral("@neutral"), theme.colors.mutedText.name());
    sheet.replace(QStringLiteral("@success"), theme.colors.success.name());
    sheet.replace(QStringLiteral("@danger"), theme.colors.danger.name());
    return sheet;
}

StatusLine::StatusLine(QWidget* parent) : QWidget(parent) {
    const auto metrics = resolveMetrics(Density::Compact, true);
    setFont(resolveTypography(TypographyRole::Ui));
    content_ = new QHBoxLayout(this);
    content_->setContentsMargins(metrics.spacingMedium, metrics.spacingSmall, metrics.spacingMedium,
                                 metrics.spacingSmall);
    content_->setSpacing(metrics.spacingMedium);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
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

void StatusLine::setState(State state) {
    state_ = state;
    refreshAppearance();
}
void StatusLine::configure(const Fields& fields, bool centered) {
    configured_ = true;
    for (auto* control : {static_cast<QWidget*>(message_), static_cast<QWidget*>(loading_),
                          static_cast<QWidget*>(details_)})
        content_->removeWidget(control);
    message_->hide();
    details_->setText({});
    details_->setToolTip(tr("Read full status details"));
    const auto metrics = resolveMetrics(Density::Compact, true);
    Button sizingButton({});
    sizingButton.setButtonSize(ButtonSize::IconSmall);
    setFixedHeight(qMax(fontMetrics().height(), sizingButton.sizeHint().height()) +
                   2 * metrics.spacingSmall);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    fields_ = fields;
    centered_ = centered;
    for (auto* label : {fields.source, fields.outcome, fields.duration, fields.memory, fields.page,
                        fields.rows}) {
        if (!label)
            continue;
        content_->removeWidget(label);
        label->setParent(this);
        label->setFont(font());
        label->setForegroundRole(QPalette::WindowText);
        label->setTextFormat(Qt::PlainText);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        label->setWordWrap(false);
        label->setMinimumWidth(0);
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        label->setAlignment(centered ? Qt::AlignCenter : Qt::AlignLeft | Qt::AlignVCenter);
    }
    if ((fields.previous || fields.next) && !paging_)
        paging_ = new ButtonGroup(Qt::Horizontal, this);
    if (paging_)
        paging_->layout()->setSpacing(content_->spacing());
    for (auto* button : {fields.previous, fields.next}) {
        if (!button)
            continue;
        content_->removeWidget(button);
        button->setParent(paging_);
        if (auto* shared = qobject_cast<Button*>(button)) {
            shared->setVariant(ButtonVariant::Ghost);
            shared->setButtonSize(ButtonSize::IconSmall);
            shared->setDesignIcon(button == fields.previous ? Icon::ChevronLeft
                                                            : Icon::ChevronRight);
        }
        if (auto* shared = qobject_cast<Button*>(button))
            static_cast<ButtonGroup*>(paging_)->addButton(shared);
        else
            static_cast<QBoxLayout*>(paging_->layout())->addWidget(button);
        button->show();
    }
    fitContent();
}
void StatusLine::setContent(const Content& content, State state) {
    values_ = content;
    feedbackMessage_.clear();
    setState(state);
    refreshContentDescription();
    fitContent();
}
void StatusLine::refreshContentDescription() {
    const auto outcome = feedbackMessage_.isEmpty() ? values_.outcome : feedbackMessage_;
    QStringList parts;
    for (const auto& value :
         {values_.source, outcome, values_.duration, values_.memory, values_.page, values_.rows})
        if (!value.isEmpty())
            parts << value;
    const auto summary = parts.join(QStringLiteral(" · "));
    fullMessage_ = summary;
    setToolTip(summary);
    setAccessibleDescription(summary);
    setAccessibleName(state_ == State::Error     ? tr("Error")
                      : state_ == State::Success ? tr("Success")
                                                 : tr("Status"));
    const std::array<QLabel*, 6> labels{fields_.source, fields_.outcome, fields_.duration,
                                        fields_.memory, fields_.page,    fields_.rows};
    const std::array<QString, 6> values{values_.source, outcome,      values_.duration,
                                        values_.memory, values_.page, values_.rows};
    for (size_t i = 0; i < labels.size(); ++i) {
        if (!labels[i])
            continue;
        labels[i]->setToolTip(summary);
        labels[i]->setAccessibleName(i == 0 && values[i].isEmpty() ? summary : values[i]);
        labels[i]->setAccessibleDescription(summary);
    }
    if (fields_.source)
        fields_.source->setProperty("fullSource", values_.source);
    if (fields_.outcome)
        fields_.outcome->setProperty("fullOutcome", outcome);
    QAccessibleEvent changed(this, QAccessible::DescriptionChanged);
    QAccessible::updateAccessibility(&changed);
}
void StatusLine::fitContent(bool reserveDetails) {
    if (!configured_)
        return;
    const auto margins = content_->contentsMargins();
    const int gap = content_->spacing();
    const int itemHeight = height() - margins.top() - margins.bottom();
    int right = width() - margins.right();
    separators_.clear();
    if (paging_) {
        const int previousWidth = fields_.previous ? fields_.previous->sizeHint().width() : 0;
        const int nextWidth = fields_.next ? fields_.next->sizeHint().width() : 0;
        const int pagingWidth = previousWidth + nextWidth + (previousWidth && nextWidth ? gap : 0);
        right -= pagingWidth;
        paging_->setGeometry(right, margins.top(), pagingWidth, itemHeight);
        setMinimumWidth(margins.left() + margins.right() + pagingWidth + 4 * gap +
                        fontMetrics().horizontalAdvance(tr("Failed")) +
                        2 * fontMetrics().horizontalAdvance(QStringLiteral("…")));
        right -= gap;
        paging_->show();
    }
    int left = margins.left();
    if (property("busy").toBool()) {
        const int loadingWidth = loading_->sizeHint().width();
        loading_->setGeometry(left, margins.top(), loadingWidth, itemHeight);
        left += loadingWidth + gap;
    }
    if (reserveDetails) {
        const int detailsWidth = details_->sizeHint().width();
        right -= detailsWidth;
        details_->setGeometry(right, margins.top(), detailsWidth, itemHeight);
        right -= gap;
    }
    const std::array<QLabel*, 6> labels{fields_.source, fields_.outcome, fields_.duration,
                                        fields_.memory, fields_.page,    fields_.rows};
    const std::array<QString, 6> values{
        values_.source,   feedbackMessage_.isEmpty() ? values_.outcome : feedbackMessage_,
        values_.duration, values_.memory,
        values_.page,     values_.rows};
    std::array<int, 6> widths{};
    for (size_t i = 0; i < labels.size(); ++i) {
        if (labels[i] && !values[i].isEmpty()) {
            widths[i] = fontMetrics().horizontalAdvance(values[i].simplified()) + 2;
        }
    }
    const int available = qMax(0, right - left);
    const int outcomeMinimum = qMin(widths[1], fontMetrics().horizontalAdvance(tr("Failed")) + gap);
    const auto occupied = [&] {
        int result = 0;
        bool leading = false, trailing = false;
        for (size_t i = 0; i < widths.size(); ++i) {
            if (!widths[i])
                continue;
            bool& present = i < 4 ? leading : trailing;
            result += widths[i] + (present ? 2 * gap : 0);
            present = true;
        }
        return result + (leading && trailing ? gap : 0);
    };
    // Preserve readable operation and page context before optional execution metrics.
    const auto required = [&] { return occupied() - widths[0] - widths[1] + outcomeMinimum; };
    for (const size_t optional : {size_t(3), size_t(2)})
        if (required() > available)
            widths[optional] = 0;
    for (const size_t flexible : {size_t(0), size_t(1)}) {
        const int floor = flexible == 1 ? outcomeMinimum : 0;
        const int reduction =
            qMin(qMax(0, widths[flexible] - floor), qMax(0, occupied() - available));
        widths[flexible] -= reduction;
    }
    // Tiny viewports compress page/row labels too, while reserving an outcome indication.
    for (const size_t trailing : {size_t(5), size_t(4)}) {
        const int floor =
            qMin(widths[trailing], fontMetrics().horizontalAdvance(QStringLiteral("…")));
        const int reduction =
            qMin(qMax(0, widths[trailing] - floor), qMax(0, occupied() - available));
        widths[trailing] -= reduction;
    }
    if (occupied() > available)
        widths[1] = qMax(0, widths[1] - (occupied() - available));
    int x = left;
    bool leading = false, trailing = false;
    for (size_t i = 0; i < labels.size(); ++i) {
        auto* label = labels[i];
        if (!label)
            continue;
        label->setVisible(widths[i] > 0);
        const auto singleLine = values[i].simplified();
        label->setText(fontMetrics().elidedText(
            singleLine, i == 0 ? Qt::ElideMiddle : Qt::ElideRight, widths[i]));
        if (widths[i] <= 0)
            continue;
        if (i >= 4 && !trailing) {
            int tailWidth = widths[4] + widths[5] + ((widths[4] && widths[5]) ? 2 * gap : 0);
            x = qMax(x, right - tailWidth);
        }
        bool& hasPrevious = i < 4 ? leading : trailing;
        if (hasPrevious) {
            separators_.append(QPoint(x + gap, height() / 2));
            x += 2 * gap;
        }
        if (centered_) {
            label->setGeometry(margins.left(), margins.top(),
                               width() - margins.left() - margins.right(), itemHeight);
        } else
            label->setGeometry(x, margins.top(), widths[i], itemHeight);
        x += widths[i];
        hasPrevious = true;
    }
    const bool needsDetails =
        fullMessage_.size() > 240 || fullMessage_.contains(QLatin1Char('\n')) ||
        (fields_.outcome && fields_.outcome->text() != values[1].simplified());
    details_->setVisible(needsDetails || reserveDetails);
    if (needsDetails && !reserveDetails) {
        fitContent(true);
        return;
    }
    update();
}
void StatusLine::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (configured_)
        fitContent();
    else
        refreshDetailsVisibility();
}
void StatusLine::setAvailable(bool available) {
    setState(available ? State::Success : State::Error);
}
void StatusLine::setNeutral(bool neutral) {
    if (neutral)
        setState(State::Neutral);
}
void StatusLine::setMessage(const QString& message) {
    if (configured_) {
        feedbackMessage_ = message;
        refreshContentDescription();
        fitContent();
        return;
    }
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
    fitContent();
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
    const auto state = property("busy").toBool() ? State::Neutral : state_;
    painter.fillRect(rect(), state == State::Success ? colors.successSurface
                             : state == State::Error ? colors.dangerSurface
                                                     : colors.muted);
    painter.setFont(font());
    painter.setPen(palette().color(QPalette::WindowText));
    for (const auto& point : separators_)
        painter.drawText(
            QRect(point.x() - content_->spacing() / 2, 0, content_->spacing(), height()),
            Qt::AlignCenter, QStringLiteral("·"));
}

void StatusLine::refreshAppearance() {
    if (refreshing_)
        return;
    refreshing_ = true;
    const auto colors = resolvedThemeForWidget(*this).colors;
    const auto state = property("busy").toBool() ? State::Neutral : state_;
    auto colorsForLine = palette();
    colorsForLine.setColor(QPalette::Window, state == State::Success ? colors.successSurface
                                             : state == State::Error ? colors.dangerSurface
                                                                     : colors.muted);
    colorsForLine.setColor(QPalette::WindowText, state == State::Success ? colors.success
                                                 : state == State::Error ? colors.danger
                                                                         : colors.mutedText);
    setPalette(colorsForLine);
    const auto semantic = state == State::Error     ? "error"
                          : state == State::Success ? "success"
                                                    : "neutral";
    for (auto* label : {fields_.source, fields_.outcome, fields_.duration, fields_.memory,
                        fields_.page, fields_.rows}) {
        if (!label)
            continue;
        if (label->property("statusLineState") != semantic) {
            label->setProperty("statusLineState", semantic);
            label->style()->unpolish(label);
            label->style()->polish(label);
        }
    }
    const auto metrics = resolveMetrics(Density::Compact, true);
    message_->setMaximumHeight(metrics.dataRowHeight * 3);
    loading_->setPixmap(themedIcon(Icon::Loader, colors.mutedText, metrics.iconSmall)
                            .pixmap(metrics.iconSmall, metrics.iconSmall));
    if (configured_)
        details_->setIcon(
            themedIcon(Icon::Eye, colorsForLine.color(QPalette::WindowText), metrics.iconSmall));
    refreshing_ = false;
    update();
}

} // namespace choscordb::design
