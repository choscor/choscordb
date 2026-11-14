#include "design_system/quick_search/quick_search_dialog.h"

#include "design_system/fonts/fonts.h"
#include "design_system/icons.h"
#include "design_system/kbd/key_hints.h"
#include "design_system/metrics/metrics.h"
#include "design_system/spinner/spinner.h"
#include "design_system/style/style_resource.h"
#include "design_system/text/text.h"
#include "design_system/theme.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QShowEvent>
#include <QStringList>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>

namespace choscordb::design {
QuickSearchResultRow::QuickSearchResultRow(const QuickSearchResult& result, QWidget* parent)
    : QWidget(parent), iconRole_(result.icon) {
    setObjectName("quickSearchResultRow");
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAccessibleName(QStringLiteral("%1. %2. %3").arg(result.type, result.title, result.context));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(spacing(Spacing::Two), spacing(Spacing::OneHalf),
                               spacing(Spacing::Two), spacing(Spacing::OneHalf));
    layout->setSpacing(spacing(Spacing::Three));

    iconLabel_ = new QLabel(this);
    iconLabel_->setObjectName("quickSearchResultIcon");
    iconLabel_->setFixedSize(dimension(Dimension::IconSmall), dimension(Dimension::IconSmall));
    iconLabel_->setAccessibleName(result.type);
    layout->addWidget(iconLabel_, 0, Qt::AlignVCenter);

    auto* text = new QHBoxLayout;
    text->setContentsMargins(0, 0, 0, 0);
    text->setSpacing(spacing(Spacing::Two));
    titleLabel_ = new QLabel(result.title, this);
    titleLabel_->setObjectName("quickSearchResultTitle");
    titleLabel_->setTextFormat(Qt::PlainText);
    titleLabel_->setFont(resolveTypography(TextStyle::Body));
    text->addWidget(titleLabel_);
    separatorLabel_ = new QLabel(QStringLiteral("\u00b7"), this);
    separatorLabel_->setObjectName("quickSearchResultSeparator");
    separatorLabel_->setFont(resolveTypography(TextStyle::Body));
    separatorLabel_->setVisible(!result.context.isEmpty());
    text->addWidget(separatorLabel_);
    detailLabel_ = new QLabel(result.context, this);
    detailLabel_->setObjectName("quickSearchResultDetail");
    detailLabel_->setTextFormat(Qt::PlainText);
    detailLabel_->setFont(resolveTypography(TextStyle::Callout));
    detailLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    text->addWidget(detailLabel_, 1);
    layout->addLayout(text, 1);
    refreshAppearance();
}

void QuickSearchResultRow::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange ||
        event->type() == QEvent::ParentChange)
        refreshAppearance();
}

void QuickSearchResultRow::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    refreshAppearance();
}

void QuickSearchResultRow::refreshAppearance() {
    if (refreshing_ || !iconLabel_)
        return;
    refreshing_ = true;
    const auto colors = resolvedThemeForWidget(*this).colors;
    auto titlePalette = titleLabel_->palette();
    titlePalette.setColor(QPalette::WindowText, colors.textDefault);
    titleLabel_->setPalette(titlePalette);
    auto subtlePalette = detailLabel_->palette();
    subtlePalette.setColor(QPalette::WindowText, colors.textSubtle);
    detailLabel_->setPalette(subtlePalette);
    separatorLabel_->setPalette(subtlePalette);
    const int size = dimension(Dimension::IconSmall);
    iconLabel_->setPixmap(themedIcon(iconRole_, colors.textSubtle, size).pixmap(size, size));
    refreshing_ = false;
}

