#include "design_system/quick_search/quick_search_dialog.h"

#include "design_system/fonts/fonts.h"
#include "design_system/metrics/metrics.h"
#include "design_system/style/style_resource.h"
#include "design_system/theme.h"

#include <QEvent>
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

namespace choscordb::design {

QuickSearchDialog::QuickSearchDialog(QWidget* parent)
    : QDialog(parent), presentation_(*this, DialogPresentation::Placement::TopCenter) {
    presentation_.makeModal();
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_WindowPropagation);
    if (parent)
        setPalette(parent->palette());
    setProperty("appDialog", true);
    setObjectName("quickSearchDialog");
    setAccessibleName(tr("Quick switch"));
    setMaximumWidth(dimension(Dimension::QuickSearchWidth));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(spacing(Spacing::Four), spacing(Spacing::Four),
                               spacing(Spacing::Four), spacing(Spacing::Four));
    layout->setSpacing(spacing(Spacing::Two));

    input_ = new QLineEdit(this);
    input_->setObjectName("quickSearchInput");
    input_->setAccessibleName(tr("Search destinations"));
    input_->setPlaceholderText(tr("Search screens, tabs, objects, and SQL…"));
    input_->setMaxLength(128);
    input_->installEventFilter(this);
    layout->addWidget(input_);

    list_ = new QListWidget(this);
    list_->setObjectName("quickSearchResults");
    list_->setAccessibleName(tr("Quick search results"));
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setUniformItemSizes(true);
    list_->setFocusPolicy(Qt::TabFocus);
    list_->installEventFilter(this);
    list_->setFixedHeight(8 * dimension(Dimension::QuickSearchRow));
    layout->addWidget(list_);

    status_ = new QLabel(this);
    status_->setObjectName("quickSearchStatus");
    status_->setAccessibleName(tr("Quick search status"));
    status_->setWordWrap(true);
    status_->setFont(resolveTypography(TypographyRole::Small));
    layout->addWidget(status_);

    connect(input_, &QLineEdit::textChanged, this, &QuickSearchDialog::queryChanged);
    connect(list_, &QListWidget::itemClicked, this, [this] { activateCurrent(); });
    connect(list_, &QListWidget::currentRowChanged, this,
            [this] { updateSelectedAccessibility(); });
    updateStatus();
}

QuickSearchDialog::~QuickSearchDialog() = default;

QString quickSearchStyleSheet() {
    return loadStyleSheet(QStringLiteral("quick_search/quick_search_style_sheet.qss"));
}

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
    const QString selectedId = selectedResultId();
    results_ = results;
    list_->clear();
    for (const auto& result : results_) {
        auto* item = new QListWidgetItem(
            QStringLiteral("%1    %2\n%3").arg(result.type, result.title, result.context), list_);
        item->setData(Qt::UserRole, result.id);
        item->setSizeHint(QSize(0, dimension(Dimension::QuickSearchRow)));
        item->setToolTip(result.context);
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
            .arg(list_->currentItem() ? list_->currentItem()->text() : QString{}));
}

void QuickSearchDialog::setStatus(const QString& message) {
    statusMessage_ = message;
    updateStatus();
}
void QuickSearchDialog::setLoading(bool loading) {
    loading_ = loading;
    updateStatus();
}
void QuickSearchDialog::setError(const QString& message) {
    errorMessage_ = message;
    updateStatus();
}
void QuickSearchDialog::setReopenShortcut(const QKeySequence& shortcut) {
    reopenShortcut_ = shortcut;
}

void QuickSearchDialog::updateStatus() {
    QStringList messages;
    if (loading_)
        messages << tr("Searching…");
    if (!errorMessage_.isEmpty())
        messages << errorMessage_;
    if (!statusMessage_.isEmpty())
        messages << statusMessage_;
    if (messages.isEmpty() && results_.isEmpty())
        messages << tr("No results. Try another search.");
    status_->setText(messages.join(QStringLiteral("  ")));
    status_->setAccessibleDescription(status_->text());
    const char* state = !errorMessage_.isEmpty() ? "error"
                        : loading_               ? "loading"
                        : results_.isEmpty()     ? "empty"
                                                 : "ready";
    status_->setProperty("state", state);
    status_->style()->unpolish(status_);
    status_->style()->polish(status_);
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
    errorMessage_.clear();
    loading_ = false;
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

void QuickSearchDialog::paintEvent(QPaintEvent*) {
    paintDialogSurface(*this);
}
void QuickSearchDialog::showEvent(QShowEvent* event) {
    const int width = parentWidget()
                          ? qMin(dimension(Dimension::QuickSearchWidth),
                                 qMax(1, parentWidget()->width() - spacing(Spacing::Eight)))
                          : dimension(Dimension::QuickSearchWidth);
    resize(width, sizeHint().height());
    QDialog::showEvent(event);
    presentation_.shown();
}
void QuickSearchDialog::hideEvent(QHideEvent* event) {
    QDialog::hideEvent(event);
    presentation_.hidden();
}

} // namespace choscordb::design
