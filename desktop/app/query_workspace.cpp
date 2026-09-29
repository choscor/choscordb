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
#include "design_system/table/table_style.h"
#include "widgets/export_dialog/export_dialog.h"
#include "widgets/profile_dialog/profile_dialog.h"
#include "widgets/sql_editor/sql_editor.h"
#include "widgets/value_detail_dialog/value_detail_dialog.h"
#include <QAction>
#include <QComboBox>
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
#include <algorithm>
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
    connect(widgets_.grid->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this] { updateActions(); });
    connect(widgets_.grid->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex&, const QModelIndex&) { updateActions(); });
    setupResultViewControls();
    if (!widgets_.objectReadOnly)
        widgets_.grid->setToolTip(
            tr("Query results are read only because the source table and key cannot be verified."));
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
            if (!detail_->openInlineValue(
                    fallback->text.toUtf8(),
                    tr("Server text fallback · %1").arg(fallback->databaseType), false))
                message(tr("The inline value exceeds the 8 MiB detail limit."));
            return;
        }
        if (const auto* binary = std::get_if<QByteArray>(&*cell)) {
            if (!detail_)
                detail_ = new ValueDetailDialog(adapter_, widgets_.dialogParent);
            const auto type =
                model_->headerData(index.column(), Qt::Horizontal, ResultTableModel::HeaderTypeRole)
                    .toString();
            if (!detail_->openInlineValue(*binary, tr("Binary · %1").arg(type), true))
                message(tr("The inline value exceeds the 8 MiB detail limit."));
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
        if (editabilityPlanning_)
            configureEditability();
        else
            updateActions();
    });
    if (widgets_.addRow)
        connect(widgets_.addRow, &QPushButton::clicked, model_, &ResultTableModel::addRow);
    if (widgets_.deleteRows)
        connect(widgets_.deleteRows, &QPushButton::clicked, this, [this] {
            if (!widgets_.grid->selectionModel())
                return;
            const auto selection = widgets_.grid->selectionModel()->selectedIndexes();
            for (const auto& index : selection)
                if (index.isValid() && index.model() == model_ && index.row() >= 0 &&
                    index.row() < model_->rowCount() &&
                    std::any_of(model_->rows()[index.row()].begin(),
                                model_->rows()[index.row()].end(), [](const Cell& value) {
                                    return std::holds_alternative<FallbackText>(value) ||
                                           std::holds_alternative<UnavailableValue>(value);
                                })) {
                    message(tr("Rows with fallback or unavailable values cannot be deleted."));
                    break;
                }
            model_->markDeleted(selection, true);
        });
    if (widgets_.restoreRows)
        connect(widgets_.restoreRows, &QPushButton::clicked, this, [this] {
            if (widgets_.grid->selectionModel())
                model_->markDeleted(widgets_.grid->selectionModel()->selectedIndexes(), false);
        });
    if (widgets_.setNull)
        connect(widgets_.setNull, &QPushButton::clicked, this, [this] {
            const auto selection = widgets_.grid->selectionModel()->selectedIndexes();
            for (const auto& index : selection)
                model_->setNull(index);
        });
    if (widgets_.discardEdits)
        connect(widgets_.discardEdits, &QPushButton::clicked, model_,
                &ResultTableModel::discardEdits);
    if (widgets_.applyEdits)
        connect(widgets_.applyEdits, &QPushButton::clicked, this,
                &QueryWorkspace::applyStagedEdits);
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
        clearRowJson();
        pendingCopy_.reset();
    });
    connect(model_, &QAbstractItemModel::rowsInserted, this, [this] { pendingCopy_.reset(); });
    connect(model_, &QAbstractItemModel::rowsRemoved, this, [this] { pendingCopy_.reset(); });
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
        if (fetching_) {
            busy_ = fetching_ = false;
            setExecutionState(QStringLiteral("failed"), tr("! Failed to load result page"));
        }
        message(error);
        updateActions();
    });
    connect(widgets_.run, &QAction::triggered, this, &QueryWorkspace::execute);
    connect(widgets_.cancel, &QAction::triggered, this, [this] {
        if (editPlanRunning_)
            return;
        if (query_ && viewBusy_) {
            if (adapter_->cancelResultView(*query_)) {
                setExecutionState(QStringLiteral("cancelling"), tr("◷ Cancelling result view…"));
                updateActions();
            }
            return;
        }
        if (query_ && queryAvailable()) {
            if (!adapter_->cancelQuery(*query_))
                return;
            cancellationPending_ = true;
            setExecutionState(QStringLiteral("cancelling"), tr("◷ Cancelling…"));
            updateActions();
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
    connect(widgets_.newConnection, &QAction::triggered, this, [this] { showProfiles(); });
    connect(widgets_.connections, &QComboBox::currentIndexChanged, this, [this] {
        if (auto* editor = widgets_.currentEditor()) {
            if (workInFlight()) {
                documentChanged();
                message(tr("Finish or cancel the active query before changing its connection."));
                return;
            }
            const auto value = widgets_.connections->currentData();
            editor->setConnectionTarget(
                value.isValid() ? std::optional<quint64>(value.toULongLong()) : std::nullopt,
                widgets_.connections->currentText());
            editor->setProfileId(value.isValid() ? connectionProfiles_.value(value.toULongLong())
                                                 : QString{});
        }
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
        if (query_ && (hasMore_ || hasMoreResults_) && !fetching_ && queryAvailable() &&
            !exporting_) {
            if (!resolvePendingEdits())
                return;
            fetching_ = true;
            busy_ = true;
            setExecutionState(QStringLiteral("running"), tr("◷ Loading result page…"));
            updateActions();
            if (hasMore_) {
                adapter_->fetchPageAt(*query_, currentPage_.value_or(0) + 1);
            } else {
                hasMoreResults_ = false;
                currentPage_.reset();
                completedDurationMs_.reset();
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
                setExecutionState(QStringLiteral("running"), tr("◷ Loading result page…"));
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
            if (widgets_.addRow || widgets_.deleteRows || widgets_.restoreRows ||
                widgets_.setNull) {
                menu.addSeparator();
                if (widgets_.addRow) {
                    auto* duplicate = menu.addAction(tr("Duplicate row"));
                    duplicate->setObjectName("duplicateRow");
                    duplicate->setEnabled(current && clicked.isValid() && model_->canInsert() &&
                                          !workInFlight());
                    duplicate->setToolTip(model_->canInsert()
                                              ? tr("Duplicate the row under the pointer")
                                              : (editReason_.isEmpty()
                                                     ? tr("This result is read only.")
                                                     : editReason_));
                    connect(duplicate, &QAction::triggered, this, [this, clicked] {
                        if (!clicked.isValid() || clicked.model() != model_)
                            return;
                        QString error;
                        bool duplicated = false;
                        if (editKey_.size() == static_cast<size_t>(model_->columnCount())) {
                            std::vector<bool> copyable(editKey_.size());
                            for (size_t column = 0; column < copyable.size(); ++column)
                                copyable[column] = !editKey_[column] && !editGenerated_[column] &&
                                                   !editColumnNames_[column].isEmpty();
                            duplicated = model_->duplicateRow(clicked.row(), copyable, &error);
                        } else {
                            duplicated = model_->duplicateRow(clicked.row(), &error);
                        }
                        if (!duplicated && !error.isEmpty())
                            message(error);
                    });
                }
                for (auto* button : {widgets_.addRow, widgets_.deleteRows, widgets_.restoreRows,
                                     widgets_.setNull}) {
                    if (!button)
                        continue;
                    auto* action = menu.addAction(button->accessibleName());
                    action->setObjectName(button->objectName());
                    action->setEnabled(button->isEnabled());
                    action->setToolTip(button->toolTip());
                    connect(action, &QAction::triggered, button, &QPushButton::click);
                }
            }
            menu.addSeparator();
            menu.addAction(widgets_.cancel);
            design::execContextMenu(menu, widgets_.grid->viewport()->mapToGlobal(point));
        });
    updateActions();
}
QueryWorkspace::~QueryWorkspace() {
    disconnect(model_, nullptr, this, nullptr);
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
    box.exec();
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
    return !disconnecting_.contains(connection) &&
           widgets_.connections->findData(QVariant::fromValue<qulonglong>(connection)) >= 0;
}
bool QueryWorkspace::queryAvailable() const {
    return queryConnection_ && connectionAvailable(*queryConnection_);
}
bool QueryWorkspace::workInFlight() const {
    return externalWork_ || executionModeToken_ != 0 || viewBusy_ || editPlanRunning_ ||
           editApplying_ ||
           ((cancellationPending_ || busy_ || fetching_ || exporting_) &&
            !(queryConnection_ && disconnecting_.contains(*queryConnection_)));
}
void QueryWorkspace::setExternalWork(bool busy) {
    if (externalWork_ == busy)
        return;
    externalWork_ = busy;
    updateActions();
}
void QueryWorkspace::disconnectConnection(quint64 connection) {
    if (editPlanRunning_ || !connectionCanDisconnect(connection) ||
        confirmingDisconnects_.contains(connection))
        return;
    const auto index = widgets_.connections->findData(QVariant::fromValue<qulonglong>(connection));
    const auto label = widgets_.connections->itemText(index);
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
void QueryWorkspace::openObjectData(quint64 connection, const QString& object, const QString& label,
                                    const QueryPreferences& preferences, const QString& kind,
                                    bool preserveView, const QString& initialFilter) {
    if (!widgets_.objectReadOnly || !adapter_ || workInFlight() || stopping_)
        return;
    if (!resolvePendingEdits())
        return;
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
    if (widgets_.connections->findData(QVariant::fromValue<qulonglong>(connection)) < 0) {
        const QSignalBlocker blocker(widgets_.connections);
        widgets_.connections->addItem(label, QVariant::fromValue<qulonglong>(connection));
    }
    resultOrigin_ = label;
    objectKind_ = kind;
    objectId_ = object;
    editQualifiedName_.clear();
    editReason_ = kind == QStringLiteral("table") ? tr("Checking table edit eligibility…")
                                                  : tr("Only base tables can be edited.");
    editColumnNames_.clear();
    editKey_.clear();
    editGenerated_.clear();
    widgets_.messages->clear();
    message(tr("Object data: %1").arg(label));
    query_ = adapter_->openObjectData(connection, object, preferences);
    if (preserveView && query_)
        viewRefreshQuery_ = query_;
    else if (preserveView && !query_) {
        preserveViewOnRefresh_ = false;
        clearViewState();
        message(tr("The previous result view was invalidated because refresh could not start."));
    }
    if (query_ && kind == QStringLiteral("table")) {
        editTargetToken_ = nextEditRequestToken();
        adapter_->inspectEditTarget(connection, object, editTargetToken_);
    }
    busy_ = query_.has_value();
    executionFinished_ = !busy_;
    setExecutionState(busy_ ? QStringLiteral("queued") : QStringLiteral("failed"),
                      busy_ ? tr("◷ Loading object data…")
                            : tr("! Object read could not be submitted"));
    updateActions();
}
void QueryWorkspace::invalidateResult() {
    if (!resolvePendingEdits())
        return;
    if (workInFlight()) {
        invalidatePending_ = true;
        if (exporting_ && export_)
            export_->clearQuery();
        if (adapter_ && query_ && viewBusy_)
            adapter_->cancelResultView(*query_);
        else if (adapter_ && query_ && (busy_ || fetching_))
            cancellationPending_ = adapter_->cancelQuery(*query_);
        setExecutionState(QStringLiteral("cancelling"), tr("◷ Cancelling…"));
        updateActions();
        return;
    }
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
    setExecutionState(QStringLiteral("disconnected"), tr("Open Data to read an object."));
    updateActions();
}
void QueryWorkspace::connectSqlite(const QString& path) {
    if (auto id = adapter_->connectSqlite(path)) {
        pendingConnections_.insert(*id, path);
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
        connectionProfiles_.insert(*id, profile.id);
        connectionDrivers_.insert(*id, profile.driver);
    }
    return id;
}
void QueryWorkspace::documentChanged() {
    auto* editor = widgets_.currentEditor();
    const auto target = editor ? editor->connectionTarget() : std::nullopt;
    const QSignalBlocker blocker(widgets_.connections);
    const int index =
        target ? widgets_.connections->findData(QVariant::fromValue<qulonglong>(*target)) : -1;
    QString label = tr("Disconnected — choose a SQL target");
    if (editor && !editor->targetLabel().isEmpty())
        label = tr("Disconnected — %1").arg(editor->targetLabel());
    else if (editor && !editor->property("profileId").toString().isEmpty())
        label = tr("Disconnected profile — %1").arg(editor->property("profileId").toString());
    widgets_.connections->setPlaceholderText(label);
    widgets_.connections->setCurrentIndex(index);
    widgets_.connections->setToolTip(
        index >= 0 ? tr("SQL document target: %1").arg(widgets_.connections->currentText())
                   : label);
    const QSignalBlocker modeBlocker(widgets_.mode);
    widgets_.mode->setCurrentIndex(target && manualModes_.value(*target, false) ? 1 : 0);
    updateActions();
    emit documentTargetChanged();
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
    widgets_.connections->setEnabled(widgets_.connections->count() > 0 && !inFlight && !stopping_);
    widgets_.mode->setEnabled(connected && !inFlight);
    widgets_.run->setEnabled(connected && !inFlight && querySettings_->isReady());
    widgets_.cancel->setEnabled(!cancellationPending_ && query_.has_value() &&
                                (busy_ || viewBusy_ || executionModeToken_ != 0) &&
                                queryAvailable() &&
                                widgets_.summary->property("state") != "cancelling");
    widgets_.cancel->setText(widgets_.summary->property("state") == "cancelling" ? tr("Cancelling…")
                                                                                 : tr("Cancel"));
    if (widgets_.exportResult)
        widgets_.exportResult->setEnabled(query_ && currentPage_ && !inFlight && queryAvailable());
    const bool manual = widgets_.mode->currentIndex() == 1;
    widgets_.commit->setEnabled(connected && manual && !inFlight);
    widgets_.rollback->setEnabled(connected && manual && !inFlight);
    widgets_.nextPage->setEnabled(query_.has_value() && (hasMore_ || hasMoreResults_) &&
                                  !inFlight && queryAvailable());
    widgets_.nextPage->setToolTip(!hasMore_ && hasMoreResults_ ? tr("Next result")
                                                               : tr("Next page"));
    widgets_.nextPage->setAccessibleName(widgets_.nextPage->toolTip());
    if (widgets_.previousPage)
        widgets_.previousPage->setEnabled(query_ && currentPage_ && *currentPage_ > 0 &&
                                          !inFlight && queryAvailable());
    if (widgets_.addRow)
        widgets_.addRow->setEnabled(model_->canInsert() && !inFlight && !editabilityPlanning_);
    if (widgets_.addRow)
        widgets_.addRow->setToolTip(
            model_->canInsert()
                ? widgets_.addRow->accessibleName()
                : (editReason_.isEmpty() ? tr("This result is read only.") : editReason_));
    const bool canRemoveRows =
        model_->canDelete() || std::any_of(model_->inserted().begin(), model_->inserted().end(),
                                           [](bool inserted) { return inserted; });
    if (widgets_.deleteRows)
        widgets_.deleteRows->setEnabled(canRemoveRows && model_->rowCount() && !inFlight &&
                                        !editabilityPlanning_);
    if (widgets_.deleteRows)
        widgets_.deleteRows->setToolTip(canRemoveRows || editReason_.isEmpty()
                                            ? widgets_.deleteRows->accessibleName()
                                            : editReason_);
    if (widgets_.restoreRows)
        widgets_.restoreRows->setEnabled(!inFlight && !editabilityPlanning_ &&
                                         std::any_of(model_->deleted().begin(),
                                                     model_->deleted().end(),
                                                     [](bool deleted) { return deleted; }));
    if (widgets_.setNull) {
        const auto selection = widgets_.grid->selectionModel()->selectedIndexes();
        widgets_.setNull->setEnabled(
            !inFlight && !editabilityPlanning_ &&
            std::any_of(selection.begin(), selection.end(), [this](const auto& index) {
                return model_->flags(index) & Qt::ItemIsEditable;
            }));
    }
    if (widgets_.applyEdits)
        widgets_.applyEdits->setEnabled(
            model_->hasPendingEdits() && !inFlight && !editabilityPlanning_ &&
            !(queryConnection_ &&
              (pendingTransactions_.contains(*queryConnection_) ||
               (widgets_.transactionActive && widgets_.transactionActive(*queryConnection_)))));
    if (widgets_.applyEdits)
        widgets_.applyEdits->setToolTip(
            queryConnection_ &&
                    (pendingTransactions_.contains(*queryConnection_) ||
                     (widgets_.transactionActive && widgets_.transactionActive(*queryConnection_)))
                ? tr("Commit or roll back the manual transaction before applying grid changes.")
                : widgets_.applyEdits->accessibleName());
    if (widgets_.discardEdits)
        widgets_.discardEdits->setEnabled(model_->hasPendingEdits() && !inFlight &&
                                          !editabilityPlanning_);
    emit activityChanged(inFlight);
}
void QueryWorkspace::execute() {
    if (widgets_.objectReadOnly)
        return;
    auto connection = selectedConnection();
    auto* editor = widgets_.currentEditor();
    if (!connection || !editor || !connectionAvailable(*connection) || workInFlight() ||
        !querySettings_->isReady())
        return;
    if (!resolvePendingEdits())
        return;
    if (driverForConnection(*connection) == "mysql" && !executionModeReady_) {
        if (executionModeToken_ != 0)
            return;
        executionModeToken_ = nextEditRequestToken();
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
        static_cast<quint64>(editor->SendScintilla(QsciScintilla::SCI_GETSELECTIONEND)),
        driverForConnection(*connection), connectionSqlModes_.value(*connection));
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
    clearViewState();
    const auto bytes = sql.toUtf8();
    executedSql_ = QString::fromUtf8(bytes.mid(static_cast<qsizetype>(range.start),
                                               static_cast<qsizetype>(range.end - range.start)));
    query_ =
        adapter_->execute(*connection, executedSql_, widgets_.mode->currentIndex() != 1,
                          connectionProfiles_.value(*connection), querySettings_->preferences());
    if (query_)
        editor->setProfileId(connectionProfiles_.value(*connection));
    if (query_ && widgets_.mode->currentIndex() == 1) {
        pendingTransactions_.insert(*connection);
        emit transactionStateChanged(*connection, true);
    }
    queryConnection_ = connection;
    auto title = editor->filePath().isEmpty() ? editor->property("documentTitle").toString()
                                              : QFileInfo(editor->filePath()).fileName();
    if (title.isEmpty())
        title = tr("Untitled query");
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
                      busy_ ? tr("◷ Queued") : tr("! Submission failed"));
    updateActions();
}
} // namespace choscordb
