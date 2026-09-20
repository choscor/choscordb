#include "history_dock.h"
#include "design_system/button/button.h"
#include "design_system/button_group/button_group.h"
#include "design_system/confirmation_dialog/confirmation_dialog.h"
#include "design_system/text/text.h"
#include "design_system/theme.h"
#include "design_system/toast_region/toast_region.h"
#include "models/history_model.h"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <atomic>
#include <limits>
namespace choscordb {
namespace {
quint64 token() {
    static std::atomic<quint64> next{quint64(1) << 60};
    return next.fetch_add(1);
}
constexpr quint32 pageSize = 100;
class HistoryRowDelegate final : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        if (!option.widget)
            return;
        const auto colors = design::resolvedThemeForWidget(*option.widget).colors;
        const auto bounds = option.rect;
        painter->save();
        painter->fillRect(bounds, option.state & QStyle::State_Selected ? colors.subtleAccent
                                                                        : colors.card);
        painter->setPen(colors.border);
        painter->drawLine(bounds.bottomLeft(), bounds.bottomRight());
        auto sqlFont = design::resolveTypography(design::TypographyRole::Monospace);
        sqlFont.setPixelSize(12);
        painter->setFont(sqlFont);
        painter->setPen(colors.text);
        const int width = qMax(0, bounds.width() - 28);
        painter->drawText(
            bounds.adjusted(14, 8, -14, -33), Qt::AlignLeft | Qt::AlignVCenter,
            QFontMetrics(sqlFont).elidedText(index.data().toString(), Qt::ElideRight, width));
        auto small = design::resolveTypography(design::TypographyRole::Small);
        small.setPixelSize(10);
        painter->setFont(small);
        const auto status = index.siblingAtColumn(4).data().toString();
        const int badgeWidth = QFontMetrics(small).horizontalAdvance(status) + 14;
        const QRect badge(bounds.left() + 14, bounds.top() + 34, badgeWidth, 20);
        painter->setPen(Qt::NoPen);
        painter->setBrush(colors.muted);
        painter->drawRoundedRect(badge, 4, 4);
        painter->setPen(status == tr("Failed") ? colors.danger : colors.action);
        painter->drawText(badge, Qt::AlignCenter, status);
        const auto detail = QString("%1 · %2 · %3 · %4")
                                .arg(index.siblingAtColumn(0).data().toString(),
                                     index.siblingAtColumn(1).data().toString(),
                                     index.siblingAtColumn(3).data().toString(),
                                     tr("%1 rows").arg(index.siblingAtColumn(5).data().toString()));
        const QRect detailRect(badge.right() + 10, badge.top(),
                               qMax(0, bounds.right() - badge.right() - 24), badge.height());
        painter->setPen(colors.mutedText);
        painter->drawText(
            detailRect, Qt::AlignLeft | Qt::AlignVCenter,
            QFontMetrics(small).elidedText(detail, Qt::ElideRight, detailRect.width()));
        if (option.state & QStyle::State_HasFocus) {
            painter->setPen(QPen(colors.focus, 2));
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(bounds.adjusted(1, 1, -1, -1));
        }
        painter->restore();
    }
};

} // namespace
HistoryDock::HistoryDock(EngineAdapter* adapter, QWidget* parent)
    : QWidget(parent), adapter_(adapter) {
    setObjectName("historyDock");
    auto* body = new QWidget(this);
    const auto metrics = design::resolveMetrics(design::Density::Compact, true);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(0, metrics.spacingMedium, 0, 0);
    layout->setSpacing(metrics.spacingMedium);
    auto* toolbarBody = new QWidget(body);
    toolbarBody->setObjectName("historyToolbar");
    auto* toolbar = new QHBoxLayout(toolbarBody);
    toolbar->setContentsMargins(design::spacing(design::Spacing::Three), 0,
                                design::spacing(design::Spacing::Three), 0);
    layout->addWidget(toolbarBody);
    auto* filters = new QHBoxLayout;
    filters->setContentsMargins(design::spacing(design::Spacing::Three), 0,
                                design::spacing(design::Spacing::Three), 0);
    search_ = new QLineEdit(body);
    search_->setObjectName("historySearch");
    search_->setPlaceholderText(tr("Filter this page"));
    search_->setAccessibleName(tr("Filter this history page"));
    search_->setToolTip(
        tr("Search visible SQL excerpts, connection names, and statuses on this page."));
    search_->setMaxLength(256);
    filters->addWidget(search_, 1);
    statusFilter_ = new QComboBox(body);
    statusFilter_->setObjectName("historyStatusFilter");
    statusFilter_->setAccessibleName(tr("History status filter"));
    statusFilter_->addItem(tr("All statuses"), QString{});
    statusFilter_->addItem(tr("Completed"), "completed");
    statusFilter_->addItem(tr("Failed"), "failed");
    statusFilter_->addItem(tr("Cancelled"), "cancelled");
    statusFilter_->addItem(tr("Disconnected"), "disconnected");
    filters->addWidget(statusFilter_);
    layout->addLayout(filters);

    record_ = new QCheckBox(tr("Record history"), body);
    record_->setObjectName("recordHistory");
    clear_ = new design::Button(tr("Clear history…"), body);
    clear_->setObjectName("clearHistory");
    refresh_ = new design::Button(tr("Refresh"), body);
    refresh_->setObjectName("refreshHistory");
    clear_->setVariant(design::ButtonVariant::Destructive);
    clear_->setDesignIcon(design::Icon::Close);
    clear_->setAccessibleName(tr("Clear history"));
    clear_->setToolTip(tr("Clear history…"));
    clear_->setText({});
    refresh_->setVariant(design::ButtonVariant::Outline);
    refresh_->setDesignIcon(design::Icon::Refresh);
    clear_->setButtonSize(design::ButtonSize::IconSmall);
    refresh_->setButtonSize(design::ButtonSize::Small);
    status_ = new design::Text({}, body);
    status_->setObjectName("historyStatus");
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    model_ = new HistoryModel(this);
    table_ = new QTableView(body);
    table_->setObjectName("historyTable");
    table_->setModel(model_);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setItemDelegate(new HistoryRowDelegate(table_));
    table_->setFrameShape(QFrame::NoFrame);
    table_->setShowGrid(false);
    table_->setWordWrap(false);
    table_->horizontalHeader()->hide();
    table_->verticalHeader()->hide();
    for (int column = 0; column < model_->columnCount(); ++column)
        table_->setColumnHidden(column, column != 2);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table_->verticalHeader()->setDefaultSectionSize(62);
    auto* content = new QSplitter(Qt::Vertical, body);
    content->setObjectName("historyContentSplitter");
    content->addWidget(table_);
    auto* previewBody = new QWidget(content);
    auto* previewLayout = new QVBoxLayout(previewBody);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    previewLayout->setSpacing(metrics.spacingMedium);
    content->addWidget(previewBody);
    previewBody->hide();
    layout->addWidget(content, 1);
    auto* previewToolbar = new QHBoxLayout;
    previewToolbar->addStretch();
    previewPrevious_ = new design::Button(tr("Earlier text"), body);
    previewPrevious_->setObjectName("historyPreviewPrevious");
    previewNext_ = new design::Button(tr("Later text"), body);
    previewNext_->setObjectName("historyPreviewNext");
    previewPrevious_->setVariant(design::ButtonVariant::Outline);
    previewNext_->setVariant(design::ButtonVariant::Outline);
    previewPrevious_->setButtonSize(design::ButtonSize::Small);
    previewNext_->setButtonSize(design::ButtonSize::Small);
    previewPrevious_->setDesignIcon(design::Icon::ChevronLeft);
    previewNext_->setDesignIcon(design::Icon::ChevronRight);
    auto* previewPaging = new design::ButtonGroup(Qt::Horizontal, previewBody);
    previewPaging->addButton(previewPrevious_);
    previewPaging->addButton(previewNext_);
    previewToolbar->addWidget(previewPaging);
    previewLayout->addLayout(previewToolbar);
    preview_ = new QPlainTextEdit(body);
    preview_->setObjectName("historyPreview");
    preview_->setReadOnly(true);
    previewLayout->addWidget(preview_, 1);
    auto* footerBody = new QWidget(body);
    footerBody->setObjectName("historyFooter");
    footerBody->setProperty("designSurface", "subtle");
    footerBody->setAttribute(Qt::WA_StyledBackground);
    auto* footer = new QHBoxLayout(footerBody);
    footer->setContentsMargins(
        design::spacing(design::Spacing::Three), design::spacing(design::Spacing::OneHalf),
        design::spacing(design::Spacing::Three), design::spacing(design::Spacing::OneHalf));
    auto* manage = new QToolButton(toolbarBody);
    manage->setObjectName("historyManage");
    manage->setText(tr("Manage history"));
    manage->setAccessibleName(tr("Manage history"));
    manage->setToolTip(tr("Manage history"));
    manage->setIcon(design::themedIcon(design::Icon::ChevronDown,
                                       design::resolvedThemeForWidget(*manage).colors.foreground,
                                       design::dimension(design::Dimension::Icon)));
    manage->setToolButtonStyle(Qt::ToolButtonIconOnly);
    manage->setPopupMode(QToolButton::InstantPopup);
    auto* manageMenu = new QMenu(manage);
    auto* recordAction = new QWidgetAction(manageMenu);
    recordAction->setDefaultWidget(record_);
    manageMenu->addAction(recordAction);
    auto* refreshAction = new QWidgetAction(manageMenu);
    refreshAction->setDefaultWidget(refresh_);
    manageMenu->addAction(refreshAction);
    auto* previewAction = manageMenu->addAction(tr("View full query"));
    previewAction->setObjectName("historyShowPreview");
    previewAction->setCheckable(true);
    connect(previewAction, &QAction::toggled, previewBody, &QWidget::setVisible);
    manage->setMenu(manageMenu);

    previous_ = new design::Button(tr("Previous"), body);
    next_ = new design::Button(tr("Next"), body);
    next_->setObjectName("historyNext");
    previous_->setObjectName("historyPrevious");
    page_ = new design::Text({}, body);
    page_->setObjectName("historyRange");
    open_ = new design::Button(tr("Open in new query"), body);
    open_->setObjectName("openHistoryQuery");
    previous_->setVariant(design::ButtonVariant::Outline);
    next_->setVariant(design::ButtonVariant::Outline);
    previous_->setDesignIcon(design::Icon::ChevronLeft);
    next_->setDesignIcon(design::Icon::ChevronRight);
    previous_->setButtonSize(design::ButtonSize::IconSmall);
    previous_->setText({});
    previous_->setAccessibleName(tr("Previous history page"));
    previous_->setToolTip(tr("Previous history page"));
    next_->setButtonSize(design::ButtonSize::IconSmall);
    next_->setText({});
    next_->setAccessibleName(tr("Next history page"));
    next_->setToolTip(tr("Next history page"));
    open_->setDesignIcon(design::Icon::Code);
    open_->setAccessibleName(tr("Open in new query"));
    open_->setToolTip(tr("Open in new query"));
    open_->setText({});
    open_->setButtonSize(design::ButtonSize::IconSmall);
    auto* paging = new design::ButtonGroup(Qt::Horizontal, body);
    paging->setObjectName("historyPaging");
    paging->addButton(previous_);
    paging->addButton(next_);
    toolbar->addWidget(manage);
    toolbar->addWidget(clear_);
    toolbar->addWidget(open_);
    toolbar->addStretch();
    footer->addWidget(page_);
    footer->addWidget(status_);
    footer->addStretch();
    footer->addWidget(paging);
    layout->addWidget(footerBody);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(body);
    connect(search_, &QLineEdit::textChanged, this, &HistoryDock::applyFilter);
    connect(statusFilter_, &QComboBox::currentIndexChanged, this, &HistoryDock::applyFilter);
    connect(model_, &QAbstractItemModel::modelReset, this, &HistoryDock::applyFilter);
    connect(model_, &QAbstractItemModel::dataChanged, this, &HistoryDock::applyFilter);
    connect(table_->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            &HistoryDock::selectEntry);
    connect(model_, &QAbstractItemModel::modelReset, this, &HistoryDock::selectEntry);
    connect(previewNext_, &QPushButton::clicked, this, [this] {
        const auto* entry = model_->entry(table_->currentIndex().row());
        if (!entry || previewLength_ == 0 || previewOffset_ + previewLength_ >= entry->sql.size())
            return;
        previewOffsets_.push_back(previewOffset_);
        previewOffset_ += previewLength_;
        renderPreview();
    });
    connect(previewPrevious_, &QPushButton::clicked, this, [this] {
        if (previewOffsets_.isEmpty())
            return;
        previewOffset_ = previewOffsets_.takeLast();
        renderPreview();
    });
    connect(table_, &QTableView::activated, this, [this] { openSelection(); });
    connect(open_, &QPushButton::clicked, this, &HistoryDock::openSelection);
    connect(refresh_, &QPushButton::clicked, this, &HistoryDock::refresh);
    connect(previous_, &QPushButton::clicked, this, [this] {
        failed_ = false;
        if (!visitedOffsets_.isEmpty())
            loadPage(visitedOffsets_.back());
    });
    connect(next_, &QPushButton::clicked, this, [this] {
        failed_ = false;
        loadPage(offset_ + quint32(model_->rowCount()));
    });
    connect(record_, &QCheckBox::clicked, this, [this](bool checked) {
        if (!adapter_ || !havePolicy_ || policyToken_)
            return;
        auto proposed = policy_;
        proposed.enabled = checked;
        {
            const QSignalBlocker blocker(record_);
            record_->setChecked(policy_.enabled);
        }
        failed_ = false;
        policyToken_ = token();
        status_->clear();
        updateControls();
        adapter_->setHistoryPolicy(proposed, policyToken_);
    });
    connect(clear_, &QPushButton::clicked, this, [this] {
        if (!adapter_ || ConfirmationDialog::question(this, tr("Clear history"),
                                                      tr("Delete all stored query history?"),
                                                      QMessageBox::Yes | QMessageBox::No,
                                                      QMessageBox::No) != QMessageBox::Yes)
            return;
        failed_ = false;
        clearToken_ = token();
        status_->clear();
        updateControls();
        adapter_->clearHistory(clearToken_);
    });
    connect(adapter, &EngineAdapter::historyListed, this,
            [this](quint64 id, const QList<SavedHistoryEntry>& entries) {
                if (!listToken_ || id != listToken_)
                    return;
                listToken_ = 0;
                if (pendingOffset_ == 0)
                    visitedOffsets_.clear();
                else if (pendingOffset_ > offset_)
                    visitedOffsets_.push_back(offset_);
                else if (pendingOffset_ < offset_ && !visitedOffsets_.isEmpty())
                    visitedOffsets_.pop_back();
                offset_ = pendingOffset_;
                model_->setEntries(entries);
                selectEntry();
                if (!failed_)
                    status_->setText(entries.isEmpty() ? tr("No query history on this page.")
                                                       : QString{});
                updateControls();
            });
    connect(adapter, &EngineAdapter::historyPolicyReady, this,
            [this](quint64 id, const HistoryPolicy& policy) {
                if (!policyToken_ || id != policyToken_)
                    return;
                policyToken_ = 0;
                havePolicy_ = true;
                policy_ = policy;
                record_->setChecked(policy.enabled);
                if (!failed_ && !listToken_)
                    status_->setText(model_->rowCount() == 0 ? tr("No query history on this page.")
                                                             : QString{});
                updateControls();
            });
    connect(adapter, &EngineAdapter::historyCleared, this, [this](quint64 id) {
        if (!clearToken_ || id != clearToken_)
            return;
        clearToken_ = 0;
        loadPage(0);
    });
    connect(adapter, &EngineAdapter::recoveryFailed, this,
            [this](quint64 id, const QString& error) {
                if (!id || (id != listToken_ && id != policyToken_ && id != clearToken_))
                    return;
                if (id == listToken_)
                    listToken_ = 0;
                if (id == policyToken_) {
                    policyToken_ = 0;
                    record_->setChecked(policy_.enabled);
                }
                if (id == clearToken_)
                    clearToken_ = 0;
                failed_ = true;
                status_->setText(error);
                updateControls();
            });
    connect(adapter, &EngineAdapter::profilesReady, this,
            [this](quint64 id, const QList<SavedProfile>& profiles) {
                if (!profilesToken_ || id != profilesToken_)
                    return;
                profilesToken_ = 0;
                QHash<QString, QString> names;
                for (const auto& profile : profiles)
                    names.insert(profile.id, profile.name);
                model_->setProfileNames(std::move(names));
            });
    refresh();
}
void HistoryDock::applyConfirmedPolicy(const HistoryPolicy& policy) {
    policy_ = policy;
    havePolicy_ = true;
    policyToken_ = 0;
    const QSignalBlocker blocker(record_);
    record_->setChecked(policy_.enabled);
    updateControls();
    refresh();
}
void HistoryDock::refresh() {
    if (!adapter_)
        return;
    failed_ = false;
    profilesToken_ = token();
    adapter_->listProfiles(profilesToken_);
    if (!policyToken_) {
        policyToken_ = token();
        adapter_->getHistoryPolicy(policyToken_);
    }
    loadPage(0);
}
void HistoryDock::loadPage(quint32 offset) {
    if (!adapter_)
        return;
    listToken_ = token();
    pendingOffset_ = offset;
    if (!failed_)
        status_->clear();
    updateControls();
    adapter_->listHistory(pageSize, offset, listToken_);
}
void HistoryDock::selectEntry() {
    previewOffsets_.clear();
    previewOffset_ = 0;
    renderPreview();
    updateControls();
}
void HistoryDock::renderPreview() {
    const auto* entry = model_->entry(table_->currentIndex().row());
    QString text = entry ? entry->sql.mid(previewOffset_, 65536) : QString{};
    if (!text.isEmpty() && text.back().isHighSurrogate())
        text.chop(1);
    previewLength_ = text.size();
    preview_->setPlainText(text);
    const bool partial = entry && entry->sql.size() > 65536;
    if (partial)
        emit noticeRequested(
            tr("Preview truncated to part %1. Use Earlier text / Later text to read all SQL.")
                .arg(previewOffsets_.size() + 1));
    previewPrevious_->setEnabled(!previewOffsets_.isEmpty());
    previewNext_->setEnabled(entry && previewLength_ > 0 &&
                             previewOffset_ + previewLength_ < entry->sql.size());
}
void HistoryDock::applyFilter() {
    const auto needle = search_->text().trimmed();
    const auto status = statusFilter_->currentData().toString();
    for (int row = 0; row < model_->rowCount(); ++row) {
        QStringList visible;
        for (int column = 0; column < model_->columnCount(); ++column)
            visible.append(model_->index(row, column).data().toString());
        const auto* entry = model_->entry(row);
        const bool matches = visible.join(' ').contains(needle, Qt::CaseInsensitive) &&
                             (status.isEmpty() || (entry && entry->status == status));
        table_->setRowHidden(row, !matches);
    }
    if (table_->currentIndex().isValid() && table_->isRowHidden(table_->currentIndex().row())) {
        table_->clearSelection();
        table_->setCurrentIndex({});
    }
    selectEntry();
}
void HistoryDock::openSelection() {
    if (table_->isRowHidden(table_->currentIndex().row()))
        return;
    if (const auto* entry = model_->entry(table_->currentIndex().row()))
        emit openRequested(*entry);
}
void HistoryDock::updateControls() {
    if (clearToken_)
        progressToast(this)->showProgress(tr("History"), tr("Clearing history…"));
    else if (policyToken_)
        progressToast(this)->showProgress(tr("History"),
                                          tr("Saving or loading history preference…"));
    else if (listToken_)
        progressToast(this)->showProgress(tr("History"), tr("Loading history…"));
    else
        clearProgressToast(this);
    status_->setVisible(!status_->text().isEmpty());
    const bool idle = adapter_ && !listToken_ && !clearToken_;
    record_->setEnabled(adapter_ && havePolicy_ && !policyToken_ && !clearToken_);
    refresh_->setEnabled(idle && !policyToken_);
    clear_->setEnabled(idle && !policyToken_);
    previous_->setEnabled(idle && !visitedOffsets_.isEmpty());
    next_->setEnabled(idle && model_->rowCount() > 0 &&
                      offset_ <= std::numeric_limits<quint32>::max() - quint32(model_->rowCount()));
    open_->setEnabled(model_->entry(table_->currentIndex().row()) != nullptr &&
                      !table_->isRowHidden(table_->currentIndex().row()));
    if (model_->rowCount() > 0)
        page_->setText(
            tr("Rows %1–%2").arg(quint64(offset_) + 1).arg(quint64(offset_) + model_->rowCount()));
    else
        page_->setText(offset_ == 0 ? tr("No rows") : tr("After row %1").arg(offset_));
}
} // namespace choscordb
