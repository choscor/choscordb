#include "app/query_settings.h"
#include "app/query_workspace.h"
#include "app/query_workspace_p.h"
#include "app/result_filter_bar.h"
#include "bridge/engine_adapter.h"
#include "bridge/result_column_adapter.h"
#include "widgets/export_dialog/export_dialog.h"
#include "widgets/sql_editor/sql_editor.h"
#include "widgets/value_detail_dialog/value_detail_dialog.h"

#include <QAction>
#include <QLabel>
#include <QSignalBlocker>
#include <QTableView>
#include <QVariantMap>
#include <QtConcurrent/QtConcurrentRun>
#include <string_view>
#include <utility>
namespace choscordb {
using query_workspace_detail::nextEditRequestToken;
using query_workspace_detail::text;
namespace {
Cell cell(const CellDto& value) {
    switch (value.kind) {
    case CellKindDto::Null:
        return std::monostate{};
    case CellKindDto::Boolean:
        return value.boolean;
    case CellKindDto::Integer:
        return static_cast<qint64>(value.integer);
    case CellKindDto::Real:
        return value.real;
    case CellKindDto::Decimal:
        return DecimalValue{text(value.text)};
    case CellKindDto::Binary:
        return QByteArray(reinterpret_cast<const char*>(value.bytes.data()),
                          static_cast<qsizetype>(value.bytes.size()));
    case CellKindDto::Deferred:
        return DeferredValue{value.handle, value.byte_length, text(value.database_type)};
    case CellKindDto::FallbackText:
        return FallbackText{text(value.text), text(value.database_type)};
    case CellKindDto::DeferredFallback:
        return DeferredValue{value.handle, value.byte_length, text(value.database_type), true};
    case CellKindDto::Unavailable:
        return UnavailableValue{text(value.database_type), text(value.text)};
    case CellKindDto::Text:
    case CellKindDto::Date:
    case CellKindDto::Time:
    case CellKindDto::Timestamp:
    case CellKindDto::Uuid:
    case CellKindDto::Json:
        return text(value.text);
    default:
        return UnavailableValue{text(value.database_type),
                                QObject::tr("Unknown value representation")};
    }
}
// Runs on a worker thread; the event stays read-only.
ResultTableModel::PreparedPage preparePage(const BridgeEvent& page,
                                           std::vector<ResultColumn> columns) {
    std::vector<ResultTableModel::Row> rows;
    std::vector<bool> filterable;
    rows.reserve(page.row_count);
    filterable.reserve(page.cells.size());
    size_t i = 0;
    for (quint32 r = 0; r < page.row_count; ++r) {
        ResultTableModel::Row row;
        row.reserve(page.column_count);
        for (quint32 c = 0; c < page.column_count; ++c, ++i) {
            row.push_back(cell(page.cells[i]));
            filterable.push_back(page.cells[i].foreign_key_filterable);
        }
        rows.push_back(std::move(row));
    }
    return ResultTableModel::preparePage(std::move(columns), std::move(rows), std::move(filterable),
                                         page.first_row);
}
} // namespace
void QueryWorkspace::showPage(const BridgeEvent& e, ResultTableModel::PreparedPage page) {
    fetching_ = false;
    if (!model_->setPage(std::move(page), cellMetadata_.size() == columns_.size()
                                              ? cellMetadata_
                                              : std::vector<ResultCellMetadata>{})) {
        if (e.has_lease)
            adapter_->releasePageLease(e.lease_id);
        message(tr("Result page exceeds the grid memory budget."));
        setExecutionState(QStringLiteral("failed"),
                          tr("Result page exceeds the grid memory budget."));
        hasMore_ = false;
        hasMoreResults_ = false;
    } else {
        configureEditability();
        if (filterBar_)
            filterBar_->setColumns(columns_);
        // setPage destroyed the old Qt buffers before we release their reservation.
        if (visibleLease_) {
            const auto lease = *visibleLease_;
            visibleLease_.reset();
            adapter_->releasePageLease(lease);
        }
        if (!e.has_lease || !adapter_->shrinkPageLease(e.lease_id, model_->residentBytes())) {
            model_->setPage({}, {}, 0);
            if (e.has_lease)
                adapter_->releasePageLease(e.lease_id);
            currentPage_.reset();
            hasMore_ = false;
            hasMoreResults_ = false;
            busy_ = false;
            message(tr("Result page exceeds its transfer reservation."));
            setExecutionState(QStringLiteral("failed"),
                              tr("Result page exceeds its transfer reservation."));
            updateActions();
            return;
        }
        visibleLease_ = e.lease_id;
        currentPage_ = e.page_index;
        jsonResultInvalidated_ = false;
        hasMore_ = e.has_more;
        const bool hasResultPage = !columns_.empty();
        setExecutionState(
            QStringLiteral("completed"),
            e.row_count == 0 && !viewFilters_.isEmpty()
                ? tr("No rows match the active filters · Clear filters to restore all rows")
                : tr("Completed"),
            {completedDurationMs_ ? tr("Executed in %1 ms").arg(*completedDurationMs_) : QString{},
             hasResultPage ? tr("Page %1").arg(e.page_index + 1) : QString{},
             hasResultPage ? (completedAffectedRows_.isEmpty()
                                  ? QString{}
                                  : completedAffectedRows_ + QStringLiteral(" · ")) +
                                 tr("%1 rows").arg(e.row_count)
                           : completedAffectedRows_,
             hasResultPage ? tr("%1 KiB visible").arg(model_->residentBytes() / 1024) : QString{}});
        if (filterBar_)
            filterBar_->setBusy(false);
        if (initialFilterPending_) {
            initialFilterPending_ = false;
            model_->setPage(columns_, {}, 0);
            if (visibleLease_) {
                adapter_->releasePageLease(*visibleLease_);
                visibleLease_.reset();
            }
            submitResultView({{0, "sql", "text", initialFilter_}}, -1, {});
            if (!viewBusy_) {
                referenceFilterPending_ = false;
                referenceFilterFailed_ = true;
                currentPage_.reset();
                hasMore_ = false;
                hasMoreResults_ = false;
                filterBar_->showValidationError(tr("Referenced row filter could not start."));
                setExecutionState(QStringLiteral("failed"),
                                  tr("Referenced row filter failed: %1").arg(commandError_));
            }
        } else if (deferredViewRequest_ && !model_->hasPendingEdits()) {
            deferredViewRequest_ = false;
            preserveViewOnRefresh_ = false;
            submitResultView(deferredViewFilters_, deferredViewSortColumn_,
                             deferredViewSortDirection_);
        } else if (preserveViewOnRefresh_) {
            preserveViewOnRefresh_ = false;
            viewRefreshQuery_.reset();
            if (!viewFilters_.isEmpty() || viewSortColumn_ >= 0)
                submitResultView(viewFilters_, viewSortColumn_, viewSortDirection_);
        }
    }
    busy_ = false;
    updateActions();
}
void QueryWorkspace::dropPageBuild() {
    pageBuild_ = nextRequestToken();
    pageBuilding_ = false;
    if (const auto lease = std::exchange(buildingLease_, std::nullopt); lease && adapter_)
        adapter_->releasePageLease(*lease);
}
void QueryWorkspace::handleEvent(const BridgeEvent& e) {
    const auto eventKind = text(e.kind);
    if (eventKind == "session_sql_mode") {
        connectionSqlModes_.insert(e.id, text(e.sql_mode));
        if (executionModeToken_ != 0 && e.request_token == executionModeToken_ &&
            executionModeConnection_ == e.id) {
            executionModeToken_ = 0;
            executionModeConnection_.reset();
            if (selectedConnection() == e.id && widgets_.currentEditor() == executionModeEditor_ &&
                executionModeEditor_ && executionModeEditor_->text() == executionModeSql_ &&
                static_cast<quint64>(executionModeEditor_->SendScintilla(
                    QsciScintilla::SCI_GETCURRENTPOS)) == executionModeCursor_ &&
                static_cast<quint64>(executionModeEditor_->SendScintilla(
                    QsciScintilla::SCI_GETSELECTIONSTART)) == executionModeStart_ &&
                static_cast<quint64>(executionModeEditor_->SendScintilla(
                    QsciScintilla::SCI_GETSELECTIONEND)) == executionModeEnd_) {
                executionModeReady_ = true;
                execute(executionModeScope_);
                executionModeReady_ = false;
            }
            executionModeEditor_.clear();
            executionModeSql_.clear();
            updateActions();
        }
        return;
    }
    if ((eventKind == "edit_target" || eventKind == "edit_target_failed" ||
         eventKind == "edit_query" || eventKind == "edit_query_failed") &&
        e.request_token == editTargetToken_ && queryConnection_ == e.id) {
        editColumnNames_.clear();
        editKey_.clear();
        editGenerated_.clear();
        editDuplicable_.clear();
        editQualifiedName_.clear();
        if (eventKind.endsWith("_failed"))
            editReason_ = text(e.error);
        else {
            editQualifiedName_ = text(e.edit_target.qualified_name);
            editReason_ = text(e.edit_target.reason);
            editParameterStyle_ = text(e.edit_target.parameter_style);
            // Rust aligns an edit query's columns to the result columns.
            for (const auto& column : e.edit_target.columns) {
                editColumnNames_.push_back(text(column.name));
                editKey_.push_back(column.key);
                editGenerated_.push_back(column.generated);
                editDuplicable_.push_back(column.duplicable);
            }
        }
        if (eventKind.startsWith("edit_query"))
            widgets_.grid->setToolTip(editReason_);
        configureEditability();
        updateActions();
        return;
    }
    if ((eventKind == "result_cells" || eventKind == "result_cells_failed") &&
        e.request_token == cellMetadataToken_ && queryConnection_ == e.id &&
        cellMetadataQuery_ == query_) {
        cellMetadataToken_ = 0;
        cellMetadata_.clear();
        if (eventKind == "result_cells" && e.result_cell_metadata.size() == columns_.size()) {
            cellMetadata_.reserve(e.result_cell_metadata.size());
            for (const auto& source : e.result_cell_metadata) {
                ResultCellMetadata metadata;
                metadata.sourceColumn = text(source.source_column);
                metadata.sourceObject = text(source.source_object);
                metadata.sourceQualifiedName = text(source.source_qualified_name);
                if (source.nullability == 0 || source.nullability == 1)
                    metadata.nullable = source.nullability == 1;
                metadata.boolean = source.boolean;
                for (const auto& choice : source.enum_choices)
                    metadata.enumChoices << text(choice);
                metadata.targetObject = text(source.fk_target_object);
                metadata.targetQualifiedName = text(source.fk_target_qualified_name);
                metadata.targetColumn = text(source.fk_target_column);
                cellMetadata_.push_back(std::move(metadata));
            }
            if (model_->columnCount() == static_cast<int>(cellMetadata_.size()))
                model_->setCellMetadata(cellMetadata_);
            configureEditability();
        }
        return;
    }
    if ((eventKind == "edit_applied" || eventKind == "edit_failed") &&
        e.request_token == editApplyToken_ && queryConnection_ == e.id) {
        editApplying_ = false;
        editApplied_ = eventKind == "edit_applied";
        if (editApplied_) {
            model_->discardEdits();
            quint64 total = 0;
            for (auto count : e.edit_affected_rows)
                total += count;
            message(tr("Applied %1 grid changes (%2 rows affected).")
                        .arg(e.edit_affected_rows.size())
                        .arg(total));
            preserveViewOnRefresh_ = true;
            if (widgets_.objectReadOnly && queryConnection_)
                openObjectData(*queryConnection_, objectId_, resultOrigin_,
                               querySettings_->preferences(), true, {}, objectTime_);
            else if (queryConnection_ && !executedSql_.isEmpty()) {
                if (query_)
                    adapter_->releaseQuery(*query_);
                clearResult();
                currentPage_.reset();
                query_ = adapter_->execute(*queryConnection_, executedSql_,
                                           connectionProfiles_.value(*queryConnection_),
                                           querySettings_->preferences());
                if (query_)
                    viewRefreshQuery_ = query_;
                else {
                    preserveViewOnRefresh_ = false;
                    clearViewState();
                    message(tr("The previous result view was invalidated because refresh could "
                               "not start."));
                }
                busy_ = query_.has_value();
                executionFinished_ = !busy_;
            }
        } else {
            deferredViewRequest_ = false;
            if (filterBar_)
                filterBar_->restoreApplied();
            message(tr("Grid changes were not applied: %1").arg(text(e.error)));
        }
        updateActions();
        return;
    }
    if (eventKind == "transaction_state") {
        if (e.transaction_active)
            pendingTransactions_.insert(e.id);
        else
            pendingTransactions_.remove(e.id);
        emit transactionStateChanged(e.id, e.transaction_active);
        updateActions();
        return;
    }
    if (eventKind == "connected") {
        if (e.has_transaction_state) {
            if (e.transaction_active) {
                pendingTransactions_.insert(e.id);
            } else {
                pendingTransactions_.remove(e.id);
            }
            emit transactionStateChanged(e.id, e.transaction_active);
        }
        const auto name = pendingConnections_.take(e.id);
        {
            const auto label = name.isEmpty() ? tr("Database session %1").arg(e.id) : name;
            liveConnections_.append({e.id, label});
            if (auto* editor = widgets_.currentEditor(); editor && !editor->hasAssignedTarget()) {
                editor->setConnectionTarget(e.id, label);
                editor->setProfileId(connectionProfiles_.value(e.id));
            }
        }
        if (connectionAttemptEditors_.contains(e.id)) {
            auto* editor = connectionAttemptEditors_.take(e.id).data();
            const auto previous = connectionAttemptStatus_.take(e.id);
            // A successful connection does not replace a retained successful query view.
            if (editor && editor == resultEditor_ && previous.value("state") == "completed" &&
                previous.value("source").toString() == resultOrigin_)
                editor->setProperty("resultStatus", previous);
            else
                setDocumentStatus(editor, QStringLiteral("disconnected"),
                                  tr("Connect and run a query to see results."), {});
        }
        for (const auto& notice : e.warnings)
            emit connectionNotice(text(notice));
        documentChanged();
        emit connectionReady(e.id);
        updateActions();
        return;
    }
    if (eventKind == "connection_failed" || eventKind == "operation_failed" ||
        eventKind == "bridge_failed") {
        if (executionModeConnection_ == e.id) {
            executionModeToken_ = 0;
            executionModeConnection_.reset();
            executionModeEditor_.clear();
        }
        const bool connectionFailure =
            eventKind == "connection_failed" && pendingConnections_.contains(e.id);
        const auto failure =
            tr("Failed: %1")
                .arg(text(e.error) + (e.vendor_code.empty()
                                          ? QString{}
                                          : tr(" [Code: %1]").arg(text(e.vendor_code))));
        if (connectionFailure)
            setDocumentStatus(connectionAttemptEditors_.take(e.id).data(), QStringLiteral("failed"),
                              failure, pendingConnections_.value(e.id));
        else if (eventKind == "operation_failed" && !widgets_.objectReadOnly &&
                 selectedConnection() == e.id)
            setDocumentStatus(widgets_.currentEditor(), QStringLiteral("failed"), failure, {});
        else if (eventKind == "bridge_failed" && workInFlight())
            setExecutionState(QStringLiteral("failed"), failure);
        connectionAttemptStatus_.remove(e.id);
        pendingConnections_.remove(e.id);
        if (eventKind == "connection_failed") {
            connectionProfiles_.remove(e.id);
            connectionDrivers_.remove(e.id);
            connectionSqlModes_.remove(e.id);
            disconnecting_.remove(e.id);
        }
        message(text(e.error) +
                (e.vendor_code.empty() ? QString{} : tr(" [Code: %1]").arg(text(e.vendor_code))));
        updateActions();
        return;
    }
    if (eventKind == "disconnected") {
        if (executionModeConnection_ == e.id) {
            executionModeToken_ = 0;
            executionModeConnection_.reset();
            executionModeEditor_.clear();
        }
        disconnecting_.remove(e.id);
        pendingTransactions_.remove(e.id);
        connectionProfiles_.remove(e.id);
        connectionDrivers_.remove(e.id);
        connectionSqlModes_.remove(e.id);
        liveConnections_.removeIf([&e](const LiveConnection& live) { return live.id == e.id; });
        documentChanged();
        if (queryConnection_ == e.id) {
            clearRowJson();
            jsonResultInvalidated_ = true;
            if (detail_)
                detail_->clearValue();
            if (export_)
                export_->clearQuery();
            busy_ = false;
            cancellationPending_ = false;
            fetching_ = false;
            hasMore_ = false;
            hasMoreResults_ = false;
            query_.reset();
            queryConnection_.reset();
            currentPage_.reset();
            clearViewState();
            const bool failed =
                resultEditor_
                    ? resultEditor_->property("resultStatus").toMap().value("state") == "failed"
                    : widgets_.summary->property("state") == "failed";
            if (!failed)
                setExecutionState(QStringLiteral("disconnected"), tr("Disconnected"));
        }
        updateActions();
        return;
    }
    if (!query_ || e.id != *query_)
        return;
    if (eventKind == "result_view_progress") {
        setExecutionState(
            QStringLiteral("running"),
            tr("Preparing result view · %1 rows scanned").arg(e.result_view_scanned_rows));
        updateActions();
        return;
    }
    if (eventKind == "result_view_applied") {
        referenceFilterPending_ = false;
        referenceFilterFailed_ = false;
        viewFilters_ = proposedViewFilters_;
        viewSortColumn_ = proposedViewSortColumn_;
        viewSortDirection_ = proposedViewSortDirection_;
        if (filterBar_)
            filterBar_->markApplied(viewFilters_);
        updateSortIndicator();
        viewBusy_ = false;
        fetching_ = true;
        currentPage_.reset();
        hasMore_ = false;
        adapter_->fetchPageAt(*query_, 0);
        setExecutionState(QStringLiteral("running"),
                          tr("Loading result view · %1 matching rows").arg(e.result_view_rows));
        updateActions();
        return;
    }
    if (eventKind == "result_view_failed") {
        viewBusy_ = false;
        const auto failureDetail =
            text(e.error) +
            (e.vendor_code.empty() ? QString{} : tr(" [Code: %1]").arg(text(e.vendor_code)));
        if (referenceFilterPending_ || referenceFilterFailed_) {
            referenceFilterPending_ = false;
            referenceFilterFailed_ = true;
            model_->setPage(columns_, {}, 0);
            currentPage_.reset();
            hasMore_ = false;
            hasMoreResults_ = false;
            if (filterBar_) {
                filterBar_->setBusy(false);
                filterBar_->showValidationError(
                    tr("Referenced row filter failed: %1").arg(failureDetail));
            }
            message(tr("Referenced row filter failed: %1").arg(failureDetail));
            setExecutionState(QStringLiteral("failed"),
                              tr("Referenced row filter failed: %1").arg(failureDetail));
            updateActions();
            return;
        }
        if (filterBar_ && proposedFiltersFromDraft_)
            filterBar_->showValidationError(failureDetail);
        if (filterBar_)
            filterBar_->setBusy(false);
        message(tr("Result view was not changed: %1").arg(failureDetail));
        setExecutionState(QStringLiteral("failed"),
                          tr("Previous result view restored: %1").arg(failureDetail));
        updateActions();
        return;
    }
    if (eventKind == "query_state") {
        const auto state = text(e.state);
        // Terminal state notices precede their detailed outcome. Keep the
        // pending cancellation visible until that acknowledgement arrives.
        if (cancellationPending_ && state != "cancelling")
            return;
        if (state == "completed" || state == "failed" || state == "disconnected")
            cancellationPending_ = false;
        busy_ = state == "queued" || state == "running" || state == "cancelling";
        if (state == "completed" || state == "failed" || state == "disconnected")
            executionFinished_ = true;
        setExecutionState(state, state);
        updateActions();
        if (state == "cancelling")
            widgets_.cancel->setEnabled(false);
    } else if (eventKind == "schema") {
        if (cancellationPending_ || !queryAvailable())
            return;
        clearResult();
        columns_.reserve(e.columns.size());
        for (const auto& c : e.columns)
            columns_.push_back(resultColumn(c));
        if (filterBar_)
            filterBar_->setColumns(columns_);
        const bool inspectNow = queryConnection_ && !EngineAdapter::driverWorkflow(
                                                         driverForConnection(*queryConnection_))
                                                         .inspectAfterResult;
        if (!widgets_.objectReadOnly && inspectNow && !executedSql_.isEmpty()) {
            editTargetToken_ = nextEditRequestToken();
            editQualifiedName_.clear();
            editReason_ = tr("Checking query edit eligibility…");
            widgets_.grid->setToolTip(editReason_);
            QStringList names;
            for (const auto& column : columns_)
                names << column.name;
            adapter_->inspectQueryEdit(*queryConnection_, executedSql_, names, editTargetToken_);
        }
        if (inspectNow)
            requestCellMetadata();
        quint64 bytes = columns_.capacity() * sizeof(ResultColumn);
        for (const auto& column : columns_)
            bytes += 96 + sizeof(QChar) * (column.name.capacity() + column.databaseType.capacity() +
                                           column.timezone.capacity());
        if (!e.has_lease || !adapter_->retainTransfer(e.lease_id, bytes)) {
            clearResult();
            busy_ = false;
            fetching_ = false;
            message(tr("Result schema exceeds its transfer reservation."));
            setExecutionState(QStringLiteral("failed"),
                              tr("Result schema exceeds its transfer reservation."));
            updateActions();
            return;
        }
        schemaLease_ = e.lease_id;
        fetching_ = true;
        adapter_->fetchPageAt(*query_, 0);
        updateActions();
    } else if (eventKind == "stored_page") {
        if (cancellationPending_)
            return;
        if (e.column_count != columns_.size() && e.row_count != 0) {
            fetching_ = false;
            message(tr("Result page has an invalid column count."));
            setExecutionState(QStringLiteral("failed"),
                              tr("Result page has an invalid column count."));
            busy_ = false;
            hasMore_ = false;
            hasMoreResults_ = false;
            updateActions();
            return;
        }
        if (static_cast<quint64>(e.row_count) * e.column_count != e.cells.size()) {
            fetching_ = false;
            message(tr("Result page has an invalid cell count."));
            setExecutionState(QStringLiteral("failed"),
                              tr("Result page has an invalid cell count."));
            busy_ = false;
            hasMore_ = false;
            hasMoreResults_ = false;
            updateActions();
            return;
        }
        auto page = adapter_->retainEvent(e);
        if (!page) {
            fetching_ = false;
            busy_ = false;
            hasMore_ = false;
            hasMoreResults_ = false;
            message(tr("Result page is no longer available."));
            setExecutionState(QStringLiteral("failed"), tr("Result page is no longer available."));
            updateActions();
            return;
        }
        dropPageBuild();
        pageBuilding_ = true;
        if (page->has_lease)
            buildingLease_ = page->lease_id;
        QtConcurrent::run([page, columns = columns_] {
            return preparePage(*page, columns);
        }).then(this, [this, page, build = pageBuild_](ResultTableModel::PreparedPage prepared) {
            if (build != pageBuild_)
                return;
            pageBuilding_ = false;
            buildingLease_.reset();
            showPage(*page, std::move(prepared));
        });
    } else if (eventKind == "query_finished") {
        hasMoreResults_ = e.has_more_results;
        if (queryConnection_ && e.has_sql_mode)
            connectionSqlModes_.insert(*queryConnection_, text(e.sql_mode));
        else if (queryConnection_)
            connectionSqlModes_.remove(*queryConnection_);
        const bool inspectNow =
            queryConnection_ && !hasMoreResults_ &&
            EngineAdapter::driverWorkflow(driverForConnection(*queryConnection_))
                .inspectAfterResult;
        if (!widgets_.objectReadOnly && inspectNow && !executedSql_.isEmpty()) {
            editTargetToken_ = nextEditRequestToken();
            QStringList names;
            for (const auto& column : columns_)
                names << column.name;
            adapter_->inspectQueryEdit(*queryConnection_, executedSql_, names, editTargetToken_);
        }
        if (inspectNow)
            requestCellMetadata();
        if (hasMoreResults_)
            message(tr("More results are available. Use Next result to continue."));
        cancellationPending_ = false;
        executionFinished_ = true;
        fetching_ = false;
        if (e.has_transaction_state && queryConnection_) {
            if (e.transaction_active)
                pendingTransactions_.insert(*queryConnection_);
            else
                pendingTransactions_.remove(*queryConnection_);
            emit transactionStateChanged(*queryConnection_, e.transaction_active);
        }
        busy_ = false;
        for (const auto& warning : e.warnings)
            message(text(warning));
        message(e.has_affected_rows ? tr("Completed in %1 ms; %2 rows affected.")
                                          .arg(e.duration_ms)
                                          .arg(e.affected_rows)
                                    : tr("Completed in %1 ms.").arg(e.duration_ms));
        completedDurationMs_ = e.duration_ms;
        const bool hasResultPage = currentPage_ && !columns_.empty();
        completedAffectedRows_ =
            e.has_affected_rows ? tr("%1 rows affected").arg(e.affected_rows) : QString{};
        QString rowMetric = completedAffectedRows_;
        if (hasResultPage)
            rowMetric += (rowMetric.isEmpty() ? QString{} : QStringLiteral(" · ")) +
                         tr("%1 rows").arg(model_->rowCount());
        setExecutionState(
            QStringLiteral("completed"), tr("Completed"),
            {tr("Executed in %1 ms").arg(e.duration_ms),
             hasResultPage ? tr("Page %1").arg(*currentPage_ + 1) : QString{}, rowMetric,
             hasResultPage ? tr("%1 KiB visible").arg(model_->residentBytes() / 1024) : QString{}});
        updateActions();
    } else if (eventKind == "query_failed") {
        // A failed statement can leave the session mode unknown.
        if (queryConnection_)
            connectionSqlModes_.remove(*queryConnection_);
        if (preserveViewOnRefresh_ && viewRefreshQuery_ == query_) {
            preserveViewOnRefresh_ = false;
            viewRefreshQuery_.reset();
            clearViewState();
            message(tr("The previous result view was invalidated because refresh failed."));
        }
        cancellationPending_ = false;
        executionFinished_ = true;
        busy_ = false;
        fetching_ = false;
        hasMore_ = false;
        hasMoreResults_ = false;
        message(text(e.error) +
                (e.vendor_code.empty() ? QString{} : tr(" [Code: %1]").arg(text(e.vendor_code))));
        const auto errorKind = text(e.error_kind);
        if (errorKind == "Cancelled")
            setExecutionState(QStringLiteral("cancelled"), tr("Cancelled"));
        else
            setExecutionState(
                QStringLiteral("failed"),
                tr("Failed: %1")
                    .arg(text(e.error) + (e.vendor_code.empty()
                                              ? QString{}
                                              : tr(" [Code: %1]").arg(text(e.vendor_code)))));
        updateActions();
    }
}
} // namespace choscordb
