#include "design_system/status_line/status_line.h"

#include "design_system/button/button.h"
#include "design_system/button_group/button_group.h"
#include "design_system/fonts/fonts.h"
#include "design_system/metrics/metrics.h"
#include "design_system/style/style_resource.h"
#include "design_system/theme.h"
#include <QStyle>

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPalette>
#include <QResizeEvent>
#include <QStringList>
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
    Button sizingButton({});
    sizingButton.setButtonSize(ButtonSize::IconSmall);
    setFixedHeight(qMax(fontMetrics().height(), sizingButton.sizeHint().height()) +
                   2 * metrics.spacingSmall);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAutoFillBackground(true);
    refreshAppearance();
}

void StatusLine::setState(State state) {
    state_ = state;
    refreshAppearance();
}
void StatusLine::configure(const Fields& fields, bool centered) {
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
    QStringList parts;
    for (const auto& value : {content.source, content.outcome, content.duration, content.memory,
                              content.page, content.rows})
        if (!value.isEmpty())
            parts << value;
    const auto summary = parts.join(QStringLiteral(" · "));
    setToolTip(summary);
    setAccessibleDescription(summary);
    setAccessibleName(state == State::Error     ? tr("Error")
                      : state == State::Success ? tr("Success")
                                                : tr("Status"));
    const std::array<QLabel*, 6> labels{fields_.source, fields_.outcome, fields_.duration,
                                        fields_.memory, fields_.page,    fields_.rows};
    const std::array<QString, 6> values{content.source, content.outcome, content.duration,
                                        content.memory, content.page,    content.rows};
    for (size_t i = 0; i < labels.size(); ++i) {
        if (!labels[i])
            continue;
        labels[i]->setToolTip(summary);
        labels[i]->setAccessibleName(i == 0 && values[i].isEmpty() ? summary : values[i]);
        labels[i]->setAccessibleDescription(summary);
    }
    if (fields_.source)
        fields_.source->setProperty("fullSource", content.source);
    if (fields_.outcome)
        fields_.outcome->setProperty("fullOutcome", content.outcome);
    setState(state);
    fitContent();
}
void StatusLine::fitContent() {
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
    const std::array<QLabel*, 6> labels{fields_.source, fields_.outcome, fields_.duration,
                                        fields_.memory, fields_.page,    fields_.rows};
    const std::array<QString, 6> values{values_.source, values_.outcome, values_.duration,
                                        values_.memory, values_.page,    values_.rows};
    std::array<int, 6> widths{};
    for (size_t i = 0; i < labels.size(); ++i) {
        if (labels[i] && !values[i].isEmpty()) {
            widths[i] = fontMetrics().horizontalAdvance(values[i].simplified()) + 2;
        }
    }
    const int available = qMax(0, right - margins.left());
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
    int x = margins.left();
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
    update();
}
void StatusLine::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
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
    painter.fillRect(rect(), state_ == State::Success ? colors.successSurface
                             : state_ == State::Error ? colors.dangerSurface
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
    auto colorsForLine = palette();
    colorsForLine.setColor(QPalette::Window, state_ == State::Success ? colors.successSurface
                                             : state_ == State::Error ? colors.dangerSurface
                                                                      : colors.muted);
    colorsForLine.setColor(QPalette::WindowText, state_ == State::Success ? colors.success
                                                 : state_ == State::Error ? colors.danger
                                                                          : colors.mutedText);
    setPalette(colorsForLine);
    const auto semantic = state_ == State::Error     ? "error"
                          : state_ == State::Success ? "success"
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
    refreshing_ = false;
}

} // namespace choscordb::design
