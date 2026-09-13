#include "widgets/confirmation_dialog.h"

#include "design_system/icons.h"
#include "design_system/theme.h"
#include "design_system/typography.h"

#include <QAbstractButton>
#include <QDialogButtonBox>
#include <QEvent>
#include <QGridLayout>
#include <QHideEvent>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QShowEvent>

namespace choscordb {
ConfirmationDialog::ConfirmationDialog(Icon icon, const QString& title, const QString& text,
                                       StandardButtons buttons, QWidget* parent)
    : QMessageBox(icon, title, text, buttons, parent), sourceIcon_(icon), presentation_(*this) {
    setOption(QMessageBox::Option::DontUseNativeDialog);
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_WindowPropagation);
    QDialog::setWindowModality(Qt::WindowModal);
    setProperty("appDialog", true);
    setTextFormat(Qt::PlainText);
    QDialog::setWindowTitle(title);
    refreshIcon();
}
void ConfirmationDialog::refreshIcon() {
    if (sourceIcon_ == NoIcon) {
        return;
    }
    const auto theme = design::resolvedThemeForWidget(*this);
    const auto role = sourceIcon_ == Critical ? design::Icon::Error : design::Icon::Warning;
    const auto color = sourceIcon_ == Critical  ? theme.colors.destructive
                       : sourceIcon_ == Warning ? theme.colors.warning
                                                : theme.colors.foreground;
    if (iconTint_ == color) {
        return;
    }
    iconTint_ = color;
    setIconPixmap(design::themedIcon(role, color, 24).pixmap(24, 24));
}
bool ConfirmationDialog::event(QEvent* event) {
    if (event->type() == QEvent::LayoutRequest && heading_) {
        const auto result = QDialog::event(event);
        if (isVisible()) {
            prepareContent();
        }
        return result;
    }
    const auto result = QMessageBox::event(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange) {
        refreshIcon();
        if (isVisible()) {
            prepareContent();
        }
    }
    return result;
}