QuickSearchDialog::QuickSearchDialog(QWidget* parent)
    : QDialog(parent), presentation_(*this, DialogPresentation::Placement::TopCenter) {
    presentation_.makeModal();
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_WindowPropagation);
    if (parent)
        setPalette(parent->palette());
    setProperty("appDialog", true);
    setObjectName("quickSearchDialog");
    setAccessibleName(tr("Quick search"));
    setMaximumWidth(dimension(Dimension::QuickSearchWidth));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* header = new QWidget(this);
    header->setObjectName("quickSearchHeader");
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(spacing(Spacing::Four), spacing(Spacing::Three),
                                     spacing(Spacing::Four), spacing(Spacing::Three));
    headerLayout->setSpacing(spacing(Spacing::Three));
    searchIcon_ = new QLabel(header);
    searchIcon_->setObjectName("quickSearchIcon");
    searchIcon_->setFixedSize(dimension(Dimension::IconSmall), dimension(Dimension::IconSmall));
    headerLayout->addWidget(searchIcon_);
    input_ = new QLineEdit(header);
    input_->setObjectName("quickSearchInput");
    input_->setAccessibleName(tr("Search destinations"));
    input_->setPlaceholderText(tr("Search screens, tabs, objects, and SQL…"));
    input_->setMaxLength(128);
    input_->installEventFilter(this);
    headerLayout->addWidget(input_, 1);
    layout->addWidget(header);

    list_ = new QListWidget(this);
    list_->setObjectName("quickSearchResults");
    list_->setAccessibleName(tr("Quick search results"));
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setUniformItemSizes(true);
    list_->setFocusPolicy(Qt::TabFocus);
    list_->installEventFilter(this);
    list_->setFixedHeight(8 * dimension(Dimension::MenuRow) + 2 * spacing(Spacing::Two) + 1);
    layout->addWidget(list_);
    placeholder_ = createPanelStateText({}, this);
    placeholder_->setObjectName("quickSearchPlaceholder");
    layout->addWidget(placeholder_);

    auto* footer = new QWidget(this);
    footer->setObjectName("quickSearchFooter");
    auto* footerLayout = new QVBoxLayout(footer);
    footerLayout->setContentsMargins(spacing(Spacing::Four), spacing(Spacing::Three),
                                     spacing(Spacing::Four), spacing(Spacing::Three));
    footerLayout->setSpacing(spacing(Spacing::Two));

    status_ = new QLabel(footer);
    status_->setObjectName("quickSearchStatus");
    status_->setAccessibleName(tr("Quick search status"));
    status_->setWordWrap(true);
    status_->setFont(resolveTypography(TextStyle::Caption));
    busy_ = new Spinner(footer);
    busy_->setObjectName("quickSearchBusy");
    busy_->setAccessibleName(tr("Searching"));
    busy_->hide();
    auto* statusRow = new QHBoxLayout;
    statusRow->setSpacing(spacing(Spacing::One));
    statusRow->addWidget(busy_, 0, Qt::AlignTop);
    statusRow->addWidget(status_, 1);
    footerLayout->addLayout(statusRow);
    const auto native = [](QKeyCombination key) {
        return QKeySequence(key).toString(QKeySequence::NativeText);
    };
    auto* hints = new KeyHints({{native(Qt::Key_Up) + native(Qt::Key_Down), tr("Select")},
                                {native(Qt::Key_Return), tr("Open")},
                                {native(Qt::Key_Escape), tr("Close")}},
                               footer);
    hints->setObjectName("quickSearchKeyHints");
    footerLayout->addWidget(hints);
    layout->addWidget(footer);

    connect(input_, &QLineEdit::textChanged, this, &QuickSearchDialog::queryChanged);
    connect(list_, &QListWidget::itemClicked, this, [this] { activateCurrent(); });
    connect(list_, &QListWidget::currentRowChanged, this,
            [this] { updateSelectedAccessibility(); });
    updateStatus();
}

QuickSearchDialog::~QuickSearchDialog() = default;

QString QuickSearchDialog::query() const {
    return input_->text();
}
QList<QuickSearchResult> QuickSearchDialog::results() const {
    return results_;
}
QString QuickSearchDialog::selectedResultId() const {
    const int row = list_->currentRow();
    return row >= 0 && row < results_.size() ? results_.at(row).id : QString{};
}

void QuickSearchDialog::setResults(const QList<QuickSearchResult>& results) {
    const auto same = [](const QuickSearchResult& left, const QuickSearchResult& right) {
        return left.id == right.id && left.icon == right.icon && left.type == right.type &&
               left.title == right.title && left.context == right.context;
    };
    if (results.size() == results_.size() && list_->count() == results_.size() &&
        std::equal(results.cbegin(), results.cend(), results_.cbegin(), same)) {
        updateStatus();
        return;
    }
    const QString selectedId = selectedResultId();
    results_ = results;
    list_->clear();
    for (const auto& result : results_) {
        auto* item = new QListWidgetItem(list_);
        item->setData(Qt::AccessibleTextRole,
                      QStringLiteral("%1. %2. %3").arg(result.type, result.title, result.context));
        item->setData(Qt::UserRole, result.id);
        item->setSizeHint(QSize(0, dimension(Dimension::MenuRow)));
        item->setToolTip(result.context);
        list_->setItemWidget(item, new QuickSearchResultRow(result, list_));
    }
    int selectedRow = 0;
    if (!selectedId.isEmpty()) {
        for (int row = 0; row < results_.size(); ++row) {
            if (results_.at(row).id == selectedId) {
                selectedRow = row;
                break;
            }
        }
    }
    if (!results_.isEmpty())
        list_->setCurrentRow(selectedRow);
    updateSelectedAccessibility();
    updateStatus();
}

