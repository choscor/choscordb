#include "history_dock.h"
#include "models/history_model.h"
#include <QCheckBox>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableView>
#include <QVBoxLayout>
#include <atomic>
#include <limits>
namespace choscordb {
namespace {
quint64 token() {
    static std::atomic<quint64> next{quint64(1) << 60};
    return next.fetch_add(1);
}
constexpr quint32 pageSize = 100;
} // namespace
HistoryDock::HistoryDock(EngineAdapter* adapter, QWidget* parent)
    : QDockWidget(tr("Query history"), parent), adapter_(adapter) {
    setObjectName("historyDock");
    auto* body = new QWidget(this);
    auto* layout = new QVBoxLayout(body);
    auto* toolbar = new QHBoxLayout;
    record_ = new QCheckBox(tr("Record history"), body);
    record_->setObjectName("recordHistory");
    clear_ = new QPushButton(tr("Clear history…"), body);
    clear_->setObjectName("clearHistory");
    refresh_ = new QPushButton(tr("Refresh"), body);
    refresh_->setObjectName("refreshHistory");
    toolbar->addWidget(record_);
    toolbar->addStretch();
    toolbar->addWidget(clear_);
    toolbar->addWidget(refresh_);
    layout->addLayout(toolbar);
    status_ = new QLabel(body);
    status_->setObjectName("historyStatus");
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    layout->addWidget(status_);
    model_ = new HistoryModel(this);
    table_ = new QTableView(body);
    table_->setObjectName("historyTable");
    table_->horizontalHeader()->setMinimumSectionSize(76);
    table_->setModel(model_);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table_->setColumnWidth(0, 220);
    table_->setColumnWidth(1, 170);
    table_->setColumnWidth(3, 100);
    table_->setColumnWidth(4, 100);
    table_->setColumnWidth(5, 80);
    layout->addWidget(table_);
    previewNotice_ = new QLabel(body);
    previewNotice_->setObjectName("historyPreviewNotice");
    previewNotice_->setTextFormat(Qt::PlainText);
    previewNotice_->setWordWrap(true);
    auto* previewToolbar = new QHBoxLayout;
    previewToolbar->addWidget(previewNotice_, 1);
    previewPrevious_ = new QPushButton(tr("Earlier text"), body);
    previewPrevious_->setObjectName("historyPreviewPrevious");
    previewNext_ = new QPushButton(tr("Later text"), body);
    previewNext_->setObjectName("historyPreviewNext");
    previewToolbar->addWidget(previewPrevious_);
    previewToolbar->addWidget(previewNext_);
    layout->addLayout(previewToolbar);
    preview_ = new QPlainTextEdit(body);
    preview_->setObjectName("historyPreview");
    preview_->setReadOnly(true);
    layout->addWidget(preview_);
    auto* footer = new QHBoxLayout;
    previous_ = new QPushButton(tr("Previous"), body);
    next_ = new QPushButton(tr("Next"), body);
    next_->setObjectName("historyNext");
    previous_->setObjectName("historyPrevious");
    page_ = new QLabel(body);
    page_->setObjectName("historyRange");
    open_ = new QPushButton(tr("Open in new query"), body);
    open_->setObjectName("openHistoryQuery");
    footer->addWidget(previous_);
    footer->addWidget(next_);
    footer->addWidget(page_);
    footer->addStretch();
    footer->addWidget(open_);
    layout->addLayout(footer);
    setWidget(body);
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
        status_->setText(tr("Saving history preference…"));
        updateControls();
        adapter_->setHistoryPolicy(proposed, policyToken_);
    });
    connect(clear_, &QPushButton::clicked, this, [this] {
        if (!adapter_ ||
            QMessageBox::question(this, tr("Clear history"), tr("Delete all stored query history?"),
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) != QMessageBox::Yes)
            return;
        failed_ = false;
        clearToken_ = token();
        status_->setText(tr("Clearing history…"));
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
        status_->setText(tr("Loading history…"));
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
    previewNotice_->setText(
        partial ? tr("Preview truncated to part %1. Use Earlier text / Later text to read all SQL.")
                      .arg(previewOffsets_.size() + 1)
                : QString{});
    previewPrevious_->setEnabled(!previewOffsets_.isEmpty());
    previewNext_->setEnabled(entry && previewLength_ > 0 &&
                             previewOffset_ + previewLength_ < entry->sql.size());
}
void HistoryDock::openSelection() {
    if (const auto* entry = model_->entry(table_->currentIndex().row()))
        emit openRequested(*entry);
}
void HistoryDock::updateControls() {
    const bool idle = adapter_ && !listToken_ && !clearToken_;
    record_->setEnabled(adapter_ && havePolicy_ && !policyToken_ && !clearToken_);
    refresh_->setEnabled(idle && !policyToken_);
    clear_->setEnabled(idle && !policyToken_);
    previous_->setEnabled(idle && !visitedOffsets_.isEmpty());
    next_->setEnabled(idle && model_->rowCount() > 0 &&
                      offset_ <= std::numeric_limits<quint32>::max() - quint32(model_->rowCount()));
    open_->setEnabled(model_->entry(table_->currentIndex().row()) != nullptr);
    if (model_->rowCount() > 0)
        page_->setText(
            tr("Rows %1–%2").arg(quint64(offset_) + 1).arg(quint64(offset_) + model_->rowCount()));
    else
        page_->setText(offset_ == 0 ? tr("No rows") : tr("After row %1").arg(offset_));
}
} // namespace choscordb