QMessageBox::StandardButton ConfirmationDialog::question(QWidget* parent, const QString& title,
                                                         const QString& text,
                                                         StandardButtons buttons,
                                                         StandardButton defaultButton) {
    ConfirmationDialog dialog(Question, title, text, buttons, parent);
    dialog.setDefaultButton(defaultButton);
    const auto result = dialog.exec();
    return result == -1 ? Cancel : dialog.standardButton(dialog.clickedButton());
}
void ConfirmationDialog::prepareContent() {
    const int padding = design::spacing(design::Spacing::Four);
    // QMessageBox installs additional platform-specific QWidget margins (on macOS,
    // 24/15/24/20). The shared panel owns its margins at the layout boundary.
    setContentsMargins(0, 0, 0, 0);
    if (auto* grid = qobject_cast<QGridLayout*>(layout())) {
        if (!heading_) {
            heading_ = new QLabel(this);
            heading_->setObjectName("confirmationHeading");
            heading_->setProperty("designRole", "heading");
            heading_->setTextFormat(Qt::PlainText);
            heading_->setWordWrap(true);
        }
        if (!bodyScroll_) {
            bodyScroll_ = new QScrollArea(this);
            bodyScroll_->setObjectName("confirmationBodyScroll");
            bodyScroll_->setFrameShape(QFrame::NoFrame);
            bodyScroll_->setWidgetResizable(true);
            bodyScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            bodyScroll_->setAutoFillBackground(false);
            bodyScroll_->viewport()->setAutoFillBackground(false);
            bodyText_ = new design::Text;
            bodyText_->setObjectName("confirmationBodyText");
            bodyText_->setWordWrap(true);
            bodyText_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
            bodyScroll_->setWidget(bodyText_);
            bodyText_->setAutoFillBackground(false);
        }
        const auto body =
            informativeText().isEmpty() ? text() : text() + "\n\n" + informativeText();
        bodyText_->setText(body);
        bodyText_->setAccessibleName(body);
        if (auto* original = findChild<QLabel*>("qt_msgbox_label")) {
            const int index = grid->indexOf(original);
            if (index >= 0) {
                int row = 0, column = 0, rows = 0, columns = 0;
                grid->getItemPosition(index, &row, &column, &rows, &columns);
                delete grid->takeAt(index);
                grid->addWidget(bodyScroll_, row, column, rows, columns);
            }
            original->hide();
        }
        if (auto* informative = findChild<QLabel*>("qt_msgbox_informativelabel")) {
            informative->hide();
        }
        bodyScroll_->show();
        heading_->setText(windowTitle());
        if (grid->indexOf(heading_) < 0) {
            struct Cell {
                QLayoutItem* item;
                int row;
                int column;
                int rows;
                int columns;
            };
            QList<Cell> cells;
            const int columns = qMax(1, grid->columnCount());
            while (grid->count() > 0) {
                Cell cell{};
                grid->getItemPosition(0, &cell.row, &cell.column, &cell.rows, &cell.columns);
                cell.item = grid->takeAt(0);
                cells.append(cell);
            }
            for (const auto& cell : cells) {
                grid->addItem(cell.item, cell.row + 1, cell.column, cell.rows, cell.columns,
                              cell.item->alignment());
            }
            grid->addWidget(heading_, 0, 0, 1, columns);
        }
        grid->setContentsMargins(padding, padding, padding, padding);
        grid->setSpacing(padding);
        heading_->show();
    }
    for (auto* button : buttons()) {
        const auto role = buttonRole(button);
        const bool affirmative = role == AcceptRole || role == YesRole;
        button->setProperty("primary", affirmative);
        button->setProperty("variant", role == DestructiveRole ? "destructive"
                                       : affirmative           ? "default"
                                                               : "outline");
    }
    const int referenceWidth = design::dimension(design::Dimension::ModalWidth);
    const int width = parentWidget()
                          ? qMin(referenceWidth, qMax(1, parentWidget()->window()->width() - 32))
                          : referenceWidth;
    const int screenHeight = screen()->availableGeometry().height() - 32;
    const int availableHeight =
        parentWidget() ? qMin(screenHeight, parentWidget()->window()->height() - 32) : screenHeight;
    if (auto* buttonBox = findChild<QDialogButtonBox*>()) {
        if (buttonBox->sizeHint().width() > width - 2 * padding) {
            buttonBox->setOrientation(Qt::Vertical);
        }
    }
    setFixedWidth(width);
    if (bodyScroll_) {
        // Measure the assigned viewport, including Qt's icon/column geometry,
        // rather than guessing its width from the nominal icon size.
        layout()->invalidate();
        layout()->activate();
        const int bodyWidth = qMax(1, bodyScroll_->maximumViewportSize().width());
        const int naturalHeight = bodyText_->heightForWidth(bodyWidth);
        bodyScroll_->setFixedHeight(qMin(naturalHeight, qMax(40, availableHeight / 2)));
    }
    if (layout()) {
        layout()->invalidate();
        layout()->activate();
    }
    const int contentHeight = layout() && layout()->hasHeightForWidth()
                                  ? layout()->totalHeightForWidth(width)
                              : layout() ? layout()->totalSizeHint().height()
                                         : sizeHint().height();
    setFixedSize(width, qMax(1, qMin(contentHeight, availableHeight)));
}
void ConfirmationDialog::showEvent(QShowEvent* event) {
    QMessageBox::showEvent(event);
    refreshIcon();
    prepareContent();
    presentation_.shown();
}
void ConfirmationDialog::hideEvent(QHideEvent* event) {
    QMessageBox::hideEvent(event);
    presentation_.hidden();
}
void ConfirmationDialog::paintEvent(QPaintEvent*) {
    design::paintDialogSurface(*this);
}
} // namespace choscordb
