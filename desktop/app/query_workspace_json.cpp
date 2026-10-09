#include "app/query_workspace.h"
#include "app/query_workspace_p.h"
#include "bridge/deferred_assembler.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/button/button.h"
#include "design_system/json_text_view/json_text_view.h"
#include "design_system/metrics/metrics.h"
#include "design_system/right_sheet/right_sheet.h"
#include "design_system/text/text.h"
#include "widgets/export_dialog/export_dialog.h"
#include "widgets/value_detail_dialog/value_detail_dialog.h"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QFutureWatcher>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSemaphore>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrentRun>
#include <atomic>
#include <memory>

namespace choscordb {
using query_workspace_detail::text;
namespace {
QString jsonAssemblyError(const QString& code) {
    if (code == QLatin1String("too_large"))
        return QObject::tr("The complete JSON view exceeds the 8 MiB loading limit.");
    if (code == QLatin1String("invalid_utf8"))
        return QObject::tr("The complete text value is not valid UTF-8.");
    return QObject::tr("Invalid or oversized value chunk.");
}
QSemaphore& jsonMenuSlots() {
    static QSemaphore permits(2);
    return permits;
}
bool jsonActionAvailable(ResultTableModel::JsonViewState state, bool deferredAvailable) {
    return state == ResultTableModel::JsonViewState::Ready ||
           state == ResultTableModel::JsonViewState::Invalid ||
           (state == ResultTableModel::JsonViewState::NeedsDeferred && deferredAvailable);
}
} // namespace
bool QueryWorkspace::jsonResultCurrent() const {
    if (jsonResultInvalidated_)
        return false;
    return !query_ || (currentPage_ && queryAvailable() && !fetching_ && !viewBusy_);
}
void QueryWorkspace::appendJsonViewActions(QMenu& menu, const QPersistentModelIndex& clicked,
                                           bool current) {
    current = current && jsonResultCurrent();
    const bool validTarget = current && clicked.isValid() && clicked.model() == model_ &&
                             clicked.row() >= 0 && clicked.row() < model_->rowCount();
    auto* viewCell = menu.addAction(tr("View cell as JSON"));
    viewCell->setObjectName("viewCellJson");
    viewCell->setEnabled(false);
    connect(viewCell, &QAction::triggered, this, [this, clicked] {
        if (clicked.isValid() && clicked.model() == model_ && widgets_.grid->model() == model_ &&
            widgets_.grid->selectionModel() && widgets_.grid->selectionModel()->model() == model_)
            openJsonView(JsonViewMode::Cell, clicked.row(), clicked.column());
    });
    auto* viewRow = menu.addAction(tr("View row as JSON"));
    viewRow->setObjectName("viewRowJson");
    viewRow->setEnabled(false);
    if (validTarget) {
        QPointer<QMenu> menuGuard(&menu);
        QPointer<QueryWorkspace> owner(this);
        QPointer<QAction> cellGuard(viewCell), rowGuard(viewRow);
        auto cancelled = std::make_shared<std::atomic_bool>(false);
        connect(&menu, &QMenu::aboutToHide, this,
                [cancelled] { cancelled->store(true, std::memory_order_relaxed); });
        auto attempt = std::make_shared<std::function<void()>>();
        std::weak_ptr<std::function<void()>> weakAttempt = attempt;
        *attempt = [this, owner, menuGuard, cellGuard, rowGuard, clicked, cancelled, weakAttempt] {
            if (!owner || !menuGuard || !cellGuard || !rowGuard ||
                cancelled->load(std::memory_order_relaxed) || !clicked.isValid() ||
                !jsonResultCurrent())
                return;
            if (!jsonMenuSlots().tryAcquire()) {
                if (auto retry = weakAttempt.lock())
                    QTimer::singleShot(20, menuGuard.data(), [retry] { (*retry)(); });
                return;
            }
            auto cell = model_->jsonViewSnapshot(ResultTableModel::JsonViewScope::Cell,
                                                 clicked.row(), clicked.column());
            auto row = model_->jsonViewSnapshot(ResultTableModel::JsonViewScope::Row, clicked.row(),
                                                clicked.column());
            if (!cell || !row) {
                jsonMenuSlots().release();
                return;
            }
            using Evaluations = std::pair<ResultTableModel::JsonViewEvaluation,
                                          ResultTableModel::JsonViewEvaluation>;
            auto* watcher = new QFutureWatcher<Evaluations>(this);
            connect(watcher, &QFutureWatcher<Evaluations>::finished, this,
                    [this, watcher, cellGuard, rowGuard, clicked, cancelled] {
                        const auto result = watcher->result();
                        watcher->deleteLater();
                        if (!cellGuard || !rowGuard || cancelled->load(std::memory_order_relaxed) ||
                            !clicked.isValid() || clicked.model() != model_ ||
                            widgets_.grid->model() != model_ || !jsonResultCurrent())
                            return;
                        const bool deferredAvailable = query_ && queryAvailable();
                        cellGuard->setEnabled(
                            jsonActionAvailable(result.first.state, deferredAvailable));
                        rowGuard->setEnabled(
                            jsonActionAvailable(result.second.state, deferredAvailable));
                    });
            watcher->setFuture(QtConcurrent::run(
                [cell = std::move(*cell), row = std::move(*row), cancelled]() mutable {
                    Evaluations result;
                    if (!cancelled->load(std::memory_order_relaxed))
                        result.first = ResultTableModel::evaluateJsonView(std::move(cell));
                    if (!cancelled->load(std::memory_order_relaxed))
                        result.second = ResultTableModel::evaluateJsonView(std::move(row));
                    jsonMenuSlots().release();
                    return result;
                }));
        };
        QTimer::singleShot(0, &menu, [attempt] { (*attempt)(); });
    }
    connect(viewRow, &QAction::triggered, this, [this, clicked] {
        if (clicked.isValid() && clicked.model() == model_ && widgets_.grid->model() == model_ &&
            widgets_.grid->selectionModel() && widgets_.grid->selectionModel()->model() == model_)
            openJsonView(JsonViewMode::Row, clicked.row());
    });
    const QPersistentModelIndex pageMarker =
        current && model_->rowCount() > 0 && model_->columnCount() > 0
            ? QPersistentModelIndex(model_->index(0, 0))
            : QPersistentModelIndex{};
    auto* viewTable = menu.addAction(tr("View table as JSON"));
    viewTable->setObjectName("viewTableJson");
    viewTable->setEnabled(pageMarker.isValid());
    connect(viewTable, &QAction::triggered, this, [this, pageMarker] {
        if (pageMarker.isValid() && pageMarker.model() == model_ &&
            widgets_.grid->model() == model_ && widgets_.grid->selectionModel() &&
            widgets_.grid->selectionModel()->model() == model_)
            openJsonView(JsonViewMode::Table);
    });
}
void QueryWorkspace::clearResult() {
    clearRowJson();
    ++copyGeneration_;
    pendingCopy_.reset();
    if (export_)
        export_->clearQuery();
    if (detail_)
        detail_->clearValue();
    model_->setPage({}, {}, 0);
    cellMetadata_.clear();
    cellMetadataToken_ = 0;
    cellMetadataQuery_.reset();
    if (visibleLease_) {
        const auto lease = *visibleLease_;
        visibleLease_.reset();
        if (adapter_)
            adapter_->releasePageLease(lease);
    }
    std::vector<ResultColumn>().swap(columns_);
    if (schemaLease_) {
        const auto lease = *schemaLease_;
        schemaLease_.reset();
        if (adapter_)
            adapter_->releasePageLease(lease);
    }
}
void QueryWorkspace::clearRowJson() {
    ++rowJsonGeneration_;
    rowJsonRendering_ = false;
    rowJsonIndex_ = QPersistentModelIndex{};
    rowJsonMode_ = JsonViewMode::Row;
    rowJsonQuery_.reset();
    rowJsonResolved_.clear();
    rowJsonAssembler_.reset();
    rowJsonAssemblyPending_ = false;
    rowJsonLoadingColumn_ = -1;
    rowJsonLoadingRow_ = -1;
    rowJsonScanRow_ = -1;
    rowJsonScanColumn_ = -1;
    rowJsonLoadingHandle_ = rowJsonLoadingOffset_ = 0;
    rowJsonResolvedBytes_ = 0;
    if (rowJsonCopy_)
        rowJsonCopy_->setEnabled(false);
    if (rowJsonText_)
        rowJsonText_->clear();
    if (rowJsonStatus_)
        rowJsonStatus_->clear();
    if (rowJsonSheet_ && rowJsonSheet_->isVisible())
        rowJsonSheet_->reject();
}
void QueryWorkspace::scheduleJsonViewEvaluation(bool afterDeferred) {
    if (!rowJsonIndex_.isValid() || rowJsonIndex_.model() != model_) {
        if (afterDeferred)
            failRowJson(tr("This result is no longer available."));
        else
            rowJsonStatus_->setText(tr("This result is no longer available."));
        return;
    }
    const auto scope = rowJsonMode_ == JsonViewMode::Cell    ? ResultTableModel::JsonViewScope::Cell
                       : rowJsonMode_ == JsonViewMode::Table ? ResultTableModel::JsonViewScope::Page
                                                             : ResultTableModel::JsonViewScope::Row;
    auto snapshot = model_->jsonViewSnapshot(scope, rowJsonIndex_.row(), rowJsonIndex_.column(),
                                             rowJsonResolved_);
    if (!snapshot) {
        if (afterDeferred)
            failRowJson(tr("This result is no longer available."));
        else
            rowJsonStatus_->setText(tr("This result is no longer available."));
        return;
    }
    rowJsonRendering_ = true;
    const auto generation = ++rowJsonGeneration_;
    auto* watcher = new QFutureWatcher<ResultTableModel::JsonViewEvaluation>(this);
    connect(
        watcher, &QFutureWatcher<ResultTableModel::JsonViewEvaluation>::finished, this,
        [this, watcher, generation, afterDeferred] {
            const auto result = watcher->result();
            watcher->deleteLater();
            if (generation != rowJsonGeneration_ || !rowJsonSheet_ || !rowJsonSheet_->isVisible())
                return;
            rowJsonRendering_ = false;
            if (!jsonResultCurrent() || !rowJsonIndex_.isValid() ||
                rowJsonIndex_.model() != model_) {
                failRowJson(tr("This result is no longer available."));
                return;
            }
            if (result.state == ResultTableModel::JsonViewState::Ready) {
                rowJsonText_->setPlainText(result.json);
                rowJsonCopy_->setEnabled(true);
                rowJsonStatus_->clear();
                rowJsonStatus_->hide();
                rowJsonQuery_.reset();
                rowJsonResolved_.clear();
            } else if (!afterDeferred &&
                       result.state == ResultTableModel::JsonViewState::NeedsDeferred && query_ &&
                       queryAvailable()) {
                rowJsonQuery_ = query_;
                rowJsonStatus_->setText(tr("Loading complete JSON values…"));
                requestRowJsonChunk();
            } else if (afterDeferred) {
                failRowJson(result.error.isEmpty() ? tr("The JSON cannot be read.") : result.error);
            } else {
                rowJsonStatus_->setText(result.error.isEmpty() ? tr("The JSON cannot be read.")
                                                               : result.error);
            }
        });
    watcher->setFuture(QtConcurrent::run([snapshot = std::move(*snapshot)]() mutable {
        return ResultTableModel::evaluateJsonView(std::move(snapshot));
    }));
}
void QueryWorkspace::openJsonView(JsonViewMode mode, int row, int column) {
    if (!jsonResultCurrent() || widgets_.grid->model() != model_ ||
        !widgets_.grid->selectionModel() || widgets_.grid->selectionModel()->model() != model_ ||
        row < 0 || row >= model_->rowCount() || column < 0 || column >= model_->columnCount())
        return;
    const auto target = model_->index(row, mode == JsonViewMode::Cell ? column : 0);
    if (!target.isValid())
        return;
    clearRowJson();
    if (!rowJsonSheet_) {
        rowJsonSheet_ = new design::RightSheet(widgets_.dialogParent);
        rowJsonSheet_->setObjectName("rowJsonSheet");
        auto* body = new QWidget(rowJsonSheet_);
        body->setAccessibleName(tr("JSON content"));
        auto* layout = new QVBoxLayout(body);
        layout->setContentsMargins(
            design::spacing(design::Spacing::Four), design::spacing(design::Spacing::Three),
            design::spacing(design::Spacing::Four), design::spacing(design::Spacing::Three));
        layout->setSpacing(design::spacing(design::Spacing::Two));
        rowJsonStatus_ = new design::Text({}, body);
        rowJsonStatus_->setObjectName("rowJsonStatus");
        rowJsonStatus_->setAccessibleName(tr("Row JSON status"));
        rowJsonStatus_->setWordWrap(true);
        rowJsonText_ = new design::JsonTextView(body);
        rowJsonText_->setObjectName("rowJsonText");
        rowJsonText_->setAccessibleName(tr("Row JSON text"));
        layout->addWidget(rowJsonStatus_);
        layout->addWidget(rowJsonText_, 1);
        rowJsonSheet_->setBody(body);
        rowJsonCopy_ = new design::Button(tr("Copy JSON"), rowJsonSheet_);
        rowJsonCopy_->setObjectName("rowJsonCopy");
        rowJsonCopy_->setAccessibleName(tr("Copy JSON"));
        rowJsonCopy_->setEnabled(false);
        rowJsonSheet_->footerLayout()->addWidget(rowJsonCopy_);
        connect(rowJsonCopy_, &QPushButton::clicked, this, [this] {
            if (rowJsonCopy_ && rowJsonCopy_->isEnabled() && rowJsonText_)
                QApplication::clipboard()->setText(rowJsonText_->toPlainText());
        });
        connect(rowJsonSheet_, &QDialog::finished, this, [this] { clearRowJson(); });
    }
    rowJsonMode_ = mode;
    rowJsonIndex_ = target;
    rowJsonScanRow_ = mode == JsonViewMode::Table ? 0 : row;
    rowJsonScanColumn_ = mode == JsonViewMode::Cell ? column : 0;
    const QString title = mode == JsonViewMode::Cell    ? tr("View cell as JSON")
                          : mode == JsonViewMode::Table ? tr("View table as JSON")
                                                        : tr("View row as JSON");
    rowJsonSheet_->setTitle(title);
    rowJsonSheet_->setAccessibleName(title);
    rowJsonText_->setAccessibleName(mode == JsonViewMode::Row ? tr("Row JSON text") : title);
    rowJsonStatus_->setAccessibleName(mode == JsonViewMode::Row ? tr("Row JSON status")
                                                                : tr("JSON view status"));
    rowJsonStatus_->show();
    rowJsonStatus_->setText(tr("Preparing JSON…"));
    rowJsonSheet_->open();
    scheduleJsonViewEvaluation(false);
}
void QueryWorkspace::requestRowJsonChunk() {
    if (rowJsonRendering_ || rowJsonAssemblyPending_)
        return;
    if (!rowJsonSheet_ || !rowJsonSheet_->isVisible() || !rowJsonQuery_ ||
        !rowJsonIndex_.isValid() || rowJsonIndex_.model() != model_ || query_ != rowJsonQuery_ ||
        !queryAvailable()) {
        if (rowJsonQuery_)
            failRowJson(tr("The result page is no longer available."));
        return;
    }
    const int firstRow = rowJsonMode_ == JsonViewMode::Table ? 0 : rowJsonIndex_.row();
    const int lastRow = rowJsonMode_ == JsonViewMode::Table ? model_->rowCount() : firstRow + 1;
    const int firstColumn = rowJsonMode_ == JsonViewMode::Cell ? rowJsonIndex_.column() : 0;
    const int lastColumn =
        rowJsonMode_ == JsonViewMode::Cell ? firstColumn + 1 : model_->columnCount();
    if (rowJsonLoadingColumn_ < 0) {
        while (rowJsonScanRow_ < lastRow) {
            if (rowJsonScanColumn_ >= lastColumn) {
                ++rowJsonScanRow_;
                rowJsonScanColumn_ = firstColumn;
                continue;
            }
            const int row = rowJsonScanRow_;
            const int column = rowJsonScanColumn_++;
            const auto value = model_->deferredValue(model_->index(row, column));
            if (!value)
                continue;
            rowJsonLoadingRow_ = row;
            rowJsonLoadingColumn_ = column;
            rowJsonLoadingHandle_ = value->handle;
            rowJsonLoadingOffset_ = 0;
            break;
        }
    }
    if (rowJsonLoadingColumn_ < 0) {
        scheduleJsonViewEvaluation(true);
        return;
    }
    if (!rowJsonAssembler_) {
        const auto deferred =
            model_->deferredValue(model_->index(rowJsonLoadingRow_, rowJsonLoadingColumn_));
        if (!deferred)
            return failRowJson(tr("The result page is no longer available."));
        rowJsonAssemblyPending_ = true;
        const auto generation = rowJsonGeneration_;
        auto* watcher =
            new QFutureWatcher<std::pair<std::shared_ptr<DeferredAssemblerJob>, QString>>(this);
        connect(
            watcher,
            &QFutureWatcher<std::pair<std::shared_ptr<DeferredAssemblerJob>, QString>>::finished,
            this, [this, watcher, generation] {
                const auto [job, error] = watcher->result();
                watcher->deleteLater();
                if (generation != rowJsonGeneration_ || !rowJsonQuery_ || !rowJsonSheet_ ||
                    !rowJsonSheet_->isVisible())
                    return;
                rowJsonAssemblyPending_ = false;
                if (!error.isEmpty())
                    return failRowJson(jsonAssemblyError(error));
                rowJsonAssembler_ = job;
                QTimer::singleShot(0, this, &QueryWorkspace::requestRowJsonChunk);
            });
        watcher->setFuture(
            QtConcurrent::run([deferred = *deferred, resolved = rowJsonResolvedBytes_] {
                auto job = std::make_shared<DeferredAssemblerJob>(deferred.type, deferred.fallback,
                                                                  deferred.bytes, resolved, true);
                return std::pair{job, job->initialError()};
            }));
        return;
    }
    rowJsonStatus_->setText(tr("Loading complete JSON values… %1 bytes")
                                .arg(rowJsonResolvedBytes_ + rowJsonLoadingOffset_));
    adapter_->loadValueChunk(*rowJsonQuery_, rowJsonLoadingHandle_, rowJsonLoadingOffset_);
}
void QueryWorkspace::failRowJson(const QString& error) {
    ++rowJsonGeneration_;
    rowJsonRendering_ = false;
    rowJsonQuery_.reset();
    rowJsonResolved_.clear();
    rowJsonAssembler_.reset();
    rowJsonAssemblyPending_ = false;
    rowJsonLoadingColumn_ = -1;
    rowJsonLoadingRow_ = -1;
    rowJsonResolvedBytes_ = 0;
    if (rowJsonText_)
        rowJsonText_->clear();
    if (rowJsonCopy_)
        rowJsonCopy_->setEnabled(false);
    if (rowJsonStatus_)
        rowJsonStatus_->setText(tr("Unable to show complete JSON: %1").arg(error.left(1024)));
}
void QueryWorkspace::handleRowJsonEvent(const BridgeEvent& event) {
    if (!rowJsonQuery_ || rowJsonLoadingColumn_ < 0 || event.id != *rowJsonQuery_ ||
        event.value_handle != rowJsonLoadingHandle_ ||
        event.chunk_offset != rowJsonLoadingOffset_ || !rowJsonSheet_ ||
        !rowJsonSheet_->isVisible())
        return;
    const auto kind = text(event.kind);
    if (kind == "value_chunk_failed") {
        failRowJson(text(event.error));
        return;
    }
    if (kind != "value_chunk" || rowJsonAssemblyPending_ || !rowJsonAssembler_)
        return;
    const auto chunkKind = text(event.chunk_kind);
    const quint64 chunkSize = event.chunk_bytes.size();
    if (event.has_lease && (!adapter_ || !adapter_->retainTransfer(event.lease_id, chunkSize))) {
        failRowJson(tr("The value exceeds the available memory budget."));
        return;
    }
    QByteArray chunk(reinterpret_cast<const char*>(event.chunk_bytes.data()),
                     static_cast<qsizetype>(chunkSize));
    if (event.has_lease)
        adapter_->releasePageLease(event.lease_id);
    rowJsonAssemblyPending_ = true;
    const auto generation = rowJsonGeneration_;
    const auto row = rowJsonLoadingRow_, column = rowJsonLoadingColumn_;
    const auto totalBytes = event.total_bytes;
    auto job = rowJsonAssembler_;
    auto* watcher = new QFutureWatcher<DeferredAssemblyOutcome>(this);
    connect(watcher, &QFutureWatcher<DeferredAssemblyOutcome>::finished, this,
            [this, watcher, generation, job, row, column, totalBytes] {
                const auto result = watcher->result();
                watcher->deleteLater();
                if (generation != rowJsonGeneration_ || job != rowJsonAssembler_ ||
                    !rowJsonQuery_ || !rowJsonSheet_ || !rowJsonSheet_->isVisible())
                    return;
                rowJsonAssemblyPending_ = false;
                if (!queryAvailable() || query_ != rowJsonQuery_)
                    return failRowJson(tr("The result page is no longer available."));
                if (!result.error.isEmpty())
                    return failRowJson(jsonAssemblyError(result.error));
                rowJsonLoadingOffset_ = result.receivedBytes;
                if (result.complete) {
                    if (result.kind == QLatin1String("fallback_text"))
                        rowJsonResolved_[{row, column}] =
                            FallbackText{result.text, result.databaseType};
                    else if (result.kind == QLatin1String("text"))
                        rowJsonResolved_[{row, column}] = result.text;
                    else
                        rowJsonResolved_[{row, column}] = result.bytes;
                    rowJsonResolvedBytes_ += totalBytes;
                    rowJsonAssembler_.reset();
                    rowJsonLoadingColumn_ = rowJsonLoadingRow_ = -1;
                    rowJsonLoadingOffset_ = 0;
                }
                QTimer::singleShot(0, this, &QueryWorkspace::requestRowJsonChunk);
            });
    watcher->setFuture(QtConcurrent::run([job, chunkKind, offset = event.chunk_offset, totalBytes,
                                          chunk = std::move(chunk), hasLease = event.has_lease] {
        return job->append(chunkKind, offset, totalBytes, chunk, hasLease);
    }));
}
} // namespace choscordb
