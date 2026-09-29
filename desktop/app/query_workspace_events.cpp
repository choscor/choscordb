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
#include <QComboBox>
#include <QSignalBlocker>
#include <QTableView>
#include <utility>
namespace choscordb {
using query_workspace_detail::nextEditRequestToken;
using query_workspace_detail::text;
namespace {
Cell cell(const CellDto& value) {
    const auto kind = text(value.kind);
    if (kind == "null")
        return std::monostate{};
    if (kind == "boolean")
        return value.boolean;
    if (kind == "integer")
        return static_cast<qint64>(value.integer);
    if (kind == "real")
        return value.real;
    if (kind == "decimal")
        return DecimalValue{text(value.text)};
    if (kind == "binary")
        return QByteArray(reinterpret_cast<const char*>(value.bytes.data()),
                          static_cast<qsizetype>(value.bytes.size()));
    if (kind == "deferred")
        return DeferredValue{value.handle, value.byte_length, text(value.database_type)};
    if (kind == "fallback_text")
        return FallbackText{text(value.text), text(value.database_type)};
    if (kind == "deferred_fallback")
        return DeferredValue{value.handle, value.byte_length, text(value.database_type), true};
    if (kind == "unavailable")
        return UnavailableValue{text(value.database_type), text(value.text)};
    if (kind == "text" || kind == "date" || kind == "time" || kind == "timestamp" ||
        kind == "uuid" || kind == "json")
        return text(value.text);
    return UnavailableValue{text(value.database_type), QObject::tr("Unknown value representation")};
}
} // namespace
void QueryWorkspace::handleEvent(const BridgeEvent& e) {
    const auto kind = text(e.kind);
    if (kind == "session_sql_mode") {
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
                execute();
                executionModeReady_ = false;
            }
            executionModeEditor_.clear();
            executionModeSql_.clear();
            updateActions();
        }
        return;
    }
    if ((kind == "edit_target" || kind == "edit_target_failed" || kind == "edit_query" ||
         kind == "edit_query_failed") &&
        e.request_token == editTargetToken_ && queryConnection_ == e.id) {
        editColumnNames_.clear();
        editKey_.clear();
        editGenerated_.clear();
        editQualifiedName_.clear();
        if (kind.endsWith("_failed"))
            editReason_ = text(e.error);
        else {
            editQualifiedName_ = text(e.edit_target.qualified_name);
            editReason_ = text(e.edit_target.reason);
            editParameterStyle_ = text(e.edit_target.parameter_style);
            if (kind == "edit_query") {
                for (const auto& source : e.edit_source_columns) {
                    const auto name = text(source);
                    editColumnNames_.push_back(name);
                    bool key = false, generated = true;
                    for (const auto& column : e.edit_target.columns)
                        if (name == text(column.name)) {
                            key = column.key;
                            generated = column.generated;
                            break;
                        }
                    editKey_.push_back(key);
                    editGenerated_.push_back(generated);
                }
            } else {
                for (const auto& column : e.edit_target.columns) {
                    editColumnNames_.push_back(text(column.name));
                    editKey_.push_back(column.key);
                    editGenerated_.push_back(column.generated);
                }
            }
        }
        if (kind.startsWith("edit_query"))
            widgets_.grid->setToolTip(editReason_);
        configureEditability();
        updateActions();
        return;
    }
    if ((kind == "result_cells" || kind == "result_cells_failed") &&
        e.request_token == cellMetadataToken_ && queryConnection_ == e.id &&
        cellMetadataQuery_ == query_) {
        cellMetadataToken_ = 0;
        cellMetadata_.clear();
        if (kind == "result_cells" && e.result_cell_metadata.size() == columns_.size()) {
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
    if ((kind == "edit_applied" || kind == "edit_failed") && e.request_token == editApplyToken_ &&
        queryConnection_ == e.id) {
        editApplying_ = false;
        editApplied_ = kind == "edit_applied";
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
                               querySettings_->preferences(), objectKind_, true);
            else if (queryConnection_ && !executedSql_.isEmpty()) {
                if (query_)
                    adapter_->releaseQuery(*query_);
                clearResult();
                currentPage_.reset();
                query_ = adapter_->execute(*queryConnection_, executedSql_, true,
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
    if (kind == "connected") {
        if (e.has_transaction_state) {
            if (e.transaction_active) {
                pendingTransactions_.insert(e.id);
                manualModes_.insert(e.id, true);
            } else {
                pendingTransactions_.remove(e.id);
            }
            emit transactionStateChanged(e.id, e.transaction_active);
        }
        const auto name = pendingConnections_.take(e.id);
        {
            const QSignalBlocker blocker(widgets_.connections);
            const auto label = name.isEmpty() ? tr("Database session %1").arg(e.id) : name;
            widgets_.connections->addItem(label, QVariant::fromValue<qulonglong>(e.id));
            if (auto* editor = widgets_.currentEditor(); editor && !editor->hasAssignedTarget()) {
                editor->setConnectionTarget(e.id, label);
                editor->setProfileId(connectionProfiles_.value(e.id));
            }
        }
        documentChanged();
        emit connectionReady(e.id);
        updateActions();
        return;
    }
    if (kind == "connection_failed" || kind == "operation_failed" || kind == "bridge_failed") {
        if (executionModeConnection_ == e.id) {
            executionModeToken_ = 0;
            executionModeConnection_.reset();
            executionModeEditor_.clear();
        }
        pendingConnections_.remove(e.id);
        if (kind == "connection_failed") {
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
    if (kind == "disconnected") {
        if (executionModeConnection_ == e.id) {
            executionModeToken_ = 0;
            executionModeConnection_.reset();
            executionModeEditor_.clear();
        }
        disconnecting_.remove(e.id);
        pendingTransactions_.remove(e.id);
        manualModes_.remove(e.id);
        connectionProfiles_.remove(e.id);
        connectionDrivers_.remove(e.id);
        connectionSqlModes_.remove(e.id);
        const auto index = widgets_.connections->findData(QVariant::fromValue<qulonglong>(e.id));
        if (index >= 0) {
            const QSignalBlocker blocker(widgets_.connections);
            widgets_.connections->removeItem(index);
        }
        documentChanged();
        if (queryConnection_ == e.id) {
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
            setExecutionState(QStringLiteral("disconnected"), tr("○ Disconnected"));
        }
        updateActions();
        return;
    }
    if (kind == "transaction_finished") {
        pendingTransactions_.remove(e.id);
        emit transactionStateChanged(e.id, false);
        message(e.committed ? tr("Transaction committed.") : tr("Transaction rolled back."));
        if (queryConnection_ == e.id) {
            hasMore_ = false;
            hasMoreResults_ = false;
            busy_ = false;
            if (query_ && currentPage_ && queryAvailable()) {
                fetching_ = true;
                adapter_->fetchPageAt(*query_, *currentPage_);
            }
            updateActions();
        }
        return;
    }
    if (!query_ || e.id != *query_)
        return;
    if (kind == "result_view_progress") {
        setExecutionState(
            QStringLiteral("running"),
            tr("◷ Preparing result view · %1 rows scanned").arg(e.result_view_scanned_rows));
        updateActions();
        return;
    }
    if (kind == "result_view_applied") {
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
                          tr("◷ Loading result view · %1 matching rows").arg(e.result_view_rows));
        updateActions();
        return;
    }
    if (kind == "result_view_failed") {
        viewBusy_ = false;
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
                    tr("Referenced row filter failed: %1").arg(text(e.error)));
            }
            message(tr("Referenced row filter failed: %1").arg(text(e.error)));
            setExecutionState(QStringLiteral("failed"), tr("! Referenced row filter failed"));
            updateActions();
            return;
        }
        if (filterBar_ && proposedFiltersFromDraft_)
            filterBar_->showValidationError(text(e.error));
        if (filterBar_)
            filterBar_->setBusy(false);
        message(tr("Result view was not changed: %1").arg(text(e.error)));
        setExecutionState(QStringLiteral("completed"), tr("✓ Previous result view restored"));
        updateActions();
        return;
    }
    if (kind == "query_state") {
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
        const QString icon = state == "completed"      ? QStringLiteral("✓ ")
                             : state == "failed"       ? QStringLiteral("! ")
                             : state == "disconnected" ? QStringLiteral("○ ")
                                                       : QStringLiteral("◷ ");
        setExecutionState(state, icon + state);
        updateActions();
        if (state == "cancelling")
            widgets_.cancel->setEnabled(false);
    } else if (kind == "schema") {
        if (cancellationPending_ || !queryAvailable())
            return;
        clearResult();
        columns_.reserve(e.columns.size());
        for (const auto& c : e.columns)
            columns_.push_back(resultColumn(c));
        if (filterBar_)
            filterBar_->setColumns(columns_);
        if (!widgets_.objectReadOnly && queryConnection_ && !executedSql_.isEmpty() &&
            driverForConnection(*queryConnection_) != "mysql") {
            editTargetToken_ = nextEditRequestToken();
            editQualifiedName_.clear();
            editReason_ = tr("Checking query edit eligibility…");
            widgets_.grid->setToolTip(editReason_);
            QStringList names;
            for (const auto& column : columns_)
                names << column.name;
            adapter_->inspectQueryEdit(*queryConnection_, executedSql_, names, editTargetToken_);
        }
        if (queryConnection_ && driverForConnection(*queryConnection_) != "mysql")
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
            updateActions();
            return;
        }
        schemaLease_ = e.lease_id;
        fetching_ = true;
        adapter_->fetchPageAt(*query_, 0);
        updateActions();
    } else if (kind == "stored_page") {
        if (cancellationPending_)
            return;
        fetching_ = false;
        if (e.column_count != columns_.size() && e.row_count != 0) {
            message(tr("Result page has an invalid column count."));
            setExecutionState(QStringLiteral("failed"), tr("! Failed to load result page"));
            busy_ = false;
            hasMore_ = false;
            hasMoreResults_ = false;
            updateActions();
            return;
        }
        if (static_cast<quint64>(e.row_count) * e.column_count != e.cells.size()) {
            message(tr("Result page has an invalid cell count."));
            setExecutionState(QStringLiteral("failed"), tr("! Failed to load result page"));
            busy_ = false;
            hasMore_ = false;
            hasMoreResults_ = false;
            updateActions();
            return;
        }
        std::vector<ResultTableModel::Row> rows;
        rows.reserve(e.row_count);
        size_t i = 0;
        for (quint32 r = 0; r < e.row_count; ++r) {
            ResultTableModel::Row row;
            row.reserve(e.column_count);
            for (quint32 c = 0; c < e.column_count; ++c)
                row.push_back(cell(e.cells[i++]));
            rows.push_back(std::move(row));
        }
        if (!model_->setPage(columns_, std::move(rows), e.first_row)) {
            message(tr("Result page exceeds the grid memory budget."));
            setExecutionState(QStringLiteral("failed"), tr("! Failed to load result page"));
            hasMore_ = false;
            hasMoreResults_ = false;
        } else {
            configureEditability();
            if (cellMetadata_.size() == columns_.size())
                model_->setCellMetadata(cellMetadata_);
            if (filterBar_)
                filterBar_->setColumns(columns_, model_->rows());
            // setPage destroyed the old Qt buffers before we release their reservation.
            if (visibleLease_) {
                const auto lease = *visibleLease_;
                visibleLease_.reset();
                adapter_->releasePageLease(lease);
            }
            if (!e.has_lease || !adapter_->retainTransfer(e.lease_id, model_->residentBytes())) {
                model_->setPage({}, {}, 0);
                currentPage_.reset();
                hasMore_ = false;
                hasMoreResults_ = false;
                busy_ = false;
                message(tr("Result page exceeds its transfer reservation."));
                setExecutionState(QStringLiteral("failed"), tr("! Failed to load result page"));
                updateActions();
                return;
            }
            visibleLease_ = e.lease_id;
            currentPage_ = e.page_index;
            hasMore_ = e.has_more;
            const bool hasResultPage = !columns_.empty();
            setExecutionState(
                QStringLiteral("completed"),
                e.row_count == 0 && !viewFilters_.isEmpty()
                    ? tr("✓ No rows match the active filters · Clear filters to restore all rows")
                    : tr("✓ Completed"),
                {completedDurationMs_ ? tr("%1 ms").arg(*completedDurationMs_) : QString{},
                 hasResultPage ? tr("Page %1").arg(e.page_index + 1) : QString{},
                 hasResultPage ? tr("%1 rows").arg(e.row_count) : QString{},
                 hasResultPage ? tr("%1 KiB visible").arg(model_->residentBytes() / 1024)
                               : QString{}});
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
                                      tr("! Referenced row filter failed"));
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
    } else if (kind == "query_finished") {
        hasMoreResults_ = e.has_more_results;
        if (queryConnection_ && e.has_sql_mode)
            connectionSqlModes_.insert(*queryConnection_, text(e.sql_mode));
        if (!widgets_.objectReadOnly && queryConnection_ && !executedSql_.isEmpty() &&
            driverForConnection(*queryConnection_) == "mysql" && !hasMoreResults_) {
            editTargetToken_ = nextEditRequestToken();
            QStringList names;
            for (const auto& column : columns_)
                names << column.name;
            adapter_->inspectQueryEdit(*queryConnection_, executedSql_, names, editTargetToken_);
        }
        if (queryConnection_ && driverForConnection(*queryConnection_) == "mysql" &&
            !hasMoreResults_)
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
        QString rowMetric =
            e.has_affected_rows ? tr("%1 rows affected").arg(e.affected_rows) : QString{};
        if (hasResultPage)
            rowMetric += (rowMetric.isEmpty() ? QString{} : QStringLiteral(" · ")) +
                         tr("%1 rows").arg(model_->rowCount());
        setExecutionState(
            QStringLiteral("completed"), tr("✓ Completed"),
            {tr("%1 ms").arg(e.duration_ms),
             hasResultPage ? tr("Page %1").arg(*currentPage_ + 1) : QString{}, rowMetric,
             hasResultPage ? tr("%1 KiB visible").arg(model_->residentBytes() / 1024) : QString{}});
        updateActions();
    } else if (kind == "query_failed") {
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
            setExecutionState(QStringLiteral("cancelled"), tr("○ Cancelled"));
        else if (errorKind == "Disconnected")
            setExecutionState(QStringLiteral("disconnected"), tr("○ Disconnected"));
        else
            setExecutionState(QStringLiteral("failed"), tr("! Failed"));
        updateActions();
    }
}
} // namespace choscordb
