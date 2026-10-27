#include "app/query_workspace.h"
#include "app/query_settings.h"
#include "app/query_workspace_p.h"
#include "app/result_filter_bar.h"
#include "bridge/engine_adapter.h"
#include "bridge/result_column_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/confirmation_dialog/confirmation_dialog.h"
#include "design_system/menu/menu.h"
#include "design_system/modal_panel/modal_panel.h"
#include "design_system/right_sheet/right_sheet.h"
#include "design_system/status_line/status_line.h"
#include "design_system/table/table_style.h"
#include "widgets/export_dialog/export_dialog.h"
#include "widgets/profile_dialog/profile_dialog.h"
#include "widgets/sql_editor/sql_editor.h"
#include "widgets/value_detail_dialog/value_detail_dialog.h"

#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QLayout>
#include <QMenu>
#include <QMessageBox>
#include <QPersistentModelIndex>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariantMap>
namespace choscordb {
using query_workspace_detail::nextEditRequestToken;
using query_workspace_detail::text;
QueryWorkspace::QueryWorkspace(Widgets widgets, QObject* parent)
    : QObject(parent), widgets_(std::move(widgets)),
      adapter_(widgets_.sharedAdapter ? widgets_.sharedAdapter
                                      : new EngineAdapter(this, widgets_.storagePath)),
      model_(new ResultTableModel(this)) {
    widgets_.grid->setModel(model_);
    if (auto* delegate = qobject_cast<design::ResultTableDelegate*>(widgets_.grid->itemDelegate()))
        connect(delegate, &design::ResultTableDelegate::linkActivated, this,
                &QueryWorkspace::activateForeignKey);
    widgets_.grid->horizontalHeader()->setContextMenuPolicy(Qt::PreventContextMenu);
    widgets_.grid->verticalHeader()->setContextMenuPolicy(Qt::PreventContextMenu);
    // Set NULL and the row commands act on the selection.
    connect(widgets_.grid->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] {
        ++selectionGeneration_;
        updateSetNullAction();
        updateGridActions();
    });
    setupGridCommands();
    setupResultViewControls();
    if (!widgets_.objectReadOnly)
        widgets_.grid->setToolTip(
            tr("Query results are read only because the source table and key cannot be verified."));
    querySettings_ = new QuerySettingsController(adapter_, widgets_.dialogParent, this);
    setupCellEditor();
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
            export_->setResultViewActive(!viewFilters_.isEmpty() || viewSortColumn_ >= 0);
            export_->show();
            export_->raise();
        });
    const auto openDetail = [this](const QModelIndex& index) {
        if (index.model() != model_ || widgets_.grid->model() != model_)
            return;
        const auto cell = model_->cellValue(index);
        if (!cell)
            return;
        if (const auto* fallback = std::get_if<FallbackText>(&*cell)) {
            if (!detail_)
                detail_ = new ValueDetailDialog(adapter_, widgets_.dialogParent);
            if (const auto error = detail_->openInlineValue(
                    fallback->text.toUtf8(),
                    tr("Server text fallback · %1").arg(fallback->databaseType), false);
                !error.isEmpty())
                message(error);
            return;
        }
        if (const auto* binary = std::get_if<QByteArray>(&*cell)) {
            if (!detail_)
                detail_ = new ValueDetailDialog(adapter_, widgets_.dialogParent);
            const auto type =
                model_->headerData(index.column(), Qt::Horizontal, ResultTableModel::HeaderTypeRole)
                    .toString();
            if (const auto error =
                    detail_->openInlineValue(*binary, tr("Binary · %1").arg(type), true);
                !error.isEmpty())
                message(error);
            return;
        }
        if (!query_ || !queryAvailable())
            return;
        const auto value = model_->deferredValue(index);
        if (!value)
            return;
        if (!detail_)
            detail_ = new ValueDetailDialog(adapter_, widgets_.dialogParent);
        detail_->openValue(*query_, value->handle,
                           value->fallback ? tr("Server text fallback · %1").arg(value->type)
                                           : value->type,
                           value->bytes);
    };
    connect(widgets_.grid, &QTableView::doubleClicked, this, openDetail);
    connect(widgets_.grid, &QTableView::activated, this, openDetail);
    connect(model_, &ResultTableModel::pendingEditsChanged, this, [this](bool) {
        ++editPolicyGeneration_;
        if (!editabilityPlanning_) {
            updateActions();
            return;
        }
        // Coalesce replanning for edits staged while eligibility is being planned.
        if (editabilityReplanQueued_)
            return;
        editabilityReplanQueued_ = true;
        QTimer::singleShot(0, this, [this] {
            editabilityReplanQueued_ = false;
            if (editabilityPlanning_)
                configureEditability();
        });
    });
    connect(adapter_, &EngineAdapter::eventReady, this, &QueryWorkspace::handleEvent);
    connect(adapter_, &EngineAdapter::eventReady, this, &QueryWorkspace::handleRowJsonEvent);
    connect(adapter_, &EngineAdapter::eventReady, this, &QueryWorkspace::handleCopyEvent);
    connect(adapter_, &EngineAdapter::valueChunkSubmissionFailed, this,
            [this](quint64 query, quint64 handle, quint64 offset, const QString& error) {
                if (rowJsonQuery_ == query && rowJsonLoadingColumn_ >= 0 &&
                    rowJsonLoadingHandle_ == handle && rowJsonLoadingOffset_ == offset)
                    failRowJson(error);
                if (pendingCopy_ && pendingCopy_->query == query &&
                    pendingCopy_->next < pendingCopy_->deferred.size()) {
                    const auto [row, column] = pendingCopy_->deferred[pendingCopy_->next];
                    const auto value = model_->deferredValue(model_->index(row, column));
                    if (value && value->handle == handle && pendingCopy_->offset == offset)
                        failCopy(error);
                }
            });
    connect(model_, &QAbstractItemModel::modelReset, this, [this] {
        ++selectionGeneration_;
        clearRowJson();
        pendingCopy_.reset();
    });
    connect(model_, &QAbstractItemModel::rowsInserted, this, [this] {
        ++selectionGeneration_;
        pendingCopy_.reset();
    });
    connect(model_, &QAbstractItemModel::rowsRemoved, this, [this] {
        ++selectionGeneration_;
        pendingCopy_.reset();
    });
    connect(model_, &QAbstractItemModel::layoutChanged, this, [this] { ++selectionGeneration_; });
    connect(model_, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex& first, const QModelIndex& last) {
                if (rowJsonIndex_.isValid() &&
                    (rowJsonMode_ == JsonViewMode::Table ||
                     (first.row() <= rowJsonIndex_.row() && last.row() >= rowJsonIndex_.row() &&
                      (rowJsonMode_ != JsonViewMode::Cell ||
                       (first.column() <= rowJsonIndex_.column() &&
                        last.column() >= rowJsonIndex_.column())))))
                    clearRowJson();
            });
    connect(model_, &QAbstractItemModel::rowsInserted, this, [this] {
        if (rowJsonIndex_.isValid())
            clearRowJson();
    });
    connect(model_, &QAbstractItemModel::rowsRemoved, this, [this] {
        if (rowJsonIndex_.isValid())
            clearRowJson();
    });
    connect(adapter_, &EngineAdapter::commandFailed, this, [this](const QString& error) {
        commandError_ = error;
        if (fetching_) {
            busy_ = fetching_ = false;
            setExecutionState(QStringLiteral("failed"),
                              tr("Failed to load result page: %1").arg(error));
        }
        message(error);
        updateActions();
    });
    connect(widgets_.run, &QAction::triggered, this, [this] { execute(RunScope::Current); });
    connect(widgets_.runAll, &QAction::triggered, this, [this] { execute(RunScope::All); });
    connect(widgets_.cancel, &QAction::triggered, this, [this] {
        if (editPlanRunning_)
            return;
        if (query_ && viewBusy_) {
            if (adapter_->cancelResultView(*query_)) {
                setExecutionState(QStringLiteral("cancelling"), tr("Cancelling result view…"));
                updateActions();
            }
            return;
        }
        if (query_ && queryAvailable()) {
            if (!adapter_->cancelQuery(*query_))
                return;
            cancellationPending_ = true;
            dropPageBuild();
            setExecutionState(QStringLiteral("cancelling"), tr("Cancelling…"));
            updateActions();
            widgets_.cancel->setEnabled(false);
        }
    });
    connect(widgets_.newConnection, &QAction::triggered, this, [this] { showProfiles(); });
    connect(widgets_.nextPage, &QPushButton::clicked, this, [this] {
        if (query_ && (hasMore_ || hasMoreResults_) && !fetching_ && queryAvailable() &&
            !exporting_) {
            if (!resolvePendingEdits())
                return;
            fetching_ = true;
            busy_ = true;
            setExecutionState(QStringLiteral("running"), tr("Loading result page…"));
            updateActions();
            if (hasMore_) {
                adapter_->fetchPageAt(*query_, currentPage_.value_or(0) + 1);
            } else {
                hasMoreResults_ = false;
                currentPage_.reset();
                completedDurationMs_.reset();
                completedAffectedRows_.clear();
                executionFinished_ = false;
                clearViewState();
                adapter_->nextResultSet(*query_);
            }
        }
    });
    if (widgets_.previousPage)
        connect(widgets_.previousPage, &QPushButton::clicked, this, [this] {
            if (query_ && currentPage_ && *currentPage_ > 0 && !fetching_ && queryAvailable() &&
                !exporting_) {
                if (!resolvePendingEdits())
                    return;
                fetching_ = true;
                setExecutionState(QStringLiteral("running"), tr("Loading result page…"));
                updateActions();
                adapter_->fetchPageAt(*query_, *currentPage_ - 1);
            }
        });
    auto* copy = new QShortcut(QKeySequence::Copy, widgets_.grid);
    copy->setContext(Qt::WidgetWithChildrenShortcut);
    connect(copy, &QShortcut::activated, this, [this] { copyResult(0); });
    widgets_.grid->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(
        widgets_.grid, &QTableView::customContextMenuRequested, this, [this](const QPoint& point) {
            QMenu menu(widgets_.grid);
            menu.setToolTipsVisible(true);
            const bool current = widgets_.grid->model() == model_ &&
                                 widgets_.grid->selectionModel() &&
                                 widgets_.grid->selectionModel()->model() == model_;
            const bool selected = current && widgets_.grid->selectionModel()->hasSelection();
            const QPersistentModelIndex clicked = widgets_.grid->indexAt(point);
            // The shared grid actions act on the cell under the pointer while the menu is open.
            menuTarget_ = current ? clicked : QPersistentModelIndex();
            updateGridActions();
            menu.addAction(gridAction(GridCommand::EditCell));
            menu.addSeparator();
            appendQuickFilterActions(menu, clicked);
            appendJsonViewActions(menu, clicked, current);
            menu.addSeparator();
            const QStringList names = {"copySelectedCells", "copySelectedRows", "copyCurrentPage"};
            const QStringList labels = {tr("Copy selected cells"), tr("Copy selected rows"),
                                        tr("Copy current page")};
            for (int scope = 0; scope < 3; ++scope) {
                auto* action = menu.addAction(labels[scope]);
                action->setObjectName(names[scope]);
                action->setEnabled(scope == 2 ? current && model_->rowCount() > 0 &&
                                                    model_->columnCount() > 0
                                              : selected);
                connect(action, &QAction::triggered, this, [this, scope] { copyResult(scope); });
            }
            if (widgets_.addRow) {
                menu.addSeparator();
                for (auto command :
                     {GridCommand::DuplicateRow, GridCommand::AddRow, GridCommand::DeleteRows,
                      GridCommand::RestoreRows, GridCommand::SetNull, GridCommand::ApplyEdits,
                      GridCommand::DiscardEdits})
                    menu.addAction(gridAction(command));
            }
            menu.addSeparator();
            menu.addAction(widgets_.cancel);
            const QPointer<QueryWorkspace> self(this);
            design::execContextMenu(menu, widgets_.grid->viewport()->mapToGlobal(point));
            if (!self)
                return;
            menuTarget_.reset();
            updateGridActions();
        });
    updateActions();
}
QueryWorkspace::~QueryWorkspace() {
    disconnect(model_, nullptr, this, nullptr);
    clearCellEditor();
    delete cellEditSheet_.data();
    clearResult();
    if (widgets_.objectReadOnly && adapter_ && query_)
        adapter_->releaseQuery(*query_);
    delete detail_;
    delete export_;
    delete profiles_;
}
bool QueryWorkspace::resolvePendingEdits() {
    if (editPlanRunning_ || editApplying_)
        return false;
    if (!model_->hasPendingEdits())
        return true;
    ConfirmationDialog box(QMessageBox::Warning, tr("Pending grid changes"),
                           tr("Apply or discard the staged grid changes before continuing."),
                           QMessageBox::NoButton, widgets_.dialogParent);
    auto* apply = box.addButton(tr("Apply…"), QMessageBox::AcceptRole);
    auto* discard = box.addButton(tr("Discard"), QMessageBox::DestructiveRole);
    auto* cancel = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(cancel);
    const QPointer<QueryWorkspace> self(this);
    box.exec();
    if (!self)
        return false;
    if (box.clickedButton() == discard) {
        model_->discardEdits();
        return true;
    }
    if (box.clickedButton() == apply && widgets_.applyEdits) {
        return applyStagedEdits();
    }
    return false;
}
bool QueryWorkspace::connectionAvailable(quint64 connection) const {
    return connectionCanDisconnect(connection);
}
bool QueryWorkspace::connectionCanDisconnect(quint64 connection) const {
    return !disconnecting_.contains(connection) && liveConnectionLabel(connection).has_value();
}
std::optional<QString> QueryWorkspace::liveConnectionLabel(quint64 connection) const {
    for (const auto& live : liveConnections_)
        if (live.id == connection)
            return live.label;
    return std::nullopt;
}
std::optional<QueryWorkspace::LiveConnection>
QueryWorkspace::liveConnectionForProfile(const QString& profileId) const {
    if (profileId.isEmpty())
        return std::nullopt;
    for (const auto& live : liveConnections_)
        if (connectionProfiles_.value(live.id) == profileId)
            return live;
    return std::nullopt;
}
bool QueryWorkspace::changeDocumentConnection(SqlEditor* editor, quint64 connection) {
    const auto label = liveConnectionLabel(connection);
    if (!editor || !label)
        return false;
    if (workInFlight()) {
        message(tr("Finish or cancel the active query before changing its connection."));
        return false;
    }
    editor->setConnectionTarget(connection, *label);
    editor->setProfileId(connectionProfiles_.value(connection));
    if (editor == widgets_.currentEditor())
        documentChanged();
    return true;
}
bool QueryWorkspace::queryAvailable() const {
    return queryConnection_ && connectionAvailable(*queryConnection_);
}
bool QueryWorkspace::workInFlight() const {
    return quickFilterPreparing_ || externalWorkActive() || executionModeToken_ != 0 || viewBusy_ ||
           editPlanRunning_ || editApplying_ ||
           ((cancellationPending_ || busy_ || fetching_ || exporting_) &&
            !(queryConnection_ && disconnecting_.contains(*queryConnection_)));
}
void QueryWorkspace::setExternalWork(bool busy) {
    if (externalWork_ == busy)
        return;
    externalWork_ = busy;
    updateActions();
}
void QueryWorkspace::setExternalWork(QObject* source, bool busy) {
    if (!source || externalWorkSources_.contains(source) == busy)
        return;
    if (busy) {
        externalWorkSources_.insert(
            source, connect(source, &QObject::destroyed, this, [this, source] {
                if (externalWorkSources_.remove(source))
                    QTimer::singleShot(0, this, &QueryWorkspace::updateActions);
            }));
    } else {
        disconnect(externalWorkSources_.take(source));
    }
    updateActions();
}
const TransactionGuard& QueryWorkspace::editTransactionGuard() const {
    const bool active =
        queryConnection_ &&
        (pendingTransactions_.contains(*queryConnection_) ||
         (widgets_.transactionActive && widgets_.transactionActive(*queryConnection_)));
    return EngineAdapter::transactionGuard(active);
}
void QueryWorkspace::disconnectConnection(quint64 connection) {
    if (editPlanRunning_ || !connectionCanDisconnect(connection) ||
        confirmingDisconnects_.contains(connection))
        return;
    const auto label = liveConnectionLabel(connection).value_or(QString{});
    const bool transaction = pendingTransactions_.contains(connection);
    const bool active = queryConnection_ == connection &&
                        (busy_ || fetching_ || viewBusy_ || exporting_ || !executionFinished_);
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
    if (box.clickedButton() != accept || !connectionCanDisconnect(connection))
        return;
    disconnecting_.insert(connection);
    updateActions();
    emit documentTargetChanged();
    if (!adapter_->disconnectConnection(connection)) {
        disconnecting_.remove(connection);
        updateActions();
        emit documentTargetChanged();
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
void QueryWorkspace::showProfiles(const QString& profileId) {
    if (stopping_ || workInFlight()) {
        message(tr("Finish or cancel active database work before managing connections."));
        return;
    }
    const bool existingDialog = profiles_ != nullptr;
    if (!profiles_) {
        profiles_ = new ProfileDialog(adapter_, widgets_.dialogParent);
        connect(profiles_, &ProfileDialog::openQueryRequested, this,
                &QueryWorkspace::openQueryRequested);
        connect(profiles_, &ProfileDialog::connectionSubmitted, this,
                [this](const SavedProfile& profile, quint64 id, bool savedProfile) {
                    pendingConnections_.insert(id, profile.name);
                    connectionAttemptEditors_.insert(id, widgets_.currentEditor());
                    connectionAttemptStatus_.insert(
                        id, widgets_.currentEditor()
                                ? widgets_.currentEditor()->property("resultStatus").toMap()
                                : QVariantMap{});
                    setDocumentStatus(widgets_.currentEditor(), QStringLiteral("queued"),
                                      tr("Connecting…"), profile.name);
                    connectionDrivers_.insert(id, profile.driver);
                    if (savedProfile)
                        connectionProfiles_.insert(id, profile.id);
                });
    }
    if (!profileId.isEmpty())
        profiles_->manageProfile(profileId, "edit");
    else if (existingDialog)
        profiles_->newProfile();
    profiles_->show();
    profiles_->raise();
}
void QueryWorkspace::manageSavedProfile(const QString& profileId, const QString& action) {
    if (stopping_ || workInFlight()) {
        message(tr("Finish or cancel active database work before managing connections."));
        return;
    }
    showProfiles(profileId);
    if (profiles_)
        profiles_->manageProfile(profileId, action);
}
void QueryWorkspace::showQuerySettings() {
    if (!stopping_)
        querySettings_->open();
}
void QueryWorkspace::trackQueryPreferencesSave(quint64 token) {
    querySettings_->trackSave(token);
}
void QueryWorkspace::applyQueryPreferences(const QueryPreferences& preferences) {
    querySettings_->applyConfirmed(preferences);
}
QueryPreferences QueryWorkspace::queryPreferences() const {
    return querySettings_->preferences();
}
bool QueryWorkspace::openObjectData(quint64 connection, const QString& object, const QString& label,
                                    const QueryPreferences& preferences, bool preserveView,
                                    const QString& initialFilter, const ObjectDataTime& time) {
    if (!widgets_.objectReadOnly || !adapter_ || workInFlight() || stopping_)
        return false;
    if (!resolvePendingEdits())
        return false;
    preserveViewOnRefresh_ = preserveView;
    viewRefreshQuery_.reset();
    if (!preserveView)
        clearViewState();
    if (query_)
        adapter_->releaseQuery(*query_);
    clearResult();
    initialFilter_ = initialFilter;
    initialFilterPending_ = !initialFilter.isEmpty();
    referenceFilterPending_ = initialFilterPending_;
    referenceFilterFailed_ = false;
    if (filterBar_ && initialFilterPending_)
        filterBar_->setExpression(initialFilter_);
    currentPage_.reset();
    hasMore_ = false;
    hasMoreResults_ = false;
    fetching_ = false;
    invalidatePending_ = false;
    cancellationPending_ = false;
    queryConnection_ = connection;
    if (!liveConnectionLabel(connection))
        liveConnections_.append({connection, label});
    resultOrigin_ = label;
    objectId_ = object;
    objectTime_ = time;
    editQualifiedName_.clear();
    editReason_ = tr("Checking edit eligibility…");
    editColumnNames_.clear();
    editKey_.clear();
    editGenerated_.clear();
    editDuplicable_.clear();
    widgets_.messages->clear();
    message(tr("Object data: %1").arg(label));
    commandError_.clear();
    query_ = adapter_->openObjectData(connection, object, preferences, time);
    if (preserveView && query_)
        viewRefreshQuery_ = query_;
    else if (preserveView && !query_) {
        preserveViewOnRefresh_ = false;
        clearViewState();
        message(tr("The previous result view was invalidated because refresh could not start."));
    }
    // The driver checks the catalog; a navigator label can be stale or approximate.
    if (query_) {
        editTargetToken_ = nextEditRequestToken();
        adapter_->inspectEditTarget(connection, object, editTargetToken_, time);
    }
    busy_ = query_.has_value();
    executionFinished_ = !busy_;
    setExecutionState(busy_ ? QStringLiteral("queued") : QStringLiteral("failed"),
                      busy_ ? tr("Loading object data…")
                            : tr("Object read could not be submitted: %1").arg(commandError_));
    updateActions();
    return true;
}
void QueryWorkspace::invalidateResult(bool connectionLost) {
    if (!resolvePendingEdits())
        return;
    connectionLost = connectionLost || invalidateConnectionLost_;
    if (workInFlight()) {
        invalidatePending_ = true;
        invalidateConnectionLost_ = connectionLost;
        if (exporting_ && export_)
            export_->clearQuery();
        if (adapter_ && query_ && viewBusy_)
            adapter_->cancelResultView(*query_);
        else if (adapter_ && query_ && (busy_ || fetching_))
            cancellationPending_ = adapter_->cancelQuery(*query_);
        if (cancellationPending_)
            dropPageBuild();
        if (!connectionLost || widgets_.summary->property("state") != "failed")
            setExecutionState(QStringLiteral("cancelling"), tr("Cancelling…"));
        updateActions();
        return;
    }
    invalidateConnectionLost_ = false;
    invalidatePending_ = false;
    cancellationPending_ = false;
    clearResult();
    if (adapter_ && query_)
        adapter_->releaseQuery(*query_);
    query_.reset();
    queryConnection_.reset();
    currentPage_.reset();
    hasMore_ = false;
    hasMoreResults_ = false;
    resultOrigin_.clear();
    clearViewState();
    if (!connectionLost || widgets_.summary->property("state") != "failed")
        setExecutionState(
            QStringLiteral("disconnected"),
            connectionLost ? tr("Connection unavailable. %1")
                                 .arg(EngineAdapter::recoveryHint(RecoveryHint::ReconnectManually))
                           : tr("Open Data to read an object."));
    updateActions();
}
void QueryWorkspace::connectSqlite(const QString& path) {
    if (auto id = adapter_->connectSqlite(path)) {
        pendingConnections_.insert(*id, path);
        connectionAttemptEditors_.insert(*id, widgets_.currentEditor());
        connectionAttemptStatus_.insert(
            *id, widgets_.currentEditor()
                     ? widgets_.currentEditor()->property("resultStatus").toMap()
                     : QVariantMap{});
        setDocumentStatus(widgets_.currentEditor(), QStringLiteral("queued"), tr("Connecting…"),
                          path);
        connectionDrivers_.insert(*id, QStringLiteral("sqlite"));
    }
}
std::optional<quint64> QueryWorkspace::connectSavedProfile(const SavedProfile& profile) {
    if (stopping_ || workInFlight()) {
        message(tr("Finish or cancel active database work before connecting."));
        return std::nullopt;
    }
    const auto id = adapter_->connectProfile(profile);
    if (id) {
        pendingConnections_.insert(*id, profile.name);
        connectionAttemptEditors_.insert(*id, widgets_.currentEditor());
        connectionAttemptStatus_.insert(
            *id, widgets_.currentEditor()
                     ? widgets_.currentEditor()->property("resultStatus").toMap()
                     : QVariantMap{});
        setDocumentStatus(widgets_.currentEditor(), QStringLiteral("queued"), tr("Connecting…"),
                          profile.name);
        connectionProfiles_.insert(*id, profile.id);
        connectionDrivers_.insert(*id, profile.driver);
    }
    return id;
}
void QueryWorkspace::documentChanged() {
    auto* editor = widgets_.currentEditor();
    if (!widgets_.objectReadOnly && !workInFlight()) {
        const auto saved = editor ? editor->property("resultStatus").toMap() : QVariantMap{};
        const auto source = saved.value("source").toString();
        widgets_.messages->setPlainText(saved.value("messages").toString());
        presentExecutionState(
            saved.value("state", "disconnected").toString(),
            saved.value("detail", tr("Connect and run a query to see results.")).toString(),
            {saved.value("duration").toString(), saved.value("page").toString(),
             saved.value("rows").toString(), saved.value("memory").toString()},
            source.isEmpty() ? documentTarget(editor) : source);
    }
    updateActions();
    emit documentTargetChanged();
}
QString QueryWorkspace::documentTarget(const SqlEditor* editor) const {
    const auto target = editor ? editor->connectionTarget() : std::nullopt;
    if (const auto live = target ? liveConnectionLabel(*target) : std::nullopt)
        return *live;
    if (editor && !editor->targetLabel().isEmpty())
        return tr("Disconnected — %1").arg(editor->targetLabel());
    if (editor && !editor->property("profileId").toString().isEmpty())
        return tr("Disconnected profile — %1").arg(editor->property("profileId").toString());
    return tr("Disconnected — choose a SQL target");
}
std::optional<quint64> QueryWorkspace::selectedConnection() const {
    if (widgets_.objectReadOnly)
        return queryConnection_;
    const auto* editor = widgets_.currentEditor();
    return editor ? editor->connectionTarget() : std::nullopt;
}
void QueryWorkspace::updateActions() {
    if (invalidatePending_ && !workInFlight()) {
        invalidateResult();
        return;
    }
    const auto selected = selectedConnection();
    const bool connected = selected && connectionAvailable(*selected);
    const bool inFlight = workInFlight();
    const auto* editor = widgets_.currentEditor();
    const bool perDocumentResults =
        !widgets_.objectReadOnly &&
        qobject_cast<design::StatusLine*>(widgets_.summary->parentWidget());
    // A standalone display-model page may be supplied without a backend query.
    const bool standalonePage =
        !query_ && !resultEditor_ && resultOrigin_.isEmpty() && model_->columnCount() > 0;
    const bool displayedResult =
        !perDocumentResults || standalonePage ||
        (resultEditor_ && resultEditor_ == editor &&
         editor->property("resultStatus").toMap().value("source").toString() == resultOrigin_);
    widgets_.grid->setVisible(displayedResult);
    widgets_.run->setEnabled(connected && !inFlight && querySettings_->isReady());
    widgets_.runAll->setEnabled(widgets_.run->isEnabled());
    widgets_.cancel->setEnabled(!cancellationPending_ && query_.has_value() &&
                                (busy_ || viewBusy_ || executionModeToken_ != 0) &&
                                queryAvailable() &&
                                widgets_.summary->property("state") != "cancelling");
    widgets_.cancel->setText(widgets_.summary->property("state") == "cancelling" ? tr("Cancelling…")
                                                                                 : tr("Cancel"));
    if (widgets_.exportResult)
        widgets_.exportResult->setEnabled(displayedResult && query_ && currentPage_ && !inFlight &&
                                          queryAvailable());
    widgets_.nextPage->setEnabled(displayedResult && query_.has_value() &&
                                  (hasMore_ || hasMoreResults_) && !inFlight && queryAvailable());
    widgets_.nextPage->setToolTip(!hasMore_ && hasMoreResults_ ? tr("Next result")
                                                               : tr("Next page"));
    widgets_.nextPage->setAccessibleName(widgets_.nextPage->toolTip());
    if (widgets_.previousPage)
        widgets_.previousPage->setEnabled(displayedResult && query_ && currentPage_ &&
                                          *currentPage_ > 0 && !inFlight && queryAvailable());
    if (widgets_.addRow)
        widgets_.addRow->setEnabled(model_->canInsert() && !inFlight && !editabilityPlanning_);
    if (widgets_.addRow)
        widgets_.addRow->setToolTip(
            model_->canInsert()
                ? widgets_.addRow->accessibleName()
                : (editReason_.isEmpty() ? tr("This result is read only.") : editReason_));
    const bool canRemoveRows = model_->canDelete() || model_->hasInsertedRows();
    if (widgets_.deleteRows)
        widgets_.deleteRows->setEnabled(canRemoveRows && model_->rowCount() && !inFlight &&
                                        !editabilityPlanning_);
    if (widgets_.deleteRows)
        widgets_.deleteRows->setToolTip(canRemoveRows || editReason_.isEmpty()
                                            ? widgets_.deleteRows->accessibleName()
                                            : editReason_);
    if (widgets_.restoreRows)
        widgets_.restoreRows->setEnabled(!inFlight && !editabilityPlanning_ &&
                                         model_->hasDeletedRows());
    setNullAvailable_ = !inFlight && !editabilityPlanning_ && displayedResult;
    updateSetNullAction();
    if (widgets_.applyEdits)
        widgets_.applyEdits->setEnabled(model_->hasPendingEdits() && !inFlight &&
                                        !editabilityPlanning_ &&
                                        editTransactionGuard().applyEdits.isEmpty());
    if (widgets_.discardEdits)
        widgets_.discardEdits->setEnabled(model_->hasPendingEdits() && !inFlight &&
                                          !editabilityPlanning_);
    emit activityChanged(inFlight);
    if (!displayedResult) {
        for (auto* button : {widgets_.addRow, widgets_.deleteRows, widgets_.restoreRows,
                             widgets_.setNull, widgets_.applyEdits, widgets_.discardEdits})
            if (button)
                button->setEnabled(false);
    }
    updateGridActions();
    updateApplyToolTip();
}
void QueryWorkspace::updateSetNullAction() {
    if (!widgets_.setNull)
        return;
    const auto* selection = widgets_.grid->selectionModel();
    widgets_.setNull->setEnabled(setNullAvailable_ && selection &&
                                 model_->hasEditableCell(selection->selection()));
}
void QueryWorkspace::execute(RunScope scope) {
    if (widgets_.objectReadOnly)
        return;
    auto connection = selectedConnection();
    auto* editor = widgets_.currentEditor();
    if (!connection || !editor || !connectionAvailable(*connection) || workInFlight() ||
        !querySettings_->isReady())
        return;
    if (!resolvePendingEdits())
        return;
    // The mode from the connect or the last completed run stays valid until a run fails.
    if (EngineAdapter::driverWorkflow(driverForConnection(*connection)).sqlModeBeforeExecution &&
        !executionModeReady_ && !connectionSqlModes_.contains(*connection)) {
        if (executionModeToken_ != 0)
            return;
        executionModeToken_ = nextEditRequestToken();
        executionModeScope_ = scope;
        executionModeEditor_ = editor;
        executionModeSql_ = editor->text();
        executionModeCursor_ =
            static_cast<quint64>(editor->SendScintilla(QsciScintilla::SCI_GETCURRENTPOS));
        executionModeStart_ =
            static_cast<quint64>(editor->SendScintilla(QsciScintilla::SCI_GETSELECTIONSTART));
        executionModeEnd_ =
            static_cast<quint64>(editor->SendScintilla(QsciScintilla::SCI_GETSELECTIONEND));
        executionModeConnection_ = connection;
        if (!adapter_->refreshSqlMode(*connection, executionModeToken_)) {
            executionModeToken_ = 0;
            executionModeConnection_.reset();
        }
        updateActions();
        return;
    }
    const auto sql = editor->text();
    const auto range = EngineAdapter::executionRange(
        sql, static_cast<quint64>(editor->SendScintilla(QsciScintilla::SCI_GETCURRENTPOS)),
        static_cast<quint64>(editor->SendScintilla(QsciScintilla::SCI_GETSELECTIONSTART)),
        static_cast<quint64>(editor->SendScintilla(QsciScintilla::SCI_GETSELECTIONEND)), scope,
        driverForConnection(*connection), connectionSqlModes_.value(*connection));
    if (!range.valid) {
        message(scope == RunScope::All ? tr("No executable statement in the document.")
                                       : tr("No executable statement at the cursor."));
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
    if (resultEditor_ && resultEditor_ != editor &&
        resultEditor_->property("resultStatus").toMap().value("state") == "completed")
        resultEditor_->setProperty("resultStatus", QVariant{});
    if (query_ && queryAvailable())
        adapter_->releaseQuery(*query_);
    clearViewState();
    const auto bytes = sql.toUtf8();
    executedSql_ = QString::fromUtf8(bytes.mid(static_cast<qsizetype>(range.start),
                                               static_cast<qsizetype>(range.end - range.start)));
    commandError_.clear();
    query_ = adapter_->execute(*connection, executedSql_, connectionProfiles_.value(*connection),
                               querySettings_->preferences());
    if (query_)
        editor->setProfileId(connectionProfiles_.value(*connection));
    queryConnection_ = connection;
    auto title = editor->filePath().isEmpty() ? editor->property("documentTitle").toString()
                                              : QFileInfo(editor->filePath()).fileName();
    if (title.isEmpty())
        title = tr("Untitled query");
    resultEditor_ = editor;
    resultOrigin_ = tr("%1 — %2").arg(title, editor->targetLabel());
    widgets_.messages->clear();
    message(tr("SQL result: %1").arg(resultOrigin_));
    clearResult();
    currentPage_.reset();
    hasMore_ = false;
    hasMoreResults_ = false;
    fetching_ = false;
    busy_ = query_.has_value();
    executionFinished_ = !query_.has_value();
    cancellationPending_ = false;
    setExecutionState(busy_ ? QStringLiteral("queued") : QStringLiteral("failed"),
                      busy_ ? tr("Queued") : tr("Submission failed: %1").arg(commandError_));
    if (!busy_ && !commandError_.isEmpty())
        message(commandError_);
    updateActions();
}
} // namespace choscordb
