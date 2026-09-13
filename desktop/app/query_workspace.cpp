#include "app/query_workspace.h"
#include "app/query_settings.h"
#include "bridge/engine_adapter.h"
#include "bridge/result_column_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/confirmation_dialog/confirmation_dialog.h"
#include "widgets/export_dialog/export_dialog.h"
#include "widgets/profile_dialog/profile_dialog.h"
#include "widgets/sql_editor/sql_editor.h"
#include "widgets/value_detail_dialog/value_detail_dialog.h"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QTableView>
namespace choscordb {
namespace {
QString text(const rust::String& value) {
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}
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
    if (kind == "binary")
        return QByteArray(reinterpret_cast<const char*>(value.bytes.data()),
                          static_cast<qsizetype>(value.bytes.size()));
    if (kind == "deferred")
        return DeferredValue{value.handle, value.byte_length, text(value.database_type)};
    return text(value.text);
}
} // namespace
QueryWorkspace::QueryWorkspace(Widgets widgets, QObject* parent)
    : QObject(parent), widgets_(std::move(widgets)),
      adapter_(new EngineAdapter(this, widgets_.storagePath)), model_(new ResultTableModel(this)) {
    widgets_.grid->setModel(model_);
    querySettings_ = new QuerySettingsController(adapter_, widgets_.dialogParent, this);
    connect(querySettings_, &QuerySettingsController::readyChanged, this,
            [this](bool) { updateActions(); });
    connect(querySettings_, &QuerySettingsController::failed, this,
            [this](const QString& error) { message(tr("Query settings: %1").arg(error)); });
    connect(adapter_, &EngineAdapter::historyWriteFailed, this,
            [this](quint64, const QString& error) { message(tr("History: %1").arg(error)); });
    if (widgets_.exportResult)
        connect(widgets_.exportResult, &QPushButton::clicked, this, [this] {
            if (!query_ || exporting_ || !queryAvailable())
                return;
            if (!export_) {
                export_ = new ExportDialog(adapter_, widgets_.dialogParent);
                connect(export_, &ExportDialog::exportRunningChanged, this, [this](bool running) {
                    exporting_ = running;
                    updateActions();
                });
            }
            export_->setQuery(*query_);
            export_->show();
            export_->raise();
        });
    const auto openDetail = [this](const QModelIndex& index) {
        if (!query_ || !queryAvailable())
            return;
        const auto value = model_->deferredValue(index);
        if (!value)
            return;
        if (!detail_)
            detail_ = new ValueDetailDialog(adapter_, widgets_.dialogParent);
        detail_->openValue(*query_, value->handle, value->type, value->bytes);
    };
    connect(widgets_.grid, &QTableView::doubleClicked, this, openDetail);
    connect(widgets_.grid, &QTableView::activated, this, openDetail);
    connect(adapter_, &EngineAdapter::eventReady, this, &QueryWorkspace::handleEvent);
    connect(adapter_, &EngineAdapter::commandFailed, this, [this](const QString& error) {
        if (fetching_) {
            busy_ = false;
            fetching_ = false;
        }
        message(error);
        updateActions();
    });
    connect(widgets_.run, &QAction::triggered, this, &QueryWorkspace::execute);
    connect(widgets_.cancel, &QAction::triggered, this, [this] {
        if (query_ && queryAvailable()) {
            adapter_->cancelQuery(*query_);
            widgets_.cancel->setEnabled(false);
        }
    });
    connect(widgets_.commit, &QAction::triggered, this, [this] {
        if (auto c = selectedConnection(); c && connectionAvailable(*c) && !workInFlight())
            adapter_->commitTransaction(*c);
    });
    connect(widgets_.rollback, &QAction::triggered, this, [this] {
        if (auto c = selectedConnection(); c && connectionAvailable(*c) && !workInFlight())
            adapter_->rollbackTransaction(*c);
    });
    connect(widgets_.newConnection, &QAction::triggered, this, [this] {
        if (!profiles_) {
            profiles_ = new ProfileDialog(adapter_, widgets_.dialogParent);
            connect(profiles_, &ProfileDialog::connectionSubmitted, this,
                    [this](const SavedProfile& profile, quint64 id, bool savedProfile) {
                        pendingConnections_.insert(id, profile.name);
                        if (savedProfile)
                            connectionProfiles_.insert(id, profile.id);
                    });
        }
        profiles_->show();
        profiles_->raise();
    });
    connect(widgets_.connections, &QComboBox::currentIndexChanged, this, [this] {
        const QSignalBlocker blocker(widgets_.mode);
        const auto c = selectedConnection();
        widgets_.mode->setCurrentIndex(c && manualModes_.value(*c, false) ? 1 : 0);
        updateActions();
    });
    connect(widgets_.mode, &QComboBox::currentIndexChanged, this, [this] {
        if (const auto c = selectedConnection()) {
            if (!connectionAvailable(*c) || workInFlight()) {
                const QSignalBlocker blocker(widgets_.mode);
                widgets_.mode->setCurrentIndex(manualModes_.value(*c, false) ? 1 : 0);
                return;
            }
            if (widgets_.mode->currentIndex() == 0 && pendingTransactions_.contains(*c)) {
                const QSignalBlocker blocker(widgets_.mode);
                widgets_.mode->setCurrentIndex(1);
                message(tr("Commit or roll back before enabling auto-commit."));
            } else
                manualModes_.insert(*c, widgets_.mode->currentIndex() == 1);
        }
        updateActions();
    });
    connect(widgets_.nextPage, &QPushButton::clicked, this, [this] {
        if (query_ && hasMore_ && !fetching_ && queryAvailable() && !exporting_) {
            fetching_ = true;
            busy_ = true;
            updateActions();
            adapter_->fetchPageAt(*query_, currentPage_.value_or(0) + 1);
        }
    });
    if (widgets_.previousPage)
        connect(widgets_.previousPage, &QPushButton::clicked, this, [this] {
            if (query_ && currentPage_ && *currentPage_ > 0 && !fetching_ && queryAvailable() &&
                !exporting_) {
                fetching_ = true;
                updateActions();
                adapter_->fetchPageAt(*query_, *currentPage_ - 1);
            }
        });
    const auto copyScope = [this](int scope) {
        if (widgets_.grid->model() != model_ || !widgets_.grid->selectionModel() ||
            widgets_.grid->selectionModel()->model() != model_) {
            message(tr("The result page is no longer available."));
            return;
        }
        const auto selection =
            scope == 2 ? QModelIndexList{} : widgets_.grid->selectionModel()->selectedIndexes();
        if ((scope != 2 && selection.isEmpty()) || !model_->rowCount() || !model_->columnCount())
            return;
        QString error;
        const auto copied = scope == 0   ? model_->copyCells(selection, &error)
                            : scope == 1 ? model_->copyRows(selection, &error)
                                         : model_->copyPage(&error);
        if (!error.isEmpty())
            message(error);
        else
            QApplication::clipboard()->setText(copied);
    };
    auto* copy = new QShortcut(QKeySequence::Copy, widgets_.grid);
    copy->setContext(Qt::WidgetWithChildrenShortcut);
    connect(copy, &QShortcut::activated, this, [copyScope] { copyScope(0); });
    widgets_.grid->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(widgets_.grid, &QTableView::customContextMenuRequested, this,
            [this, copyScope](const QPoint& point) {
                QMenu menu(widgets_.grid);
                const bool current = widgets_.grid->model() == model_ &&
                                     widgets_.grid->selectionModel() &&
                                     widgets_.grid->selectionModel()->model() == model_;
                const bool selected = current && widgets_.grid->selectionModel()->hasSelection();
                const QStringList names = {"copySelectedCells", "copySelectedRows",
                                           "copyCurrentPage"};
                const QStringList labels = {tr("Copy selected cells"), tr("Copy selected rows"),
                                            tr("Copy current page")};
                for (int scope = 0; scope < 3; ++scope) {
                    auto* action = menu.addAction(labels[scope]);
                    action->setObjectName(names[scope]);
                    action->setEnabled(scope == 2 ? current && model_->rowCount() > 0 &&
                                                        model_->columnCount() > 0
                                                  : selected);
                    connect(action, &QAction::triggered, this,
                            [copyScope, scope] { copyScope(scope); });
                }
                menu.exec(widgets_.grid->viewport()->mapToGlobal(point));
            });
    updateActions();
}
QueryWorkspace::~QueryWorkspace() {
    clearResult();
    delete detail_;
    delete export_;
    delete profiles_;
}
void QueryWorkspace::clearResult() {
    if (export_)
        export_->clearQuery();
    if (detail_)
        detail_->clearValue();
    model_->setPage({}, {}, 0);
    if (visibleLease_) {
        const auto lease = *visibleLease_;
        visibleLease_.reset();
        adapter_->releasePageLease(lease);
    }
    std::vector<ResultColumn>().swap(columns_);
    if (schemaLease_) {
        const auto lease = *schemaLease_;
        schemaLease_.reset();
        adapter_->releasePageLease(lease);
    }
}
bool QueryWorkspace::connectionAvailable(quint64 connection) const {
    return !stopping_ && !disconnecting_.contains(connection) &&
           widgets_.connections->findData(QVariant::fromValue<qulonglong>(connection)) >= 0;
}
bool QueryWorkspace::queryAvailable() const {
    return queryConnection_ && connectionAvailable(*queryConnection_);
}
bool QueryWorkspace::workInFlight() const {
    return (busy_ || fetching_ || exporting_) &&
           !(queryConnection_ && disconnecting_.contains(*queryConnection_));
}
void QueryWorkspace::disconnectConnection(quint64 connection) {
    if (!connectionAvailable(connection) || confirmingDisconnects_.contains(connection))
        return;
    const auto index = widgets_.connections->findData(QVariant::fromValue<qulonglong>(connection));
    const auto label = widgets_.connections->itemText(index);
    const bool transaction = pendingTransactions_.contains(connection);
    const bool active =
        queryConnection_ == connection && (busy_ || fetching_ || exporting_ || !executionFinished_);
    QString notice =
        tr("Disconnect %1? Any uncommitted transaction will be rolled back.").arg(label);
    if (active)
        notice += tr(" Active query, fetch, or export work will be cancelled.");
    ConfirmationDialog box(QMessageBox::Warning, tr("Disconnect database session"), notice,
                           QMessageBox::NoButton, widgets_.dialogParent);
    box.setTextFormat(Qt::PlainText);
    auto* accept = box.addButton(transaction ? tr("Roll back and disconnect") : tr("Disconnect"),
                                 QMessageBox::DestructiveRole);
    auto* cancel = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(cancel);
    confirmingDisconnects_.insert(connection);
    box.exec();
    confirmingDisconnects_.remove(connection);
    if (box.clickedButton() != accept || !connectionAvailable(connection))
        return;
    disconnecting_.insert(connection);
    updateActions();
    if (!adapter_->disconnectConnection(connection)) {
        disconnecting_.remove(connection);
        updateActions();
        return;
    }
    // Separate dialogs can otherwise submit work even with the toolbar disabled.
    if (queryConnection_ == connection) {
        if (export_)
            export_->clearQuery();
        if (detail_)
            detail_->clearValue();
    }
    updateActions();
}
void QueryWorkspace::showQuerySettings() {
    if (!stopping_)
        querySettings_->open();
}
void QueryWorkspace::connectSqlite(const QString& path) {
    if (auto id = adapter_->connectSqlite(path))
        pendingConnections_.insert(*id, path);
}
std::optional<quint64> QueryWorkspace::selectedConnection() const {
    if (widgets_.connections->currentIndex() < 0)
        return std::nullopt;
    const auto value = widgets_.connections->currentData();
    if (!value.isValid())
        return std::nullopt;
    return value.toULongLong();
}
void QueryWorkspace::message(const QString& value) {
    widgets_.messages->appendPlainText(value);
}
void QueryWorkspace::setExecutionState(const QString& state, const QString& detail) {
    widgets_.summary->setProperty("state", state);
    const auto label = detail.isEmpty() ? state : detail;
    widgets_.summary->setText(label);
    widgets_.summary->setAccessibleName(tr("Execution status: %1").arg(label));
    widgets_.summary->style()->unpolish(widgets_.summary);
    widgets_.summary->style()->polish(widgets_.summary);
}
void QueryWorkspace::updateActions() {
    const auto selected = selectedConnection();
    const bool connected = selected && connectionAvailable(*selected);
    const bool inFlight = workInFlight();
    widgets_.connections->setEnabled(widgets_.connections->count() > 0 && !inFlight && !stopping_);
    widgets_.mode->setEnabled(connected && !inFlight);
    widgets_.run->setEnabled(connected && !inFlight && querySettings_->isReady());
    widgets_.cancel->setEnabled(query_.has_value() && busy_ && queryAvailable());
    widgets_.cancel->setText(widgets_.summary->property("state") == "cancelling" ? tr("Cancelling…")
                                                                                 : tr("Cancel"));
    if (widgets_.exportResult)
        widgets_.exportResult->setEnabled(query_ && currentPage_ && !inFlight && queryAvailable());
    const bool manual = widgets_.mode->currentIndex() == 1;
    widgets_.commit->setEnabled(connected && manual && !inFlight);
    widgets_.rollback->setEnabled(connected && manual && !inFlight);
    widgets_.nextPage->setEnabled(query_.has_value() && hasMore_ && !inFlight && queryAvailable());
    if (widgets_.previousPage)
        widgets_.previousPage->setEnabled(query_ && currentPage_ && *currentPage_ > 0 &&
                                          !inFlight && queryAvailable());
}
void QueryWorkspace::execute() {
    auto connection = selectedConnection();
    auto* editor = widgets_.currentEditor();
    if (!connection || !editor || !connectionAvailable(*connection) || workInFlight() ||
        !querySettings_->isReady())
        return;
    const auto sql = editor->text();
    const auto range = EngineAdapter::executionRange(
        sql, static_cast<quint64>(editor->SendScintilla(QsciScintilla::SCI_GETCURRENTPOS)),
        static_cast<quint64>(editor->SendScintilla(QsciScintilla::SCI_GETSELECTIONSTART)),
        static_cast<quint64>(editor->SendScintilla(QsciScintilla::SCI_GETSELECTIONEND)));
    if (!range.valid) {
        message(tr("No executable statement at the cursor."));
        return;
    }
    if (range.confirmation) {
        ConfirmationDialog box(QMessageBox::Warning, tr("Confirm SQL execution"),
                               tr("This statement may change or delete data. Execute it?"),
                               QMessageBox::Yes | QMessageBox::Cancel, widgets_.dialogParent);
        box.setTextFormat(Qt::PlainText);
        box.setDefaultButton(QMessageBox::Cancel);
        if (box.exec() != QMessageBox::Yes)
            return;
    }
    // Confirmation runs a nested event loop: the target may have disconnected.
    if (!connectionAvailable(*connection) || workInFlight() || widgets_.currentEditor() != editor)
        return;
    if (query_ && queryAvailable())
        adapter_->releaseQuery(*query_);
    const auto bytes = sql.toUtf8();
    query_ = adapter_->execute(
        *connection,
        QString::fromUtf8(bytes.mid(static_cast<qsizetype>(range.start),
                                    static_cast<qsizetype>(range.end - range.start))),
        widgets_.mode->currentIndex() != 1, connectionProfiles_.value(*connection),
        querySettings_->preferences());
    if (query_)
        editor->setProfileId(connectionProfiles_.value(*connection));
    if (query_ && widgets_.mode->currentIndex() == 1)
        pendingTransactions_.insert(*connection);
    queryConnection_ = connection;
    clearResult();
    currentPage_.reset();
    hasMore_ = false;
    fetching_ = false;
    busy_ = query_.has_value();
    executionFinished_ = !query_.has_value();
    setExecutionState(busy_ ? QStringLiteral("queued") : QStringLiteral("failed"),
                      busy_ ? tr("◷ Queued") : tr("! Submission failed"));
    updateActions();
}
void QueryWorkspace::handleEvent(const BridgeEvent& e) {
    const auto kind = text(e.kind);
    if (kind == "connected") {
        const auto name = pendingConnections_.take(e.id);
        widgets_.connections->addItem(name.isEmpty() ? tr("Database session") : name,
                                      QVariant::fromValue<qulonglong>(e.id));
        widgets_.connections->setCurrentIndex(widgets_.connections->count() - 1);
        emit connectionReady(e.id);
        updateActions();
        return;
    }
    if (kind == "connection_failed" || kind == "operation_failed" || kind == "bridge_failed") {
        pendingConnections_.remove(e.id);
        if (kind == "connection_failed") {
            connectionProfiles_.remove(e.id);
            disconnecting_.remove(e.id);
        }
        message(text(e.error) +
                (e.vendor_code.empty() ? QString{} : tr(" [Code: %1]").arg(text(e.vendor_code))));
        return;
    }
    if (kind == "disconnected") {
        disconnecting_.remove(e.id);
        pendingTransactions_.remove(e.id);
        manualModes_.remove(e.id);
        connectionProfiles_.remove(e.id);
        const auto index = widgets_.connections->findData(QVariant::fromValue<qulonglong>(e.id));
        if (index >= 0)
            widgets_.connections->removeItem(index);
        if (queryConnection_ == e.id) {
            if (detail_)
                detail_->clearValue();
            if (export_)
                export_->clearQuery();
            busy_ = false;
            fetching_ = false;
            hasMore_ = false;
            query_.reset();
            queryConnection_.reset();
            currentPage_.reset();
            setExecutionState(QStringLiteral("disconnected"), tr("○ Disconnected"));
        }
        updateActions();
        return;
    }
    if (kind == "transaction_finished") {
        pendingTransactions_.remove(e.id);
        message(e.committed ? tr("Transaction committed.") : tr("Transaction rolled back."));
        if (queryConnection_ == e.id) {
            hasMore_ = false;
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
    if (kind == "query_state") {
        const auto state = text(e.state);
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
        if (!queryAvailable())
            return;
        clearResult();
        columns_.reserve(e.columns.size());
        for (const auto& c : e.columns)
            columns_.push_back(resultColumn(c));
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
        fetching_ = false;
        if (e.column_count != columns_.size() && e.row_count != 0) {
            message(tr("Result page has an invalid column count."));
            hasMore_ = false;
            updateActions();
            return;
        }
        if (static_cast<quint64>(e.row_count) * e.column_count != e.cells.size()) {
            message(tr("Result page has an invalid cell count."));
            hasMore_ = false;
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
            hasMore_ = false;
        } else {
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
                busy_ = false;
                message(tr("Result page exceeds its transfer reservation."));
                updateActions();
                return;
            }
            visibleLease_ = e.lease_id;
            currentPage_ = e.page_index;
            hasMore_ = e.has_more;
            setExecutionState(QStringLiteral("completed"),
                              tr("✓ Completed · Page %1 · %2 rows · %3 KiB visible")
                                  .arg(e.page_index + 1)
                                  .arg(e.row_count)
                                  .arg(model_->residentBytes() / 1024));
        }
        busy_ = false;
        updateActions();
    } else if (kind == "query_finished") {
        executionFinished_ = true;
        if (e.has_transaction_state && queryConnection_) {
            if (e.transaction_active)
                pendingTransactions_.insert(*queryConnection_);
            else
                pendingTransactions_.remove(*queryConnection_);
        }
        busy_ = false;
        for (const auto& warning : e.warnings)
            message(text(warning));
        message(e.has_affected_rows ? tr("Completed in %1 ms; %2 rows affected.")
                                          .arg(e.duration_ms)
                                          .arg(e.affected_rows)
                                    : tr("Completed in %1 ms.").arg(e.duration_ms));
        QString summary = e.has_affected_rows ? tr("✓ Completed · %1 ms · %2 rows affected")
                                                    .arg(e.duration_ms)
                                                    .arg(e.affected_rows)
                                              : tr("✓ Completed · %1 ms").arg(e.duration_ms);
        if (currentPage_)
            summary += tr(" · Page %1 · %2 rows · %3 KiB visible")
                           .arg(*currentPage_ + 1)
                           .arg(model_->rowCount())
                           .arg(model_->residentBytes() / 1024);
        setExecutionState(QStringLiteral("completed"), summary);
        updateActions();
    } else if (kind == "query_failed") {
        executionFinished_ = true;
        busy_ = false;
        fetching_ = false;
        hasMore_ = false;
        message(text(e.error) +
                (e.vendor_code.empty() ? QString{} : tr(" [Code: %1]").arg(text(e.vendor_code))));
        setExecutionState(QStringLiteral("failed"), tr("! Failed"));
        updateActions();
    }
}
bool QueryWorkspace::confirmShutdown() {
    const bool transaction = !pendingTransactions_.isEmpty();
    const bool active = busy_ || fetching_ || exporting_ || (query_ && !executionFinished_);
    if (!transaction && !active)
        return true;
    ConfirmationDialog box(
        QMessageBox::Warning, tr("Close database sessions"),
        transaction
            ? tr("Uncommitted transactions will be rolled back and active work cancelled.")
            : tr("Active database work will be cancelled and any uncommitted changes rolled back."),
        QMessageBox::NoButton, widgets_.dialogParent);
    box.setTextFormat(Qt::PlainText);
    auto* close =
        box.addButton(transaction ? tr("Roll back and close") : tr("Cancel work and close"),
                      QMessageBox::DestructiveRole);
    auto* cancel = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(cancel);
    box.exec();
    return box.clickedButton() == close;
}
void QueryWorkspace::beginShutdown() {
    stopping_ = true;
    updateActions();
    adapter_->beginShutdown();
}
void QueryWorkspace::cancelShutdown() {
    stopping_ = false;
    updateActions();
}
void QueryWorkspace::shutdown() {
    stopping_ = true;
    adapter_->shutdown();
    updateActions();
}
} // namespace choscordb
