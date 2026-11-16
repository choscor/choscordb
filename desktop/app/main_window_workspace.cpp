#include "app/main_window.h"

#include "app/appearance_controller.h"
#include "app/application_data.h"
#include "app/connection_tone.h"
#include "app/editor_preferences.h"
#include "app/main_window_ui.h"
#include "app/navigator_controller.h"
#include "app/object_explorer.h"
#include "app/query_workspace.h"
#include "app/workspace_recovery.h"
#include "bridge/engine_adapter.h"
#include "bridge/request_token.h"
#include "bridge/rust_text.h"
#include "bridge/text_filter.h"
#include "design_system/button/button.h"
#include "design_system/confirmation_dialog/confirmation_dialog.h"
#include "design_system/icons.h"
#include "design_system/menu/menu.h"
#include "design_system/navigation_profile_row/navigation_profile_row.h"
#include "design_system/status_line/status_line.h"
#include "design_system/table/table_style.h"
#include "design_system/text/text.h"
#include "design_system/theme_manager.h"
#include "models/navigator_model.h"
#include "widgets/editor_completion/editor_completion.h"
#include "widgets/filter_field/filter_field.h"
#include "widgets/screen_state/screen_state.h"
#include "widgets/sidebar_section/sidebar_section.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAbstractItemModel>
#include <QAction>
#include <QClipboard>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QHash>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableView>
#include <QTreeView>
#include <QTreeWidget>
#include <QtConcurrentRun>
#include <memory>
#include <utility>

