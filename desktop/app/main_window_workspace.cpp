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
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableView>
#include <QTreeView>
#include <QTreeWidget>
#include <atomic>

namespace choscordb {

void MainWindow::connectWorkspace(const Ui& ui, const QString& storagePath) {
    const auto newQuery = ui.newQuery;
    const auto open = ui.open;
    const auto save = ui.save;
    const auto saveAs = ui.saveAs;
    const auto queryMenu = ui.queryMenu;
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
    const auto queryOverflowMenu = ui.queryOverflowMenu;
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
            if (auto* data = object->findChild<ObjectDataWorkspace*>();
                data && !data->resolvePendingEdits())
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
            savedStatus->setText(QObject::tr("No saved queries yet.\n\nSave a query as a SQL file "
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
                                     : QObject::tr("No saved queries yet.\n\nSave a query as a SQL "
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
            grid->setColumnWidth(column, qBound(80, grid->columnWidth(column), 400));
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
                              kind == QStringLiteral("table") ? 4 : -1);
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
    connect(
        workspace_->adapter(), &EngineAdapter::profilesReady, this,
        [this, savedConnections](quint64 token, const QList<SavedProfile>& profiles) {
            if (token != profileListToken_)
                return;
            const auto selected =
                savedConnections->currentItem()
                    ? savedConnections->currentItem()->data(Qt::UserRole).value<SavedProfile>().id
                    : QString{};
            savedConnections->clear();
            for (const auto& profile : profiles) {
                auto* item =
                    new QListWidgetItem(profile.name + "\n" +
                                            (profile.driver == "sqlite"  ? tr("SQLite")
                                             : profile.driver == "mysql" ? tr("MySQL")
                                                                         : tr("PostgreSQL")),
                                        savedConnections);
                item->setData(Qt::UserRole, QVariant::fromValue(profile));
                item->setData(design::NavigationProfileDelegate::DriverRole, profile.driver);
                item->setIcon(
                    design::themedIcon(profile.driver == "sqlite"     ? design::Icon::SQLite
                                       : profile.driver == "postgres" ? design::Icon::PostgreSQL
                                       : profile.driver == "mysql"    ? design::Icon::MySQL
                                                                      : design::Icon::Database,
                                       theme_->resolvedTheme().colors.mutedText, 16));
                item->setToolTip(profile.name);
                if (profile.id == selected)
                    savedConnections->setCurrentItem(item);
            }
            const int rowHeight = savedConnections->sizeHintForRow(0);
            savedConnections->setMaximumHeight(
                profiles.isEmpty()
                    ? 0
                    : profiles.size() * (rowHeight + 2 * savedConnections->spacing()) +
                          2 * savedConnections->frameWidth());
        });
    connect(workspace_->adapter(), &EngineAdapter::profileSaved, this,
            [refreshProfiles] { refreshProfiles(); });
    connect(workspace_->adapter(), &EngineAdapter::profileDeleted, this,
            [this, refreshProfiles, savedConnections](quint64, const QString& id, const QString&) {
                if (id == pendingBrowseProfileId_) {
                    pendingBrowseConnection_.reset();
                    pendingBrowseProfileId_.clear();
                    pendingBrowseProfileName_.clear();
                }
                if (id == lastBrowsedProfileId_)
                    lastBrowsedProfileId_.clear();
                if (savedConnections->currentItem() &&
                    savedConnections->currentItem()->data(Qt::UserRole).value<SavedProfile>().id ==
                        id) {
                    savedConnections->setCurrentItem(nullptr);
                    browsingConnection_.reset();
                    if (navigatorController_)
                        navigatorController_->clearSelectedConnection();
                }
                refreshProfiles();
            });
    const auto selectProfile = [this, connections, savedConnections](QListWidgetItem* item) {
        if (!item || !allowDocumentChange())
            return;
        const auto profile = item->data(Qt::UserRole).value<SavedProfile>();
        pendingBrowseConnection_.reset();
        pendingBrowseProfileId_.clear();
        pendingBrowseProfileName_.clear();
        for (int i = 0; i < connections->count(); ++i) {
            if (!connections->itemData(i).isValid())
                continue;
            const auto id = connections->itemData(i).toULongLong();
            if (workspace_->profileIdForConnection(id) == profile.id) {
                if (allowDocumentChange()) {
                    browsingConnection_ = id;
                    lastBrowsedProfileId_ = profile.id;
                    if (navigatorController_)
                        navigatorController_->setSelectedConnection(id);
                    emit browsingConnectionChanged(id);
                }
                return;
            }
        }
        pendingBrowseProfileId_ = profile.id;
        pendingBrowseProfileName_ = profile.name;
        browsingConnection_.reset();
        if (navigatorController_)
            navigatorController_->setPendingConnection(profile.name);
        submittingBrowseProfile_ = true;
        pendingBrowseConnection_ = workspace_->connectSavedProfile(profile);
        submittingBrowseProfile_ = false;
        if (!pendingBrowseConnection_ && pendingBrowseProfileId_ == profile.id) {
            pendingBrowseProfileId_.clear();
            pendingBrowseProfileName_.clear();
            const auto reason = tr("Finish or cancel active database work before connecting.");
            std::optional<quint64> restore;
            QListWidgetItem* restoreItem = nullptr;
            for (int i = 0; i < savedConnections->count(); ++i) {
                auto* candidate = savedConnections->item(i);
                if (candidate->data(Qt::UserRole).value<SavedProfile>().id != lastBrowsedProfileId_)
                    continue;
                for (int j = 0; j < connections->count(); ++j) {
                    if (connections->itemData(j).isValid()) {
                        const auto id = connections->itemData(j).toULongLong();
                        if (workspace_->profileIdForConnection(id) == lastBrowsedProfileId_) {
                            restore = id;
                            restoreItem = candidate;
                            break;
                        }
                    }
                }
            }
            savedConnections->setCurrentItem(restoreItem);
            browsingConnection_ = restore;
            if (navigatorController_) {
                if (restore)
                    navigatorController_->setSelectedConnection(*restore);
                else
                    navigatorController_->clearSelectedConnection();
            }
            auto* dialog = new ConfirmationDialog(
                QMessageBox::Warning, tr("Connection failed"),
                tr("Could not open %1: %2").arg(profile.name, reason), QMessageBox::Ok, this);
            dialog->setObjectName("sidebarConnectionFailure");
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->open();
        }
    };
    reconnectProfile_ = [savedConnections, selectProfile](const QString& id) {
        for (int i = 0; i < savedConnections->count(); ++i) {
            auto* item = savedConnections->item(i);
            if (item->data(Qt::UserRole).value<SavedProfile>().id != id)
                continue;
            savedConnections->setCurrentItem(item);
            selectProfile(item);
            return true;
        }
        return false;
    };
    connect(savedConnections, &QListWidget::itemClicked, this, selectProfile);
    connect(savedConnections, &QListWidget::itemActivated, this, selectProfile);
    savedConnections->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(savedConnections, &QWidget::customContextMenuRequested, this,
            [this, savedConnections, connections, selectProfile](const QPoint& position) {
                auto* item = savedConnections->itemAt(position);
                if (!item || !allowDocumentChange())
                    return;
                savedConnections->setCurrentItem(item);
                const auto profile = item->data(Qt::UserRole).value<SavedProfile>();
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
                connectAction->setEnabled(!session && !pendingBrowseConnection_);
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
                menu->popup(design::detail::contextMenuPosition(
                    savedConnections->viewport()->mapToGlobal(position)));
            });
    connect(workspace_, &QueryWorkspace::connectionReady, this, [this](quint64 id) {
        if (pendingBrowseConnection_ != id)
            return;
        pendingBrowseConnection_.reset();
        lastBrowsedProfileId_ = pendingBrowseProfileId_;
        pendingBrowseProfileId_.clear();
        pendingBrowseProfileName_.clear();
        if (allowDocumentChange()) {
            browsingConnection_ = id;
            if (navigatorController_)
                navigatorController_->setSelectedConnection(id);
            emit browsingConnectionChanged(id);
        }
    });
    connect(
        workspace_->adapter(), &EngineAdapter::eventReady, this,
        [this, savedConnections, connections](const BridgeEvent& event) {
            const auto kind = QString::fromUtf8(event.kind.data(), qsizetype(event.kind.size()));
            if (kind == "connection_failed" && pendingBrowseConnection_ == event.id) {
                pendingBrowseConnection_.reset();
                const auto failedName = pendingBrowseProfileName_;
                pendingBrowseProfileId_.clear();
                pendingBrowseProfileName_.clear();
                std::optional<quint64> restore;
                QListWidgetItem* restoreItem = nullptr;
                for (int i = 0; i < savedConnections->count(); ++i) {
                    auto* item = savedConnections->item(i);
                    if (item->data(Qt::UserRole).value<SavedProfile>().id != lastBrowsedProfileId_)
                        continue;
                    for (int j = 0; j < connections->count(); ++j) {
                        if (connections->itemData(j).isValid()) {
                            const auto id = connections->itemData(j).toULongLong();
                            if (workspace_->profileIdForConnection(id) == lastBrowsedProfileId_) {
                                restore = id;
                                restoreItem = item;
                                break;
                            }
                        }
                    }
                }
                savedConnections->setCurrentItem(restoreItem);
                browsingConnection_ = restore;
                if (navigatorController_) {
                    if (restore)
                        navigatorController_->setSelectedConnection(*restore);
                    else
                        navigatorController_->clearSelectedConnection();
                }
                auto* dialog = new ConfirmationDialog(
                    QMessageBox::Warning, tr("Connection failed"),
                    tr("Could not open %1: %2")
                        .arg(failedName,
                             QString::fromUtf8(event.error.data(), qsizetype(event.error.size()))),
                    QMessageBox::Ok, this);
                dialog->setObjectName("sidebarConnectionFailure");
                dialog->setAttribute(Qt::WA_DeleteOnClose);
                dialog->open();
            }
            if (kind == "disconnected" && browsingConnection_ == event.id) {
                browsingConnection_.reset();
                savedConnections->setCurrentItem(nullptr);
                if (navigatorController_)
                    navigatorController_->clearSelectedConnection();
            }
        },
        Qt::DirectConnection);
    connect(workspace_->adapter(), &EngineAdapter::profileFailed, this,
            [this](quint64 token, const QString& error) {
                if (token == profileListToken_)
                    showToast(tr("Saved connections: %1").arg(error), ToastVariant::Danger);
            });
    connect(workspace_->adapter(), &EngineAdapter::profileConnectFailed, this,
            [this, navigatorStatus, savedConnections, connections](const QString& error) {
                navigatorStatus->setText(tr("! Connection failed"));
                navigatorStatus->setProperty("state", "error");
                navigatorStatus->setAccessibleName(
                    tr("Navigator connection status: %1").arg(navigatorStatus->text()));
                navigatorStatus->style()->unpolish(navigatorStatus);
                navigatorStatus->style()->polish(navigatorStatus);
                if (!submittingBrowseProfile_ || pendingBrowseProfileId_.isEmpty()) {
                    showToast(error, ToastVariant::Danger);
                    return;
                }
                const auto name = pendingBrowseProfileName_;
                pendingBrowseProfileId_.clear();
                pendingBrowseProfileName_.clear();
                auto* dialog = new ConfirmationDialog(QMessageBox::Warning, tr("Connection failed"),
                                                      tr("Could not open %1: %2").arg(name, error),
                                                      QMessageBox::Ok, this);
                dialog->setObjectName("sidebarConnectionFailure");
                dialog->setAttribute(Qt::WA_DeleteOnClose);
                dialog->open();
                std::optional<quint64> restore;
                for (int i = 0; i < savedConnections->count(); ++i)
                    if (savedConnections->item(i)->data(Qt::UserRole).value<SavedProfile>().id ==
                        lastBrowsedProfileId_)
                        savedConnections->setCurrentRow(i);
                for (int i = 0; i < connections->count(); ++i)
                    if (connections->itemData(i).isValid()) {
                        const auto id = connections->itemData(i).toULongLong();
                        if (workspace_->profileIdForConnection(id) == lastBrowsedProfileId_)
                            restore = id;
                    }
                if (!restore)
                    savedConnections->setCurrentItem(nullptr);
                browsingConnection_ = restore;
                if (navigatorController_) {
                    if (restore)
                        navigatorController_->setSelectedConnection(*restore);
                    else
                        navigatorController_->clearSelectedConnection();
                }
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
    auto* querySettings = queryMenu->addAction(tr("Query settings…"));
    querySettings->setObjectName("querySettings");
    queryOverflowMenu->addSeparator();
    queryOverflowMenu->addAction(querySettings);
    queryOverflowMenu->addAction(open);
    connect(querySettings, &QAction::triggered, workspace_, &QueryWorkspace::showQuerySettings);
    preferences_->initialize(workspace_->adapter());
}
} // namespace choscordb
