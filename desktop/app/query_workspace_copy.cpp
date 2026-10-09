#include "app/query_workspace.h"
#include "app/query_workspace_p.h"
#include "bridge/deferred_assembler.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include <QApplication>
#include <QClipboard>
#include <QFutureWatcher>
#include <QItemSelectionModel>
#include <QTableView>
#include <QTimer>
#include <QtConcurrentRun>
#include <algorithm>

namespace choscordb {
using query_workspace_detail::text;
namespace {
QString copyAssemblyError(const QString& code) {
    if (code == QLatin1String("too_large"))
        return QObject::tr("Complete values exceed the 8 MiB copy loading limit.");
    if (code == QLatin1String("invalid_utf8"))
        return QObject::tr("The complete text value is not valid UTF-8.");
    return QObject::tr("Invalid or stale complete value chunk.");
}
} // namespace

void QueryWorkspace::copyResult(int scope) {
    ++copyGeneration_;
    pendingCopy_.reset();
    if (widgets_.grid->model() != model_ || !widgets_.grid->selectionModel() ||
        widgets_.grid->selectionModel()->model() != model_) {
        message(tr("The result page is no longer available."));
        return;
    }
    auto selection = scope == 2 ? QItemSelection{} : widgets_.grid->selectionModel()->selection();
    if ((scope != 2 && selection.isEmpty()) || !model_->rowCount() || !model_->columnCount())
        return;
    PendingCopy request;
    request.scope = scope;
    request.selectionGeneration = selectionGeneration_;
    request.anchor = model_->index(0, 0);
    request.query = query_.value_or(0);
    request.deferred = model_->copyDeferredCells(selection, scope);
    if (request.deferred.empty()) {
        auto snapshot = model_->copySnapshot(selection, scope);
        if (snapshot)
            renderCopy(std::move(*snapshot), request.anchor, request.query,
                       request.selectionGeneration, request.scope, copyGeneration_);
        return;
    }
    request.selection = std::move(selection);
    if (!query_ || !queryAvailable() || !adapter_)
        return failCopy(tr("The result is no longer available for complete value loading."));
    pendingCopy_ = std::move(request);
    requestCopyChunk();
}

void QueryWorkspace::failCopy(const QString& error) {
    ++copyGeneration_;
    pendingCopy_.reset();
    message(error.left(1024));
}

void QueryWorkspace::renderCopy(ResultTableModel::CopySnapshot snapshot,
                                QPersistentModelIndex anchor, quint64 query,
                                quint64 selectionGeneration, int scope, quint64 generation) {
    auto* watcher = new QFutureWatcher<ResultTableModel::CopyEvaluation>(this);
    connect(watcher, &QFutureWatcher<ResultTableModel::CopyEvaluation>::finished, this,
            [this, watcher, anchor, query, selectionGeneration, scope, generation] {
                const auto result = watcher->result();
                watcher->deleteLater();
                if (generation != copyGeneration_)
                    return;
                if (!anchor.isValid() || widgets_.grid->model() != model_ ||
                    !widgets_.grid->selectionModel() ||
                    widgets_.grid->selectionModel()->model() != model_ ||
                    query_.value_or(0) != query ||
                    (scope != 2 && selectionGeneration != selectionGeneration_))
                    return failCopy(tr("The result changed before copying completed."));
                if (!result.error.isEmpty())
                    message(result.error);
                else
                    QApplication::clipboard()->setText(result.text);
            });
    watcher->setFuture(QtConcurrent::run([snapshot = std::move(snapshot)]() mutable {
        return ResultTableModel::evaluateCopy(std::move(snapshot));
    }));
}

void QueryWorkspace::requestCopyChunk() {
    if (!pendingCopy_)
        return;
    auto& request = *pendingCopy_;
    if (request.assembling)
        return;
    if (!request.anchor.isValid() || widgets_.grid->model() != model_ ||
        !widgets_.grid->selectionModel() || widgets_.grid->selectionModel()->model() != model_ ||
        query_ != request.query || !queryAvailable() || !adapter_)
        return failCopy(tr("The result changed before copying completed."));
    if (request.next == request.deferred.size()) {
        if (request.scope != 2 && request.selectionGeneration != selectionGeneration_)
            return failCopy(tr("The selection changed before copying completed."));
        auto snapshot = model_->copySnapshot(request.selection, request.scope, request.resolved);
        const auto anchor = request.anchor;
        const auto query = request.query;
        const auto selectionGeneration = request.selectionGeneration;
        const auto scope = request.scope;
        pendingCopy_.reset();
        if (!snapshot)
            return failCopy(tr("The result changed before copying completed."));
        renderCopy(std::move(*snapshot), anchor, query, selectionGeneration, scope,
                   copyGeneration_);
        return;
    }
    const auto [row, column] = request.deferred[request.next];
    const auto value = model_->deferredValue(model_->index(row, column));
    if (!value)
        return failCopy(tr("The complete value is no longer available."));
    if (!request.assembler) {
        request.assembling = true;
        const auto generation = copyGeneration_;
        const auto position = request.next;
        auto* watcher =
            new QFutureWatcher<std::pair<std::shared_ptr<DeferredAssemblerJob>, QString>>(this);
        connect(
            watcher,
            &QFutureWatcher<std::pair<std::shared_ptr<DeferredAssemblerJob>, QString>>::finished,
            this, [this, watcher, generation, position] {
                const auto [job, error] = watcher->result();
                watcher->deleteLater();
                if (generation != copyGeneration_ || !pendingCopy_ ||
                    pendingCopy_->next != position)
                    return;
                auto& current = *pendingCopy_;
                current.assembling = false;
                if (!error.isEmpty())
                    return failCopy(copyAssemblyError(error));
                current.assembler = job;
                QTimer::singleShot(0, this, &QueryWorkspace::requestCopyChunk);
            });
        watcher->setFuture(QtConcurrent::run([value = *value, resolved = request.resolvedBytes] {
            auto job = std::make_shared<DeferredAssemblerJob>(value.type, value.fallback,
                                                              value.bytes, resolved, false);
            return std::pair{job, job->initialError()};
        }));
        return;
    }
    adapter_->loadValueChunk(request.query, value->handle, request.offset);
}

void QueryWorkspace::handleCopyEvent(const BridgeEvent& event) {
    if (!pendingCopy_ || pendingCopy_->next >= pendingCopy_->deferred.size() ||
        pendingCopy_->query != event.id)
        return;
    auto& request = *pendingCopy_;
    const auto [row, column] = request.deferred[request.next];
    const auto value = model_->deferredValue(model_->index(row, column));
    if (!value || value->handle != event.value_handle || request.offset != event.chunk_offset)
        return;
    const auto eventKind = text(event.kind);
    if (eventKind == "value_chunk_failed")
        return failCopy(text(event.error));
    if (eventKind != "value_chunk" || request.assembling || !request.assembler)
        return;
    const auto kind = text(event.chunk_kind);
    const auto size = static_cast<quint64>(event.chunk_bytes.size());
    if (!request.anchor.isValid() || query_ != request.query)
        return failCopy(tr("The result changed before copying completed."));
    if (event.has_lease && (!adapter_ || !adapter_->retainTransfer(event.lease_id, size)))
        return failCopy(tr("The complete value exceeds the available memory budget."));
    QByteArray chunk(reinterpret_cast<const char*>(event.chunk_bytes.data()),
                     static_cast<qsizetype>(size));
    if (event.has_lease)
        adapter_->releasePageLease(event.lease_id);
    request.assembling = true;
    const auto generation = copyGeneration_;
    const auto position = request.next;
    const auto totalBytes = event.total_bytes;
    auto job = request.assembler;
    auto* watcher = new QFutureWatcher<DeferredAssemblyOutcome>(this);
    connect(watcher, &QFutureWatcher<DeferredAssemblyOutcome>::finished, this,
            [this, watcher, generation, position, job, row, column, totalBytes] {
                const auto result = watcher->result();
                watcher->deleteLater();
                if (generation != copyGeneration_ || !pendingCopy_ ||
                    pendingCopy_->next != position || pendingCopy_->assembler != job)
                    return;
                auto& current = *pendingCopy_;
                current.assembling = false;
                if (!result.error.isEmpty())
                    return failCopy(copyAssemblyError(result.error));
                current.offset = result.receivedBytes;
                if (result.complete) {
                    if (result.kind == QLatin1String("fallback_text"))
                        current.resolved[{row, column}] =
                            FallbackText{result.text, result.databaseType};
                    else if (result.kind == QLatin1String("text"))
                        current.resolved[{row, column}] = result.text;
                    else
                        current.resolved[{row, column}] = result.bytes;
                    current.resolvedBytes += totalBytes;
                    current.assembler.reset();
                    current.offset = 0;
                    ++current.next;
                }
                QTimer::singleShot(0, this, &QueryWorkspace::requestCopyChunk);
            });
    watcher->setFuture(QtConcurrent::run([job, kind, offset = event.chunk_offset, totalBytes,
                                          chunk = std::move(chunk), hasLease = event.has_lease] {
        return job->append(kind, offset, totalBytes, chunk, hasLease);
    }));
}
} // namespace choscordb