namespace choscordb {
namespace {
using bridge_detail::fromRust;
using bridge_detail::utf8View;

QString prepareSavedSqlDirectory(const QString& root) {
    if (!root.isValidUtf16())
        return QObject::tr("Path is not valid Unicode.");
    const auto encoded = root.toUtf8();
    return fromRust(saved_sql_prepare_directory(utf8View(encoded)));
}

}

void MainWindow::connectWorkspace(const Ui& ui, const QString& storagePath) {
    const auto newQuery = ui.newQuery;
    const auto open = ui.open;
    const auto save = ui.save;
    const auto saveAs = ui.saveAs;
    const auto newConnection = ui.newConnection;
    const auto viewMenu = ui.viewMenu;
    const auto addConnection = ui.addConnection;
    const auto savedConnections = ui.savedConnections;
    const auto tree = ui.tree;
    const auto run = ui.run;
    const auto runAll = ui.runAll;
    const auto cancel = ui.cancel;
    const auto cancelButton = ui.cancelButton;
    const auto querySettings = ui.querySettings;
    const auto results = ui.results;
    const auto empty = ui.empty;
    const auto grid = ui.grid;
    const auto compactState = ui.compactState;
    const auto durationMetric = ui.durationMetric;
    const auto pageMetric = ui.pageMetric;
    const auto rowsMetric = ui.rowsMetric;
    const auto visibleSizeMetric = ui.visibleSizeMetric;
    const auto previousPage = ui.previousPage;
    const auto nextPage = ui.nextPage;
    const auto exportResult = ui.exportResult;
    const auto addResultRow = ui.addResultRow;
    const auto deleteResultRows = ui.deleteResultRows;
    const auto restoreResultRows = ui.restoreResultRows;
    const auto nullResultCell = ui.nullResultCell;
    const auto discardResultEdits = ui.discardResultEdits;
    const auto applyResultEdits = ui.applyResultEdits;
    const auto resultEditReason = ui.resultEditReason;
    const auto messages = ui.messages;
    connect(completion_, &EditorCompletionController::partialCatalog, this,
            [resultFooter = ui.resultFooter, partialShown = false](bool partial) mutable {
                if (partial && !partialShown)
                    resultFooter->showNotice(
                        tr("Suggestions use loaded navigator objects. Expand nodes for more "
                           "names; large catalogs may be limited."),
                        EngineAdapter::panelNoticeMs());
                partialShown = partial;
            });
    connect(newQuery, &QAction::triggered, this, [this] { addEditor(); });
    connect(addConnection, &QPushButton::clicked, this, [this, addConnection, newConnection] {
        auto* menu = new QMenu(addConnection);
        menu->setObjectName("navigatorAddMenu");
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->addAction(newConnection);
        menu->addSeparator();
        if (!navigatorController_->addCurrentCreateMenu(menu)) {
            delete menu;
            newConnection->trigger();
            return;
        }
        design::popupContextMenu(*menu,
                                 addConnection->mapToGlobal(addConnection->rect().bottomLeft()));
    });
    connect(editors_, &QTabWidget::tabCloseRequested, this, [this](int index) {
        if (!allowDocumentChange())
            return;
        auto* closing = editors_->widget(index);
        if (auto* object = qobject_cast<ObjectExplorer*>(closing);
            object && !object->resolvePendingChanges())
            return;
        auto* editor = qobject_cast<SqlEditor*>(closing);
        if (editor && editor->isModified() &&
            !ConfirmationDialog::confirm(
                this, tr("Discard changes?"), tr("The changes in this query are not saved."),
                tr("Discard changes"), ConfirmationDialog::Consequence::Destructive))
            return;
        if (!allowDocumentChange())
            return;
        editors_->removeTab(index);
        closing->deleteLater();
        if (!editors_->count())
            showScreen(Screen::Start);
        if (recovery_)
            recovery_->changed();
    });
    connect(open, &QAction::triggered, this, [this] {
        if (!allowDocumentChange())
            return;
        const auto path = QFileDialog::getOpenFileName(this, tr("Open SQL file"), {},
                                                       tr("SQL files (*.sql);;All files (*)"));
        if (path.isEmpty())
            return;
        if (auto* editor = addEditor())
            editor->openFile(path);
    });
    const auto savedDirectory = connectSavedSql(ui, storagePath);
    auto promptSaveSql = [this, savedDirectory](QPointer<SqlEditor> editor, const QString& title,
                                                const QString& suggested, bool createDirectory) {
        auto showDialog = [this, editor, title, suggested] {
            if (!editor)
                return;
            const auto path =
                QFileDialog::getSaveFileName(this, title, suggested, tr("SQL files (*.sql)"));
            if (!path.isEmpty() && editor)
                editor->saveFile(path);
        };
        if (!createDirectory) {
            showDialog();
            return;
        }
        auto* watcher = new QFutureWatcher<QString>(this);
        connect(watcher, &QFutureWatcher<QString>::finished, this,
                [this, watcher, savedDirectory, showDialog] {
                    const auto error = watcher->result();
                    watcher->deleteLater();
                    if (!error.isEmpty()) {
                        reportError(tr("Could not create saved SQL directory %1: %2")
                                        .arg(savedDirectory, error));
                        return;
                    }
                    showDialog();
                });
        watcher->setFuture(QtConcurrent::run(
            [savedDirectory] { return prepareSavedSqlDirectory(savedDirectory); }));
    };
    connect(save, &QAction::triggered, this, [this, savedDirectory, promptSaveSql] {
        auto* editor = qobject_cast<SqlEditor*>(editors_->currentWidget());
        if (!editor)
            return;
        if (!editor->filePath().isEmpty())
            editor->saveFile(editor->filePath());
        else
            promptSaveSql(editor, tr("Save SQL file"), savedDirectory + "/", true);
    });
    connect(saveAs, &QAction::triggered, this, [this, savedDirectory, promptSaveSql] {
        auto* editor = qobject_cast<SqlEditor*>(editors_->currentWidget());
        if (!editor)
            return;
        const auto suggested =
            editor->filePath().isEmpty() ? savedDirectory + "/" : editor->filePath();
        promptSaveSql(editor, tr("Save SQL file as"), suggested, editor->filePath().isEmpty());
    });
    connect(run, &QAction::triggered, this, [this] { showScreen(Screen::Sql); });
    workspace_ =
        new QueryWorkspace({run,
                            runAll,
                            cancel,
                            newConnection,
                            nextPage,
                            empty,
                            messages,
                            grid,
                            [this] { return qobject_cast<SqlEditor*>(editors_->currentWidget()); },
                            this,
                            previousPage,
                            exportResult,
                            storagePath,
                            nullptr,
                            false,
                            addResultRow,
                            deleteResultRows,
                            nullResultCell,
                            applyResultEdits,
                            discardResultEdits,
                            {},
                            restoreResultRows,
                            compactState,
                            durationMetric,
                            pageMetric,
                            rowsMetric,
                            visibleSizeMetric,
                            resultEditReason,
                            ui.rowLimit,
                            ui.safetyLevel,
                            ui.connectionTag,
                            ui.statementResults,
                            ui.stopOnFirstError,
                            ui.explain},
                           this);
    preferences_->addGrid(workspace_);
    connect(workspace_, &QueryWorkspace::foreignKeyRequested, this, &MainWindow::openReferencedRow);
    const auto widths = resultColumnWidths_;
    struct GridFit {
        bool fitting = false;
        bool fitted = false;
    };
    const auto fit = std::make_shared<GridFit>();
    const auto columnName = [grid](int column) {
        return grid->model()
            ->headerData(column, Qt::Horizontal, ResultTableModel::HeaderNameRole)
            .toString();
    };
    connect(grid->horizontalHeader(), &QHeaderView::sectionResized, grid,
            [this, widths, fit, columnName](int column, int, int size) {
                if (fit->fitting)
                    return;
                const auto name = columnName(column);
                widths->user.removeIf([&name](const auto& entry) { return entry.first == name; });
                widths->user.append({name, static_cast<quint32>(size)});
                if (appearance_)
                    appearance_->scheduleSave();
            });
    connect(grid->model(), &QAbstractItemModel::modelReset, grid, [grid, widths, fit, columnName] {
        if (!grid->model()->columnCount()) {
            fit->fitted = false;
            return;
        }
        if (fit->fitted)
            return;
        fit->fitting = true;
        design::fitColumnsToContents(*grid);
        QHash<QString, quint32> remembered;
        for (const auto& [name, width] : std::as_const(widths->user))
            remembered.insert(name, width);
        for (int column = 0; column < grid->model()->columnCount(); ++column)
            if (const auto width = remembered.constFind(columnName(column));
                width != remembered.cend())
                grid->setColumnWidth(column, static_cast<int>(*width));
        fit->fitting = false;
        fit->fitted = true;
    });
    connect(workspace_, &QueryWorkspace::executionStateChanged, results,
            [results](QueryState state) {
                const auto pane = EngineAdapter::queryPresentation(state).pane;
                if (pane != ResultPane::Unchanged)
                    results->setCurrentIndex(pane == ResultPane::Messages ? 1 : 0);
            });
    connect(workspace_, &QueryWorkspace::executionStateChanged, this,
            [this, cancelButton](QueryState state) {
                const bool active = EngineAdapter::queryPresentation(state).busy;
                const bool restoreFocus = !active && cancelButton->hasFocus();
                if (restoreFocus && editors_->currentWidget())
                    editors_->currentWidget()->setFocus();
            });
    auto* objectExplorer = makeObjectExplorer();
    objectExplorer->hide();
    initialObjectExplorer_ = objectExplorer;
    connect(this, &MainWindow::objectContextSelected, this,
            [this, tree](quint64 connection, const QString& object, const QString& label,
                         const QString& kind) {
                openObjectTab(connection, object, label, kind,
                              tree->currentIndex().data(NavigatorModel::PropertiesRole).toList(),
                              EngineAdapter::objectKindTraits(kind).initialPane);
            });
    connect(workspace_, &QueryWorkspace::openQueryRequested, this,
            &MainWindow::openConnectionQuery);
    connect(preferences_, &EditorPreferencesController::queryPreferencesSaveSubmitted, workspace_,
            &QueryWorkspace::trackQueryPreferencesSave);
    connect(preferences_, &EditorPreferencesController::queryPreferencesConfirmed, workspace_,
            &QueryWorkspace::applyQueryPreferences);
    const auto refreshProfiles = [this] {
        profileListToken_ = nextRequestToken();
        workspace_->adapter()->listProfiles(profileListToken_);
    };
    const auto syncVisible = [this, savedConnections] {
        QList<quint64> ordered;
        std::optional<quint64> firstUsable;
        const QSignalBlocker selectionBlocked(savedConnections->selectionModel());
        for (int row = 0; row < savedConnections->count(); ++row) {
            auto* item = savedConnections->item(row);
            const auto profile = item->data(Qt::UserRole).value<SavedProfile>();
            const bool selected = selectedProfileIds_.contains(profile.id);
            item->setSelected(selected);
            item->setData(Qt::AccessibleDescriptionRole,
                          selected ? tr("Shown in the object navigator")
                                   : tr("Hidden from the object navigator"));
            if (!selected)
                continue;
            if (pendingBrowseProfiles_.contains(profile.id)) {
                ordered.append(pendingBrowseProfiles_.value(profile.id).placeholder);
                continue;
            }
            std::optional<quint64> session;
            const auto preferred = selectedSessionIds_.value(profile.id, 0);
            for (const auto& live : workspace_->liveConnections()) {
                const auto id = live.id;
                if (id == preferred && workspace_->profileIdForConnection(id) == profile.id) {
                    session = id;
                    sessionProfileIds_.insert(id, profile.id);
                    break;
                }
            }
            if (!session)
                for (const auto& live : workspace_->liveConnections()) {
                    const auto id = live.id;
                    if (workspace_->profileIdForConnection(id) == profile.id &&
                        !retiredBrowseConnections_.contains(id)) {
                        session = id;
                        sessionProfileIds_.insert(id, profile.id);
                        break;
                    }
                }
            if (session) {
                ordered.append(*session);
                if (!firstUsable)
                    firstUsable = *session;
            }
        }
        if (!browsingConnection_ || !ordered.contains(*browsingConnection_))
            browsingConnection_ = firstUsable;
        if (navigatorController_)
            navigatorController_->setVisibleConnections(ordered);
    };
    connect(
        workspace_->adapter(), &EngineAdapter::profilesReady, this,
        [this, savedConnections, syncVisible](quint64 token, const QList<SavedProfile>& profiles) {
            if (token != profileListToken_)
                return;
            const auto current = savedConnections->currentItem()
                                     ? savedConnections->currentItem()->data(Qt::UserRole)
                                     : QVariant{};
            const auto focused = current.value<SavedProfile>().id;
            QSet<QString> surviving;
            for (const auto& profile : profiles)
                surviving.insert(profile.id);
            for (const auto& id : selectedProfileIds_)
                if (!surviving.contains(id) && pendingBrowseProfiles_.contains(id)) {
                    const auto pending = pendingBrowseProfiles_.take(id);
                    if (pending.connection)
                        retiredBrowseConnections_.insert(*pending.connection);
                    if (navigatorController_)
                        navigatorController_->removePendingConnection(pending.placeholder);
                }
            for (auto it = selectedSessionIds_.begin(); it != selectedSessionIds_.end();)
                if (!surviving.contains(it.key()))
                    it = selectedSessionIds_.erase(it);
                else
                    ++it;
            selectedProfileIds_.intersect(surviving);
            const QSignalBlocker blocked(savedConnections);
            const QSignalBlocker selectionBlocked(savedConnections->selectionModel());
            savedConnections->clear();
            for (const auto& profile : profiles) {
                auto* item = new QListWidgetItem(profile.name, savedConnections);
                item->setData(Qt::UserRole, QVariant::fromValue(profile));
                item->setData(design::NavigationProfileDelegate::DriverRole, profile.driver);
                if (const auto tone = connectionTone(profile.tone))
                    item->setData(design::NavigationProfileDelegate::ToneRole, int(*tone));
                item->setIcon(design::themedIcon(design::driverIcon(profile.driver),
                                                 theme_->resolvedTheme().colors.textSubtle,
                                                 design::dimension(design::Dimension::Icon)));
                item->setToolTip(profile.tagLabel.isEmpty()
                                     ? profile.name
                                     : tr("%1 · %2").arg(profile.name, profile.tagLabel));
                if (profile.id == focused)
                    savedConnections->setCurrentItem(item, QItemSelectionModel::NoUpdate);
                for (const auto& live : workspace_->liveConnections()) {
                    const auto id = live.id;
                    if (workspace_->profileIdForConnection(id) == profile.id &&
                        navigatorController_)
                        navigatorController_->renameConnection(id, profile.name);
                }
                if (pendingBrowseProfiles_.contains(profile.id)) {
                    auto& pending = pendingBrowseProfiles_[profile.id];
                    if (pending.name != profile.name && navigatorController_) {
                        navigatorController_->removePendingConnection(pending.placeholder);
                        navigatorController_->setPendingConnection(pending.placeholder,
                                                                   profile.name);
                    }
                    pending.name = profile.name;
                }
            }
            syncVisible();
        });
    connect(workspace_->adapter(), &EngineAdapter::profileSaved, this,
            [this, refreshProfiles](quint64, const SavedProfile& profile, const QString&) {
                workspace_->refreshProfileTag(profile);
                if (scheduleTabTags_)
                    scheduleTabTags_();
                refreshProfiles();
            });
    connect(workspace_->adapter(), &EngineAdapter::profileDeleted, this,
            [this, refreshProfiles, syncVisible](quint64, const QString& id, const QString&) {
                selectedProfileIds_.remove(id);
                selectedSessionIds_.remove(id);
                if (pendingBrowseProfiles_.contains(id)) {
                    const auto pending = pendingBrowseProfiles_.take(id);
                    if (pending.connection)
                        retiredBrowseConnections_.insert(*pending.connection);
                    if (navigatorController_)
                        navigatorController_->removePendingConnection(pending.placeholder);
                }
                syncVisible();
                refreshProfiles();
            });
    const auto selectProfile = [this, syncVisible](QListWidgetItem* item) {
        if (!item)
            return;
        const auto profile = item->data(Qt::UserRole).value<SavedProfile>();
        if (selectedProfileIds_.remove(profile.id)) {
            selectedSessionIds_.remove(profile.id);
            if (pendingBrowseProfiles_.contains(profile.id)) {
                const auto pending = pendingBrowseProfiles_.take(profile.id);
                if (pending.connection)
                    retiredBrowseConnections_.insert(*pending.connection);
                if (navigatorController_)
                    navigatorController_->removePendingConnection(pending.placeholder);
            }
            syncVisible();
            return;
        }
        selectedProfileIds_.insert(profile.id);
        for (const auto& live : workspace_->liveConnections()) {
            const auto id = live.id;
            if (workspace_->profileIdForConnection(id) == profile.id) {
                retiredBrowseConnections_.remove(id);
                selectedSessionIds_.insert(profile.id, id);
                browsingConnection_ = id;
                syncVisible();
                emit browsingConnectionChanged(id);
                return;
            }
        }
        const quint64 placeholder = --nextPendingPlaceholder_;
        pendingBrowseProfiles_.insert(profile.id,
                                      PendingBrowse{placeholder, std::nullopt, profile.name});
        if (navigatorController_)
            navigatorController_->setPendingConnection(placeholder, profile.name);
        syncVisible();
        submittingBrowseProfileId_ = profile.id;
        submissionError_.clear();
        submittingBrowseProfile_ = true;
        const auto submitted = workspace_->connectSavedProfile(profile);
        submittingBrowseProfile_ = false;
        submittingBrowseProfileId_.clear();
        if (submitted) {
            if (pendingBrowseProfiles_.contains(profile.id))
                pendingBrowseProfiles_[profile.id].connection = submitted;
            return;
        }
        pendingBrowseProfiles_.remove(profile.id);
        if (navigatorController_)
            navigatorController_->removePendingConnection(placeholder);
        selectedProfileIds_.remove(profile.id);
        syncVisible();
        if (submissionError_.isEmpty())
            reportNotice(tr("Could not open %1: %2")
                             .arg(profile.name,
                                  tr("Finish or cancel active database work before connecting.")));
        else
            reportError(tr("Could not open %1: %2").arg(profile.name, submissionError_));
    };
    reconnectProfile_ = [this, savedConnections, selectProfile](const QString& id) {
        for (int i = 0; i < savedConnections->count(); ++i) {
            auto* item = savedConnections->item(i);
            const auto data = item->data(Qt::UserRole);
            if (data.value<SavedProfile>().id != id)
                continue;
            savedConnections->setCurrentItem(item, QItemSelectionModel::NoUpdate);
            if (!selectedProfileIds_.contains(id))
                selectProfile(item);
            return selectedProfileIds_.contains(id) || pendingBrowseProfiles_.contains(id);
        }
        reportError(tr("The saved connection is unavailable. %1")
                        .arg(EngineAdapter::recoveryHint(RecoveryHint::RestoreConnection)));
        return false;
    };
    savedConnectionsList_ = savedConnections;
    activateFocusedSavedProfile_ = [savedConnections, selectProfile] {
        selectProfile(savedConnections->currentItem());
    };
    savedConnections->installEventFilter(this);
    connect(savedConnections->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [syncVisible] { syncVisible(); });
    connect(savedConnections, &QListWidget::itemClicked, this, selectProfile);
    connect(savedConnections, &QListWidget::itemActivated, this, selectProfile);
    savedConnections->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(savedConnections, &QWidget::customContextMenuRequested, this,
            [this, savedConnections, selectProfile, syncVisible](const QPoint& position) {
                auto* item = savedConnections->itemAt(position);
                if (!item || !allowDocumentChange())
                    return;
                savedConnections->setCurrentItem(item, QItemSelectionModel::NoUpdate);
                syncVisible();
                const auto profileData = item->data(Qt::UserRole);
                const auto profile = profileData.value<SavedProfile>();
                std::optional<quint64> session;
                for (const auto& live : workspace_->liveConnections())
                    if (workspace_->profileIdForConnection(live.id) == profile.id)
                        session = live.id;
                auto* menu = new QMenu(savedConnections);
                menu->setObjectName("savedConnectionMenu");
                menu->setAttribute(Qt::WA_DeleteOnClose);
                auto* connectAction = menu->addAction(tr("Connect"));
                connectAction->setObjectName("connectSavedConnection");
                connectAction->setEnabled(!selectedProfileIds_.contains(profile.id) &&
                                          !pendingBrowseProfiles_.contains(profile.id));
                connect(connectAction, &QAction::triggered, this,
                        [selectProfile, item] { selectProfile(item); });
                auto* disconnectAction = menu->addAction(tr("Disconnect"));
                disconnectAction->setObjectName("disconnectSavedConnection");
                disconnectAction->setEnabled(session.has_value());
                connect(disconnectAction, &QAction::triggered, this, [this, session] {
                    if (session)
                        workspace_->disconnectConnection(*session);
                });
                menu->addSeparator();
                const QList<QPair<QString, QString>> management = {{"edit", tr("Edit…")},
                                                                   {"test", tr("Test connection")},
                                                                   {"duplicate", tr("Duplicate")},
                                                                   {"delete", tr("Delete…")}};
                for (const auto& entry : management) {
                    if (entry.first == QStringLiteral("delete"))
                        menu->addSeparator();
                    auto* action = menu->addAction(entry.second);
                    action->setObjectName(entry.first + "SavedConnection");
                    connect(action, &QAction::triggered, this,
                            [this, profile, operation = entry.first] {
                                workspace_->manageSavedProfile(profile.id, operation);
                            });
                }
                design::popupContextMenu(*menu,
                                         savedConnections->viewport()->mapToGlobal(position));
            });
    connect(workspace_, &QueryWorkspace::connectionNotice, this,
            [this](const QString& notice) { reportNotice(notice); });
    connect(workspace_, &QueryWorkspace::connectionReady, this, [this, syncVisible](quint64 id) {
        const auto profileId = workspace_->profileIdForConnection(id);
        if (profileId.isEmpty())
            return;
        if (reconnectingProfile_ == profileId)
            finishReconnect(true);
        sessionProfileIds_.insert(id, profileId);
        if (retiredBrowseConnections_.contains(id))
            return;
        bool selectedAttempt = false;
        if (pendingBrowseProfiles_.contains(profileId) &&
            (!pendingBrowseProfiles_.value(profileId).connection ||
             pendingBrowseProfiles_.value(profileId).connection == id)) {
            const auto pending = pendingBrowseProfiles_.take(profileId);
            if (navigatorController_)
                navigatorController_->removePendingConnection(pending.placeholder);
            selectedAttempt = true;
        } else if (pendingBrowseProfiles_.contains(profileId)) {
            return;
        }
        if (!selectedProfileIds_.contains(profileId))
            return;
        if (!selectedAttempt && selectedSessionIds_.contains(profileId))
            return;
        selectedSessionIds_.insert(profileId, id);
        browsingConnection_ = id;
        syncVisible();
        emit browsingConnectionChanged(id);
    });
    connect(
        workspace_->adapter(), &EngineAdapter::eventReady, this,
        [this, syncVisible](const BridgeEvent& event) {
            const auto kind = fromRust(event.kind);
            if (kind == "connection_failed") {
                retiredBrowseConnections_.remove(event.id);
                for (auto it = pendingBrowseProfiles_.begin(); it != pendingBrowseProfiles_.end();
                     ++it) {
                    if (it->connection != event.id)
                        continue;
                    const auto profileId = it.key();
                    const auto pending = it.value();
                    pendingBrowseProfiles_.erase(it);
                    if (navigatorController_)
                        navigatorController_->removePendingConnection(pending.placeholder);
                    const bool selected = selectedProfileIds_.remove(profileId);
                    if (selected)
                        syncVisible();
                    if (reconnectingProfile_ == profileId) {
                        finishReconnect(false);
                        reportError(tr("Could not reconnect %1: %2")
                                        .arg(pending.name, fromRust(event.error)));
                    } else if (selected) {
                        reportError(
                            tr("Could not open %1: %2").arg(pending.name, fromRust(event.error)));
                    }
                    break;
                }
            } else if (kind == "disconnected") {
                for (auto it = pendingBrowseProfiles_.cbegin(); it != pendingBrowseProfiles_.cend();
                     ++it) {
                    if (it->connection == event.id && reconnectingProfile_ == it.key()) {
                        finishReconnect(false);
                        reportError(tr("The connection closed before reconnecting completed."));
                        break;
                    }
                }
                const auto profileId = sessionProfileIds_.take(event.id);
                if (!profileId.isEmpty() && reconnectingProfile_ == profileId) {
                    finishReconnect(false);
                    reportError(tr("The connection closed before reconnecting completed."));
                }
                retiredBrowseConnections_.remove(event.id);
                if (!profileId.isEmpty() && selectedSessionIds_.value(profileId) == event.id) {
                    selectedSessionIds_.remove(profileId);
                    selectedProfileIds_.remove(profileId);
                    syncVisible();
                }
            }
        },
        Qt::DirectConnection);
    connect(workspace_->adapter(), &EngineAdapter::profileFailed, this,
            [this](quint64 token, const QString& error) {
                if (token == profileListToken_)
                    reportNotice(tr("Saved connections: %1").arg(error), ToastVariant::Danger);
            });
    connect(workspace_->adapter(), &EngineAdapter::profileConnectFailed, this,
            [this](const QString& error) {
                if (!submittingBrowseProfile_ || submittingBrowseProfileId_.isEmpty()) {
                    reportError(error);
                    return;
                }
                submissionError_ = error;
            });
    auto* refreshSaved = viewMenu->addAction(tr("Refresh saved connections"));
    refreshSaved->setObjectName("refreshSavedConnections");
    const QList<QStringList> viewGroups{
        {"showStart", "showSql", "showObjects", "showHistory"},
        {"quickSwitch", "listEditorTabs", "focusConnections"},
        {"toggleSidebar", "resetLayout"},
        {"refreshSavedConnections"},
        {"openDesignSystemPreview"},
    };
    const auto viewActions = viewMenu->actions();
    for (auto* action : viewActions)
        viewMenu->removeAction(action);
    for (const auto& group : viewGroups) {
        bool separated = viewMenu->actions().isEmpty();
        for (const auto& name : group)
            for (auto* action : viewActions)
                if (action->objectName() == name) {
                    if (!std::exchange(separated, true))
                        viewMenu->addSeparator();
                    viewMenu->addAction(action);
                }
    }
    connect(refreshSaved, &QAction::triggered, this, refreshProfiles);
    refreshProfiles();
    connect(querySettings, &QAction::triggered, this,
            [this] { preferences_->open(PreferencesPage::Results); });
    preferences_->initialize(workspace_->adapter());
}
}