void QuickSearchDialog::updateSelectedAccessibility() {
    list_->setAccessibleDescription(
        tr("%1 results. %2")
            .arg(results_.size())
            .arg(list_->currentItem()
                     ? list_->currentItem()->data(Qt::AccessibleTextRole).toString()
                     : QString{}));
}

void QuickSearchDialog::setStatus(const QString& message) {
    statusMessage_ = message;
    updateStatus();
}
void QuickSearchDialog::setSearchState(PanelStateKind kind, const QString& message, bool busy) {
    showPanelState(placeholder_, kind, message);
    busy_->setVisible(busy);
    searchMessage_ = kind == PanelStateKind::Hidden ? message : QString();
    updateStatus();
}
void QuickSearchDialog::setReopenShortcut(const QKeySequence& shortcut) {
    reopenShortcut_ = shortcut;
}

void QuickSearchDialog::updateStatus() {
    QStringList messages;
    if (!searchMessage_.isEmpty())
        messages << searchMessage_;
    if (!statusMessage_.isEmpty())
        messages << statusMessage_;
    status_->setText(messages.join(QStringLiteral("  ")));
    status_->setAccessibleDescription(status_->text());
}

void QuickSearchDialog::openSearch() {
    if (isVisible()) {
        raise();
        input_->setFocus(Qt::ShortcutFocusReason);
        return;
    }
    results_.clear();
    list_->clear();
    statusMessage_.clear();
    searchMessage_.clear();
    showPanelState(placeholder_, PanelStateKind::Hidden, {});
    busy_->hide();
    const bool changed = !input_->text().isEmpty();
    input_->clear();
    updateStatus();
    show();
    input_->setFocus(Qt::ShortcutFocusReason);
    if (!changed)
        emit queryChanged(QString{});
}

void QuickSearchDialog::activateCurrent() {
    const QString id = selectedResultId();
    if (!id.isEmpty())
        emit activated(id);
}

bool QuickSearchDialog::eventFilter(QObject* watched, QEvent* event) {
    if ((watched == input_ || watched == list_) &&
        (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (!reopenShortcut_.isEmpty() && QKeySequence(key->keyCombination()) == reopenShortcut_) {
            if (event->type() == QEvent::ShortcutOverride) {
                event->accept();
            } else {
                input_->setFocus(Qt::ShortcutFocusReason);
            }
            return true;
        }
    }
    if ((watched == input_ || watched == list_) && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape) {
            reject();
            return true;
        }
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            activateCurrent();
            return true;
        }
        if (watched == input_ && (key->key() == Qt::Key_Down || key->key() == Qt::Key_Up)) {
            if (!results_.isEmpty()) {
                const int delta = key->key() == Qt::Key_Down ? 1 : -1;
                list_->setCurrentRow(qBound(0, list_->currentRow() + delta, results_.size() - 1));
            }
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void QuickSearchDialog::changeEvent(QEvent* event) {
    QDialog::changeEvent(event);
    if (!list_ || (event->type() != QEvent::PaletteChange && event->type() != QEvent::StyleChange))
        return;
    refreshSearchIcon();
    for (int row = 0; row < list_->count(); ++row) {
        if (auto* resultRow =
                static_cast<QuickSearchResultRow*>(list_->itemWidget(list_->item(row))))
            resultRow->refreshAppearance();
    }
}

void QuickSearchDialog::refreshSearchIcon() {
    const int size = dimension(Dimension::IconSmall);
    searchIcon_->setPixmap(
        themedIcon(Icon::Search, resolvedThemeForWidget(*this).colors.textSubtle, size)
            .pixmap(size, size));
}

void QuickSearchDialog::paintEvent(QPaintEvent*) {
    paintDialogSurface(*this, true, Radius::Large);
}
void QuickSearchDialog::showEvent(QShowEvent* event) {
    const int width = parentWidget()
                          ? qMin(dimension(Dimension::QuickSearchWidth),
                                 qMax(1, parentWidget()->width() - spacing(Spacing::Eight)))
                          : dimension(Dimension::QuickSearchWidth);
    resize(width, sizeHint().height());
    refreshSearchIcon();
    QDialog::showEvent(event);
    presentation_.shown();
}
void QuickSearchDialog::hideEvent(QHideEvent* event) {
    QDialog::hideEvent(event);
    presentation_.hidden();
}

}
