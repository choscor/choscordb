#include "app/main_window.h"

#include "app/application_data.h"
#include "app/editor_preferences.h"
#include "app/main_window_ui.h"
#include "app/navigator_controller.h"
#include "app/object_data_workspace.h"
#include "app/object_explorer.h"
#include "app/query_workspace.h"
#include "app/workspace_recovery.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/button/button.h"
#include "design_system/confirmation_dialog/confirmation_dialog.h"
#include "design_system/icons.h"
#include "design_system/menu/menu.h"
#include "design_system/navigation_profile_row/navigation_profile_row.h"
#include "design_system/text/text.h"
#include "design_system/theme_manager.h"
#include "design_system/toast_region/toast_region.h"
#include "models/navigator_model.h"
#include "widgets/editor_completion/editor_completion.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAbstractItemModel>
#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QPushButton>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableView>
#include <QTreeView>
#include <QTreeWidget>
#include <atomic>

namespace choscordb {

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == savedConnectionsList_.data() && event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if ((key->key() == Qt::Key_Space || key->key() == Qt::Key_Select ||
             key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
            key->modifiers() == Qt::NoModifier && activateFocusedSavedProfile_) {
            activateFocusedSavedProfile_();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::connectWorkspace(const Ui& ui, const QString& storagePath) {
    const auto newQuery = ui.newQuery;
    const auto open = ui.open;
    const auto save = ui.save;
    const auto saveAs = ui.saveAs;
    const auto newConnection = ui.newConnection;
    const auto viewMenu = ui.viewMenu;
    const auto addConnection = ui.addConnection;
    const auto sidebarPanels = ui.sidebarPanels;
    const auto savedConnections = ui.savedConnections;
    const auto tree = ui.tree;
    const auto navigatorStatus = ui.navigatorStatus;
    const auto savedSearch = ui.savedSearch;
    const auto savedStatus = ui.savedStatus;
    const auto savedFiles = ui.savedFiles;
    const auto connections = ui.connections;
    const auto run = ui.run;
    const auto cancel = ui.cancel;
    const auto cancelButton = ui.cancelButton;
    const auto mode = ui.mode;
    const auto commitAction = ui.commitAction;
    const auto rollbackAction = ui.rollbackAction;
    const auto querySettings = ui.querySettings;
    const auto results = ui.results;
    const auto empty = ui.empty;
    const auto grid = ui.grid;
    const auto compactState = ui.compactState;
    const auto previousPage = ui.previousPage;
    const auto nextPage = ui.nextPage;
    const auto exportResult = ui.exportResult;
    const auto addResultRow = ui.addResultRow;
    const auto deleteResultRows = ui.deleteResultRows;
    const auto restoreResultRows = ui.restoreResultRows;
    const auto nullResultCell = ui.nullResultCell;
    const auto discardResultEdits = ui.discardResultEdits;
    const auto applyResultEdits = ui.applyResultEdits;
    const auto messages = ui.messages;
    connect(completion_, &EditorCompletionController::partialCatalog, this,
            [this, partialShown = false](bool partial) mutable {
                if (partial && !partialShown)
                    showToast(tr("Suggestions use loaded navigator objects. Expand nodes for more "
                                 "names; large catalogs may be limited."),
                              ToastVariant::Warning);
                partialShown = partial;
            });
    connect(newQuery, &QAction::triggered, this, [this] { addEditor(); });
    connect(addConnection, &QPushButton::clicked, newConnection, &QAction::trigger);
    connect(editors_, &QTabWidget::tabCloseRequested, this, [this](int index) {
        if (!allowDocumentChange())
            return;
        auto* closing = editors_->widget(index);
        if (auto* object = qobject_cast<ObjectExplorer*>(closing)) {
            if (auto* objectData = object->findChild<ObjectDataWorkspace*>();
                objectData && !objectData->resolvePendingEdits())
                return;
        }
        auto* editor = qobject_cast<SqlEditor*>(closing);
        if (editor && editor->isModified() &&
            ConfirmationDialog::question(this, tr("Close query"), tr("Discard unsaved changes?"),
                                         QMessageBox::Discard | QMessageBox::Cancel,
                                         QMessageBox::Cancel) != QMessageBox::Discard)
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
    const auto savedDirectory = QDir(applicationDataDirectory()).filePath("sql");
    auto filterSavedFiles = [savedFiles, savedSearch] {
        const auto query = savedSearch->text().trimmed();
        const auto filterItem = [&](const auto& self, QTreeWidgetItem* item,
                                    const QString& path) -> bool {
            const auto relativePath = path + item->text(0);
            bool matches = relativePath.contains(query, Qt::CaseInsensitive);
            for (int i = 0; i < item->childCount(); ++i)
                matches = self(self, item->child(i), relativePath + '/') || matches;
            item->setHidden(!matches);
            if (!query.isEmpty() && matches && item->childCount())
                item->setExpanded(true);
            return matches;
        };
        for (int i = 0; i < savedFiles->topLevelItemCount(); ++i)
            filterItem(filterItem, savedFiles->topLevelItem(i), {});
    };
    connect(savedSearch, &QLineEdit::textChanged, this, filterSavedFiles);
    auto refreshSavedFiles = [savedFiles, savedStatus, savedDirectory, filterSavedFiles] {
        savedFiles->clear();
        savedStatus->show();
        const QDir directory(savedDirectory);
        if (!directory.exists()) {
            savedStatus->setText(QObject::tr("No saved queries yet.\nSave a query as a SQL file "
                                             "in the default folder to find it here."));
            return;
        }
        if (!directory.isReadable()) {
            savedStatus->setText(
                QObject::tr("Saved SQL directory cannot be read: %1").arg(savedDirectory));
            return;
        }
        QDirIterator files(savedDirectory, {"*.sql"}, QDir::Files | QDir::NoSymLinks,
                           QDirIterator::Subdirectories);
        constexpr int limit = 1000;
        int count = 0;
        QHash<QString, QTreeWidgetItem*> folders;
        while (files.hasNext() && count < limit) {
            const auto path = files.next();
            const auto parts = directory.relativeFilePath(path).split('/');
            QTreeWidgetItem* parent = savedFiles->invisibleRootItem();
            QString folderPath;
            for (int i = 0; i + 1 < parts.size(); ++i) {
                folderPath += parts.at(i) + '/';
                auto* folder = folders.value(folderPath);
                if (!folder) {
                    folder = new QTreeWidgetItem(parent, {parts.at(i)});
                    folder->setData(0, NavigatorModel::KindRole, "schema");
                    folders.insert(folderPath, folder);
                }
                parent = folder;
            }
            auto* item = new QTreeWidgetItem(parent, {parts.last()});
            item->setData(0, Qt::UserRole, path);
            item->setToolTip(0, directory.relativeFilePath(path));
            item->setData(0, NavigatorModel::KindRole, "file");
            ++count;
        }
        savedFiles->sortItems(0, Qt::AscendingOrder);
        savedStatus->setText(files.hasNext() ? QObject::tr("Showing the first %1 files.").arg(limit)
                             : count ? QString{}
                                     : QObject::tr("No saved queries yet.\nSave a query as a SQL "
                                                   "file in the default folder to find it here."));
        savedStatus->setVisible(!savedStatus->text().isEmpty());
        filterSavedFiles();
    };
    refreshSavedFiles_ = refreshSavedFiles;
    connect(sidebarPanels, &QStackedWidget::currentChanged, this, [refreshSavedFiles](int index) {
        if (index == 1)
            refreshSavedFiles();
    });
    auto openSavedItem = [this](QTreeWidgetItem* item) {
        const auto path = item->data(0, Qt::UserRole).toString();
        if (path.isEmpty())
            return;
        if (!allowDocumentChange() || databaseClosePending_ ||
            (recovery_ && (!recovery_->isReady() || recovery_->isClosing()))) {
            showToast(tr("Saved file cannot be opened while the workspace is busy."),
                      ToastVariant::Warning);
            return;
        }
        const auto identity = [](const QString& value) {
            const QFileInfo info(value);
            const auto canonical = info.canonicalFilePath();
            return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
        };
        for (int i = 0; i < editors_->count(); ++i) {
            auto* existing = qobject_cast<SqlEditor*>(editors_->widget(i));
            if (existing && !existing->filePath().isEmpty() &&
                identity(existing->filePath()) == identity(path)) {
                editors_->setCurrentIndex(i);
                showScreen(Screen::Sql);
                return;
            }
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            showToast(
                tr("Could not open %1: %2").arg(QFileInfo(path).fileName(), file.errorString()),
                ToastVariant::Danger);
            return;
        }
        file.close();
        if (auto* editor = addEditor()) {
            connect(editor, &SqlEditor::fileOpened, this,
                    [this, editor](const QString&, const QString& error) {
                        if (error.isEmpty() || editors_->indexOf(editor) < 0)
                            return;
                        editors_->removeTab(editors_->indexOf(editor));
                        editor->deleteLater();
                        if (!editors_->count())
                            showScreen(Screen::Start);
                    });
            editor->openFile(path);
        }
    };
    connect(savedFiles, &QTreeWidget::itemClicked, this, openSavedItem);
    connect(savedFiles, &QTreeWidget::itemActivated, this, openSavedItem);
    connect(save, &QAction::triggered, this, [this] {
        auto* editor = qobject_cast<SqlEditor*>(editors_->currentWidget());
        if (!editor)
            return;
        auto path = editor->filePath();
        if (path.isEmpty()) {
            const auto directory = QDir(applicationDataDirectory()).filePath("sql");
            if (!QDir().mkpath(directory)) {
                showToast(tr("Could not create saved SQL directory: %1").arg(directory),
                          ToastVariant::Danger);
                return;
            }
            path = QFileDialog::getSaveFileName(this, tr("Save SQL file"), directory + "/",
                                                tr("SQL files (*.sql)"));
        }
        if (path.isEmpty())
            return;
        editor->saveFile(path);
    });
    connect(saveAs, &QAction::triggered, this, [this, savedDirectory] {
        auto* editor = qobject_cast<SqlEditor*>(editors_->currentWidget());
        if (!editor)
            return;
        const auto suggested =
            editor->filePath().isEmpty() ? savedDirectory + "/" : editor->filePath();
        if (editor->filePath().isEmpty() && !QDir().mkpath(savedDirectory)) {
            showToast(tr("Could not create saved SQL directory: %1").arg(savedDirectory),
                      ToastVariant::Danger);
            return;
        }
        const auto path = QFileDialog::getSaveFileName(this, tr("Save SQL file as"), suggested,
                                                       tr("SQL files (*.sql)"));
        if (!path.isEmpty())
            editor->saveFile(path);
    });
    connect(run, &QAction::triggered, this, [this] { showScreen(Screen::Sql); });
    workspace_ =
        new QueryWorkspace({connections,
                            mode,
                            run,
                            cancel,
                            commitAction,
                            rollbackAction,
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
                            restoreResultRows},
                           this);
    connect(workspace_, &QueryWorkspace::foreignKeyRequested, this, &MainWindow::openReferencedRow);
    connect(grid->model(), &QAbstractItemModel::modelReset, grid, [grid, fitted = false]() mutable {
        if (!grid->model()->columnCount()) {
            fitted = false;
            return;
        }
        if (fitted)
            return;
        // Sample only the first bounded page once. Subsequent paging
        // retains column widths the user adjusted for this result.
        grid->resizeColumnsToContents();
        for (int column = 0; column < grid->model()->columnCount(); ++column)
            grid->setColumnWidth(column, qMax(grid->horizontalHeader()->sectionSizeHint(column),
                                              qBound(80, grid->columnWidth(column), 400)));
        fitted = true;
    });
    connect(workspace_, &QueryWorkspace::executionStateChanged, results,
            [results](const QString& state) {
                if (state == "failed")
                    results->setCurrentIndex(1);
                else if (state == "completed" || state == "queued" || state == "running")
                    results->setCurrentIndex(0);
            });
    connect(workspace_, &QueryWorkspace::executionStateChanged, this, [this](const QString& state) {
        if (!centralWidget())
            return;
        if (state == "queued" || state == "running" || state == "cancelling") {
            const auto detail = state == "queued"       ? tr("Waiting to run…")
                                : state == "cancelling" ? tr("Cancelling query…")
                                                        : tr("Running query…");
            progressToast(centralWidget())->showProgress(tr("Query in progress"), detail);
        } else {
            clearProgressToast(centralWidget());
        }
    });
    connect(workspace_, &QueryWorkspace::executionStateChanged, this,
            [this, empty, compactState, cancelButton](const QString& state) {
                const bool active =
                    state == "queued" || state == "running" || state == "cancelling";
                const bool restoreFocus = !active && cancelButton->hasFocus();
                if (restoreFocus && editors_->currentWidget())
                    editors_->currentWidget()->setFocus();
                empty->setToolTip(empty->text());
                compactState->setText(state);
                compactState->setAccessibleName(tr("Execution status: %1").arg(state));
            });
    auto* objectExplorer = makeObjectExplorer();
    objectExplorer->hide(); // Not part of the workspace until its first object tab opens.
    initialObjectExplorer_ = objectExplorer;
    connect(this, &MainWindow::objectContextSelected, this,
            [this, tree](quint64 connection, const QString& object, const QString& label,
                         const QString& kind) {
                openObjectTab(connection, object, label, kind,
                              tree->currentIndex().data(NavigatorModel::PropertiesRole).toList(),
                              kind == QStringLiteral("table") ? 5 : -1);
            });
    connect(workspace_, &QueryWorkspace::openQueryRequested, this,
            &MainWindow::openConnectionQuery);
    connect(preferences_, &EditorPreferencesController::queryPreferencesSaveSubmitted, workspace_,
            &QueryWorkspace::trackQueryPreferencesSave);
    connect(preferences_, &EditorPreferencesController::queryPreferencesConfirmed, workspace_,
            &QueryWorkspace::applyQueryPreferences);
    const auto refreshProfiles = [this] {
        static std::atomic<quint64> next{quint64(1) << 54};
        profileListToken_ = next.fetch_add(1);
        workspace_->adapter()->listProfiles(profileListToken_);
    };
    const auto syncVisible = [this, savedConnections, connections, navigatorStatus] {
        QList<quint64> ordered;
        std::optional<quint64> firstUsable;
        const QSignalBlocker selectionBlocked(savedConnections->selectionModel());
        for (int row = 0; row < savedConnections->count(); ++row) {
            auto* item = savedConnections->item(row);
            const auto profile = item->data(Qt::UserRole).value<SavedProfile>();
            const bool selected = selectedProfileIds_.contains(profile.id);
            item->setSelected(selected);
            item->setData(Qt::AccessibleDescriptionRole,
                          selected ? tr("Visible in Schema & Objects")
                                   : tr("Hidden from Schema & Objects"));
            if (!selected)
                continue;
            if (pendingBrowseProfiles_.contains(profile.id)) {
                ordered.append(pendingBrowseProfiles_.value(profile.id).placeholder);
                continue;
            }
            std::optional<quint64> session;
            const auto preferred = selectedSessionIds_.value(profile.id, 0);
            for (int i = 0; i < connections->count(); ++i) {
                if (!connections->itemData(i).isValid())
                    continue;
                const auto id = connections->itemData(i).toULongLong();
                if (id == preferred && workspace_->profileIdForConnection(id) == profile.id) {
                    session = id;
                    sessionProfileIds_.insert(id, profile.id);
                    break;
                }
            }
            if (!session)
                for (int i = 0; i < connections->count(); ++i) {
                    if (!connections->itemData(i).isValid())
                        continue;
                    const auto id = connections->itemData(i).toULongLong();
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
        navigatorStatus->setText(!navigatorSearchStatus_.isEmpty()
                                     ? navigatorSearchStatus_
                                     : firstUsable ? tr("● Connected")
                                     : ordered.isEmpty() ? tr("○ Disconnected")
                                                         : tr("Loading connections…"));
        navigatorStatus->setProperty("state", !navigatorSearchStatus_.isEmpty()
                                                  ? "search"
                                                  : firstUsable ? "connected" : "disconnected");
        navigatorStatus->setAccessibleName(
            tr("Navigator connection status: %1").arg(navigatorStatus->text()));
        navigatorStatus->style()->unpolish(navigatorStatus);
        navigatorStatus->style()->polish(navigatorStatus);
    };
    const auto showBrowseFailure = [this](const QString& name, const QString& reason) {
        auto* dialog = new ConfirmationDialog(
            QMessageBox::Warning, tr("Connection failed"),
            tr("Could not open %1: %2").arg(name, reason), QMessageBox::Ok, this);
        dialog->setObjectName("sidebarConnectionFailure");
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->open();
    };
    connect(
        workspace_->adapter(), &EngineAdapter::profilesReady, this,
        [this, savedConnections, connections, syncVisible](quint64 token,
                                               const QList<SavedProfile>& profiles) {
            if (token != profileListToken_)
                return;
            const auto focused =
                savedConnections->currentItem()
                    ? savedConnections->currentItem()->data(Qt::UserRole).value<SavedProfile>().id
                    : QString{};
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
                item->setIcon(
                    design::themedIcon(profile.driver == "sqlite"     ? design::Icon::SQLite
                                       : profile.driver == "postgres" ? design::Icon::PostgreSQL
                                       : profile.driver == "mysql"    ? design::Icon::MySQL
                                                                      : design::Icon::Database,
                                       theme_->resolvedTheme().colors.mutedText, 16));
                item->setToolTip(profile.name);
                if (profile.id == focused)
                    savedConnections->setCurrentItem(item, QItemSelectionModel::NoUpdate);
                for (int i = 0; i < connections->count(); ++i) {
                    if (!connections->itemData(i).isValid())
                        continue;
                    const auto id = connections->itemData(i).toULongLong();
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
            const int rowHeight = savedConnections->sizeHintForRow(0);
            savedConnections->setMaximumHeight(
                profiles.isEmpty()
                    ? 0
                    : profiles.size() * (rowHeight + 2 * savedConnections->spacing()) +
                          2 * savedConnections->frameWidth());
            syncVisible();
        });
    connect(workspace_->adapter(), &EngineAdapter::profileSaved, this,
            [refreshProfiles] { refreshProfiles(); });
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
    const auto selectProfile = [this, connections, syncVisible,
                                showBrowseFailure](QListWidgetItem* item) {
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
        for (int i = 0; i < connections->count(); ++i) {
            if (!connections->itemData(i).isValid())
                continue;
            const auto id = connections->itemData(i).toULongLong();
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
        const auto reason = submissionError_.isEmpty()
                                ? tr("Finish or cancel active database work before connecting.")
                                : submissionError_;
        pendingBrowseProfiles_.remove(profile.id);
        if (navigatorController_)
            navigatorController_->removePendingConnection(placeholder);
        selectedProfileIds_.remove(profile.id);
        syncVisible();
        showBrowseFailure(profile.name, reason);
    };
    reconnectProfile_ = [this, savedConnections, selectProfile](const QString& id) {
        for (int i = 0; i < savedConnections->count(); ++i) {
            auto* item = savedConnections->item(i);
            if (item->data(Qt::UserRole).value<SavedProfile>().id != id)
                continue;
            savedConnections->setCurrentItem(item, QItemSelectionModel::NoUpdate);
            if (!selectedProfileIds_.contains(id))
                selectProfile(item);
            return selectedProfileIds_.contains(id) || pendingBrowseProfiles_.contains(id);
        }
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
            [this, savedConnections, connections, selectProfile,
             syncVisible](const QPoint& position) {
                auto* item = savedConnections->itemAt(position);
                if (!item || !allowDocumentChange())
                    return;
                savedConnections->setCurrentItem(item, QItemSelectionModel::NoUpdate);
                syncVisible();
                const auto profileData = item->data(Qt::UserRole);
                const auto profile = profileData.value<SavedProfile>();
                std::optional<quint64> session;
                for (int i = 0; i < connections->count(); ++i) {
                    if (connections->itemData(i).isValid()) {
                        const auto id = connections->itemData(i).toULongLong();
                        if (workspace_->profileIdForConnection(id) == profile.id)
                            session = id;
                    }
                }
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
    connect(workspace_, &QueryWorkspace::connectionReady, this, [this, syncVisible](quint64 id) {
        const auto profileId = workspace_->profileIdForConnection(id);
        if (profileId.isEmpty())
            return;
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
        [this, syncVisible, showBrowseFailure](const BridgeEvent& event) {
            const auto kind = QString::fromUtf8(event.kind.data(), qsizetype(event.kind.size()));
            if (kind == "connection_failed") {
                retiredBrowseConnections_.remove(event.id);
                for (auto it = pendingBrowseProfiles_.begin();
                     it != pendingBrowseProfiles_.end(); ++it) {
                    if (it->connection != event.id)
                        continue;
                    const auto profileId = it.key();
                    const auto pending = it.value();
                    pendingBrowseProfiles_.erase(it);
                    if (navigatorController_)
                        navigatorController_->removePendingConnection(pending.placeholder);
                    if (selectedProfileIds_.remove(profileId)) {
                        syncVisible();
                        showBrowseFailure(
                            pending.name,
                            QString::fromUtf8(event.error.data(), qsizetype(event.error.size())));
                    }
                    break;
                }
            } else if (kind == "disconnected") {
                const auto profileId = sessionProfileIds_.take(event.id);
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
                    showToast(tr("Saved connections: %1").arg(error), ToastVariant::Danger);
            });
    connect(workspace_->adapter(), &EngineAdapter::profileConnectFailed, this,
            [this](const QString& error) {
                if (!submittingBrowseProfile_ || submittingBrowseProfileId_.isEmpty()) {
                    showToast(error, ToastVariant::Danger);
                    return;
                }
                submissionError_ = error;
            });
    auto* refreshSaved = viewMenu->addAction(tr("Refresh saved connections"));
    refreshSaved->setObjectName("refreshSavedConnections");
#ifdef Q_OS_MACOS
    // Keep commands available to internal callers and keyboard shortcuts without
    // listing them in the native View menu. Qt hides the resulting empty menu.
    for (auto* action : viewMenu->actions()) {
        addAction(action);
        viewMenu->removeAction(action);
    }
#endif
    connect(refreshSaved, &QAction::triggered, this, refreshProfiles);
    refreshProfiles();
    connect(querySettings, &QAction::triggered, workspace_, &QueryWorkspace::showQuerySettings);
    preferences_->initialize(workspace_->adapter());
}
} // namespace choscordb
