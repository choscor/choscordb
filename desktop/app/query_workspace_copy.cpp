#include "app/query_workspace.h"
#include "app/query_workspace_p.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include <QApplication>
#include <QClipboard>
#include <QItemSelectionModel>
#include <QStringDecoder>
#include <QTableView>
#include <QTimer>
#include <algorithm>

namespace choscordb {
using query_workspace_detail::text;
namespace {
constexpr quint64 CopyLoadLimit = 8 * 1024 * 1024;

QModelIndexList sortedSelection(QModelIndexList selection) {
    std::sort(selection.begin(), selection.end(), [](const auto& left, const auto& right) {
        return left.row() == right.row() ? left.column() < right.column()
                                         : left.row() < right.row();
    });
    selection.erase(std::unique(selection.begin(), selection.end()), selection.end());
    return selection;
}
} // namespace

void QueryWorkspace::copyResult(int scope) {
    pendingCopy_.reset();
    if (widgets_.grid->model() != model_ || !widgets_.grid->selectionModel() ||
        widgets_.grid->selectionModel()->model() != model_) {
        message(tr("The result page is no longer available."));
        return;
    }
    auto selection = scope == 2
                         ? QModelIndexList{}
                         : sortedSelection(widgets_.grid->selectionModel()->selectedIndexes());
    if ((scope != 2 && selection.isEmpty()) || !model_->rowCount() || !model_->columnCount())
        return;
    PendingCopy request;
    request.scope = scope;
    request.selection = selection;
    request.anchor = model_->index(0, 0);
    request.query = query_.value_or(0);
    std::vector<std::pair<int, int>> positions;
    if (scope == 0) {
        for (const auto& index : selection)
            positions.emplace_back(index.row(), index.column());
    } else if (scope == 1) {
        int previousRow = -1;
        for (const auto& index : selection) {
            if (index.row() == previousRow)
                continue;
            previousRow = index.row();
            for (int column = 0; column < model_->columnCount(); ++column)
                positions.emplace_back(index.row(), column);
        }
    } else {
        for (int row = 0; row < model_->rowCount(); ++row)
            for (int column = 0; column < model_->columnCount(); ++column)
                positions.emplace_back(row, column);
    }
    quint64 deferredBytes = 0;
    for (const auto [row, column] : positions) {
        const auto value = model_->cellValue(model_->index(row, column));
        if (!value)
            return failCopy(tr("The result page changed before copying."));
        if (const auto* unavailable = std::get_if<UnavailableValue>(&*value))
            return failCopy(tr("Cannot copy unavailable %1 value: %2")
                                .arg(unavailable->databaseType, unavailable->reason));
        if (const auto* deferred = std::get_if<DeferredValue>(&*value)) {
            if (deferred->bytes > CopyLoadLimit - deferredBytes)
                return failCopy(tr("Complete values exceed the 8 MiB copy loading limit."));
            deferredBytes += deferred->bytes;
            request.deferred.emplace_back(row, column);
        }
    }
    if (request.deferred.empty()) {
        QString error;
        const auto copied = scope == 0   ? model_->copyCells(selection, &error)
                            : scope == 1 ? model_->copyRows(selection, &error)
                                         : model_->copyPage(&error);
        if (!error.isEmpty())
            message(error);
        else
            QApplication::clipboard()->setText(copied);
        return;
    }
    if (!query_ || !queryAvailable() || !adapter_)
        return failCopy(tr("The result is no longer available for complete value loading."));
    pendingCopy_ = std::move(request);
    requestCopyChunk();
}

void QueryWorkspace::failCopy(const QString& error) {
    pendingCopy_.reset();
    message(error.left(1024));
}

void QueryWorkspace::requestCopyChunk() {
    if (!pendingCopy_)
        return;
    auto& request = *pendingCopy_;
    if (!request.anchor.isValid() || widgets_.grid->model() != model_ ||
        !widgets_.grid->selectionModel() || widgets_.grid->selectionModel()->model() != model_ ||
        query_ != request.query || !queryAvailable() || !adapter_)
        return failCopy(tr("The result changed before copying completed."));
    if (request.next == request.deferred.size()) {
        if (request.scope != 2 &&
            sortedSelection(widgets_.grid->selectionModel()->selectedIndexes()) !=
                request.selection)
            return failCopy(tr("The selection changed before copying completed."));
        QString error;
        const auto copied =
            request.scope == 0   ? model_->copyCells(request.selection, &error, request.resolved)
            : request.scope == 1 ? model_->copyRows(request.selection, &error, request.resolved)
                                 : model_->copyPage(&error, request.resolved);
        pendingCopy_.reset();
        if (!error.isEmpty())
            message(error);
        else
            QApplication::clipboard()->setText(copied);
        return;
    }
    const auto [row, column] = request.deferred[request.next];
    const auto value = model_->deferredValue(model_->index(row, column));
    if (!value || value->bytes > CopyLoadLimit - request.resolvedBytes)
        return failCopy(tr("The complete value is no longer available within the copy limit."));
    adapter_->loadValueChunk(request.query, value->handle, request.offset, 65536);
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
    if (eventKind != "value_chunk")
        return;
    const auto kind = text(event.chunk_kind);
    const auto size = static_cast<quint64>(event.chunk_bytes.size());
    if (!request.anchor.isValid() || query_ != request.query || !event.has_lease ||
        (kind != "text" && kind != "binary") || (value->fallback && kind != "text") ||
        (!request.kind.isEmpty() && request.kind != kind) || event.total_bytes != value->bytes ||
        size > 65536 || request.offset > value->bytes || size > value->bytes - request.offset ||
        (size == 0 && request.offset < value->bytes))
        return failCopy(tr("Invalid or stale complete value chunk."));
    if (!adapter_ || !adapter_->retainTransfer(event.lease_id, size))
        return failCopy(tr("The complete value exceeds the available memory budget."));
    if (size)
        request.bytes.append(reinterpret_cast<const char*>(event.chunk_bytes.data()),
                             static_cast<qsizetype>(size));
    adapter_->releasePageLease(event.lease_id);
    request.kind = kind;
    request.offset += size;
    if (request.offset == value->bytes) {
        Cell complete;
        if (kind == "text") {
            QStringDecoder decoder(QStringDecoder::Utf8);
            auto decoded = decoder(QByteArrayView(request.bytes));
            if (decoder.hasError())
                return failCopy(tr("The complete text value is not valid UTF-8."));
            complete = value->fallback ? Cell{FallbackText{std::move(decoded), value->type}}
                                       : Cell{std::move(decoded)};
        } else {
            complete = std::move(request.bytes);
        }
        request.resolved[{row, column}] = std::move(complete);
        request.resolvedBytes += value->bytes;
        request.bytes.clear();
        request.kind.clear();
        request.offset = 0;
        ++request.next;
    }
    QTimer::singleShot(0, this, &QueryWorkspace::requestCopyChunk);
}
} // namespace choscordb
