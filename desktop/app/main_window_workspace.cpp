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
#include "bridge/request_token.h"
#include "bridge/rust_text.h"
#include "bridge/text_filter.h"
#include "design_system/button/button.h"
#include "design_system/confirmation_dialog/confirmation_dialog.h"
#include "design_system/icons.h"
#include "design_system/menu/menu.h"
#include "design_system/navigation_profile_row/navigation_profile_row.h"
#include "design_system/status_line/status_line.h"
#include "design_system/text/text.h"
#include "design_system/theme_manager.h"
#include "models/navigator_model.h"
#include "widgets/editor_completion/editor_completion.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAbstractItemModel>
#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFutureWatcher>
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

namespace choscordb {
namespace {
using bridge_detail::fromRust;
using bridge_detail::utf8View;

struct SavedSqlEntry {
    QString path;
    QString relativePath;
};

struct SavedSqlListing {
    QList<SavedSqlEntry> entries;
    QString error;
    bool hasMore = false;
    quint32 limit = 0;
};

SavedSqlListing loadSavedSql(const QString& root) {
    SavedSqlListing result;
    if (!root.isValidUtf16()) {
        result.error = QObject::tr("Path is not valid Unicode.");
        return result;
    }
    const auto encoded = root.toUtf8();
    const auto dto = saved_sql_list_directory(utf8View(encoded));
    result.error = fromRust(dto.error);
    result.hasMore = dto.has_more;
    result.limit = dto.limit;
    for (const auto& entry : dto.entries)
        result.entries.append({fromRust(entry.path), fromRust(entry.relative_path)});
    return result;
}

struct SavedSqlIdentity {
    QString path;
    QString error;
};

SavedSqlIdentity documentIdentity(const QString& path) {
    if (!path.isValidUtf16())
        return {{}, QObject::tr("Path is not valid Unicode.")};
    const auto encoded = path.toUtf8();
    const auto dto = saved_sql_document_identity(utf8View(encoded));
    return {fromRust(dto.path), fromRust(dto.error)};
}

QString prepareSavedSqlDirectory(const QString& root) {
    if (!root.isValidUtf16())
        return QObject::tr("Path is not valid Unicode.");
    const auto encoded = root.toUtf8();
    return fromRust(saved_sql_prepare_directory(utf8View(encoded)));
}

} // namespace

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
    const auto messages = ui.messages;
    connect(completion_, &EditorCompletionController::partialCatalog, this,
            [this, partialShown = false](bool partial) mutable {
                const auto hint =
                    tr("Suggestions use loaded navigator objects. Expand nodes for more "
                       "names; large catalogs may be limited.");
                if (partial && !partialShown)
                    showStatus(hint, ToastVariant::Warning, QStringLiteral("completion"));
                else if (!partial)
                    clearStatus(QStringLiteral("completion"), hint);
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
    const auto storageBytes = storagePath.toUtf8();
    const auto defaultDataBytes = applicationDataDirectory().toUtf8();
    const auto savedDirectory =
        fromRust(saved_sql_root(utf8View(storageBytes), utf8View(defaultDataBytes)));
    auto filterSavedFiles = [savedFiles, savedSearch] {
        const TextFilter query(savedSearch->text());
        const auto filterItem = [&](const auto& self, QTreeWidgetItem* item,
                                    const QString& path) -> bool {
            const auto relativePath = path + item->text(0);
            bool matches = query.matches(relativePath);
            for (int i = 0; i < item->childCount(); ++i)
                matches = self(self, item->child(i), relativePath + '/') || matches;
            item->setHidden(!matches);
            if (!query.blank() && matches && item->childCount())
                item->setExpanded(true);
            return matches;
        };
        for (int i = 0; i < savedFiles->topLevelItemCount(); ++i)
            filterItem(filterItem, savedFiles->topLevelItem(i), {});
    };
    connect(savedSearch, &QLineEdit::textChanged, this, filterSavedFiles);
    const auto savedRefreshGeneration = std::make_shared<quint64>(0);
    auto refreshSavedFiles = [this, savedFiles, savedStatus, savedDirectory, filterSavedFiles,
                              savedRefreshGeneration] {
        const auto generation = ++*savedRefreshGeneration;
        savedFiles->clear();
        savedStatus->show();
        savedStatus->setText(QObject::tr("Loading saved queries…"));
        auto* watcher = new QFutureWatcher<SavedSqlListing>(this);
        connect(
            watcher, &QFutureWatcher<SavedSqlListing>::finished, this,
            [watcher, savedFiles, savedStatus, savedDirectory, filterSavedFiles,
             savedRefreshGeneration, generation] {
                const auto result = watcher->result();
                watcher->deleteLater();
                if (generation != *savedRefreshGeneration)
                    return;
                if (!result.error.isEmpty()) {
                    savedStatus->setText(
                        QObject::tr("Saved SQL directory cannot be read: %1").arg(savedDirectory));
                    return;
                }
                QHash<QString, QTreeWidgetItem*> folders;
                for (const auto& entry : result.entries) {
                    const auto relative = QDir::fromNativeSeparators(entry.relativePath);
                    const auto parts = relative.split('/');
                    QTreeWidgetItem* parent = savedFiles->invisibleRootItem();
                    QString folderPath;
                    for (int i = 0; i + 1 < parts.size(); ++i) {
                        folderPath += parts.at(i) + '/';
                        auto* folder = folders.value(folderPath);
                        if (!folder) {
                            folder = new QTreeWidgetItem(parent, {parts.at(i)});
                            folder->setData(0, NavigatorModel::KindRole, "folder");
                            folders.insert(folderPath, folder);
                        }
                        parent = folder;
                    }
                    auto* item = new QTreeWidgetItem(parent, {parts.last()});
                    item->setData(0, Qt::UserRole, entry.path);
                    item->setToolTip(0, relative);
                    item->setData(0, NavigatorModel::KindRole, "file");
                }
                savedFiles->sortItems(0, Qt::AscendingOrder);
                savedStatus->setText(
                    result.hasMore ? QObject::tr("Showing the first %1 files.").arg(result.limit)
                    : result.entries.isEmpty()
                        ? QObject::tr("No saved queries yet.\nSave a query as a SQL file "
                                      "in the default folder to find it here.")
                        : QString{});
                savedStatus->setVisible(!savedStatus->text().isEmpty());
                filterSavedFiles();
            });
        watcher->setFuture(
            QtConcurrent::run([savedDirectory] { return loadSavedSql(savedDirectory); }));
    };
    refreshSavedFiles_ = refreshSavedFiles;
    connect(sidebarPanels, &QStackedWidget::currentChanged, this, [refreshSavedFiles](int index) {
        if (index == 1)
            refreshSavedFiles();
    });
    const auto pendingSavedOpens = std::make_shared<QSet<QString>>();
    const auto savedOpenGeneration = std::make_shared<quint64>(0);
    auto openSavedItem = [this, savedDirectory, pendingSavedOpens,
                          savedOpenGeneration](QTreeWidgetItem* item) {
        const auto path = item->data(0, Qt::UserRole).toString();
        if (path.isEmpty() || pendingSavedOpens->contains(path))
            return;
        if (!allowDocumentChange() || databaseClosePending_ ||
            (recovery_ && (!recovery_->isReady() || recovery_->isClosing()))) {
            showStatus(tr("Saved file cannot be opened while the workspace is busy."),
                       ToastVariant::Warning, QStringLiteral("saved"), path);
            return;
        }
        pendingSavedOpens->insert(path);
        const auto generation = ++*savedOpenGeneration;
        QList<QPointer<SqlEditor>> openEditors;
        QStringList openPaths;
        for (int i = 0; i < editors_->count(); ++i) {
            auto* existing = qobject_cast<SqlEditor*>(editors_->widget(i));
            if (!existing || existing->filePath().isEmpty())
                continue;
            openEditors.append(existing);
            openPaths.append(existing->filePath());
        }
        auto* watcher = new QFutureWatcher<QList<SavedSqlIdentity>>(this);
        connect(watcher, &QFutureWatcher<QList<SavedSqlIdentity>>::finished, this,
                [this, watcher, openEditors, openPaths, path, savedDirectory, pendingSavedOpens,
                 savedOpenGeneration, generation] {
                    const auto identities = watcher->result();
                    watcher->deleteLater();
                    const bool focusRequested = generation == *savedOpenGeneration;
                    if (!allowDocumentChange() || databaseClosePending_ ||
                        (recovery_ && (!recovery_->isReady() || recovery_->isClosing()))) {
                        pendingSavedOpens->remove(path);
                        showStatus(tr("Saved file cannot be opened while the workspace is busy."),
                                   ToastVariant::Warning, QStringLiteral("saved"), path);
                        return;
                    }
                    if (identities.isEmpty() || !identities.front().error.isEmpty()) {
                        pendingSavedOpens->remove(path);
                        showStatus(tr("Could not open %1: %2")
                                       .arg(QFileInfo(path).fileName(),
                                            identities.isEmpty() ? tr("Path is invalid.")
                                                                 : identities.front().error),
                                   ToastVariant::Danger, QStringLiteral("saved"), path);
                        return;
                    }
                    for (int i = 0; i < openEditors.size(); ++i) {
                        auto* existing = openEditors.at(i).data();
                        if (existing && existing->filePath() == openPaths.at(i) &&
                            identities.at(i + 1).error.isEmpty() &&
                            identities.at(i + 1).path == identities.front().path) {
                            pendingSavedOpens->remove(path);
                            clearStatus(QStringLiteral("saved"), {}, path);
                            if (focusRequested) {
                                editors_->setCurrentWidget(existing);
                                showScreen(Screen::Sql);
                            }
                            return;
                        }
                    }
                    const QPointer<QWidget> previouslyFocused = editors_->currentWidget();
                    if (auto* editor = addEditor()) {
                        connect(editor, &SqlEditor::fileOpened, this,
                                [this, editor, path, pendingSavedOpens](const QString& openedPath,
                                                                        const QString& error) {
                                    pendingSavedOpens->remove(path);
                                    editor->setProperty("savedSqlOpen", false);
                                    if (error.isEmpty()) {
                                        clearStatus(QStringLiteral("saved"), {}, path);
                                        return;
                                    }
                                    if (editors_->indexOf(editor) < 0)
                                        return;
                                    showStatus(tr("Could not open %1: %2")
                                                   .arg(QFileInfo(openedPath).fileName(), error),
                                               ToastVariant::Danger, QStringLiteral("saved"), path);
                                    editors_->removeTab(editors_->indexOf(editor));
                                    editor->deleteLater();
                                    if (!editors_->count())
                                        showScreen(Screen::Start);
                                });
                        connect(editor, &QObject::destroyed, this,
                                [pendingSavedOpens, path] { pendingSavedOpens->remove(path); });
                        editor->setProperty("savedSqlOpen", true);
                        editor->openSavedFile(savedDirectory, path);
                        if (!focusRequested && previouslyFocused &&
                            editors_->indexOf(previouslyFocused) >= 0)
                            editors_->setCurrentWidget(previouslyFocused);
                    } else
                        pendingSavedOpens->remove(path);
                });
        watcher->setFuture(QtConcurrent::run([path, openPaths] {
            QList<SavedSqlIdentity> identities;
            identities.reserve(openPaths.size() + 1);
            identities.append(documentIdentity(path));
            for (const auto& openPath : openPaths)
                identities.append(documentIdentity(openPath));
            return identities;
        }));
    };
    connect(savedFiles, &QTreeWidget::itemClicked, this, openSavedItem);
    connect(savedFiles, &QTreeWidget::itemActivated, this, openSavedItem);
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
                        showStatus(tr("Could not create saved SQL directory %1: %2")
                                       .arg(savedDirectory, error),
                                   ToastVariant::Danger, QStringLiteral("saved"), savedDirectory);
                        return;
                    }
                    clearStatus(QStringLiteral("saved"), {}, savedDirectory);
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
                            restoreResultRows,
                            compactState,
                            durationMetric,
                            pageMetric,
                            rowsMetric,
                            visibleSizeMetric},
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
        // perf-ok: configureResultTable limits the content sample to 50 rows.
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
    connect(workspace_, &QueryWorkspace::executionStateChanged, this,
            [this, cancelButton](const QString& state) {
                const bool active =
                    state == "queued" || state == "running" || state == "cancelling";
                const bool restoreFocus = !active && cancelButton->hasFocus();
                if (restoreFocus && editors_->currentWidget())
                    editors_->currentWidget()->setFocus();
            });
    auto* objectExplorer = makeObjectExplorer();
    objectExplorer->hide(); // Not part of the workspace until its first object tab opens.
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
    const auto syncVisible = [this, savedConnections, connections] {
        QList<quint64> ordered;
        std::optional<quint64> firstUsable;
        const QSignalBlocker selectionBlocked(savedConnections->selectionModel());
        for (int row = 0; row < savedConnections->count(); ++row) {
            auto* item = savedConnections->item(row);
            const auto profile = item->data(Qt::UserRole).value<SavedProfile>();
            const bool selected = selectedProfileIds_.contains(profile.id);
            item->setSelected(selected);
            item->setData(Qt::AccessibleDescriptionRole, selected
                                                             ? tr("Visible in Schema & Objects")
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
    };
    const auto showBrowseFailure = [this](const QString& name, const QString& reason) {
        auto* dialog = new ConfirmationDialog(QMessageBox::Warning, tr("Connection failed"),
                                              tr("Could not open %1: %2").arg(name, reason),
                                              QMessageBox::Ok, this);
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
                item->setIcon(design::themedIcon(design::driverIcon(profile.driver),
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
    connect(
        savedConnections, &QWidget::customContextMenuRequested, this,
        [this, savedConnections, connections, selectProfile, syncVisible](const QPoint& position) {
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
            design::popupContextMenu(*menu, savedConnections->viewport()->mapToGlobal(position));
        });
    connect(workspace_, &QueryWorkspace::connectionReady, this, [this, syncVisible](quint64 id) {
        const auto profileId = workspace_->profileIdForConnection(id);
        if (profileId.isEmpty())
            return;
        if (reconnectingProfile_ == profileId) {
            reconnectingProfile_.clear();
            showStatus(tr("Connected. Select the object tab to load fresh metadata."),
                       ToastVariant::Success, QStringLiteral("connection"));
        }
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
            const auto kind = fromRust(event.kind);
            if (kind == "connection_failed") {
                retiredBrowseConnections_.remove(event.id);
                for (auto it = pendingBrowseProfiles_.begin(); it != pendingBrowseProfiles_.end();
                     ++it) {
                    if (it->connection != event.id)
                        continue;
                    const auto profileId = it.key();
                    if (reconnectingProfile_ == profileId) {
                        reconnectingProfile_.clear();
                        showStatus(
                            tr("Could not reconnect %1: %2").arg(it->name, fromRust(event.error)),
                            ToastVariant::Danger, QStringLiteral("connection"));
                    }
                    const auto pending = it.value();
                    pendingBrowseProfiles_.erase(it);
                    if (navigatorController_)
                        navigatorController_->removePendingConnection(pending.placeholder);
                    if (selectedProfileIds_.remove(profileId)) {
                        syncVisible();
                        showBrowseFailure(pending.name, fromRust(event.error));
                    }
                    break;
                }
            } else if (kind == "disconnected") {
                for (auto it = pendingBrowseProfiles_.cbegin(); it != pendingBrowseProfiles_.cend();
                     ++it) {
                    if (it->connection == event.id && reconnectingProfile_ == it.key()) {
                        reconnectingProfile_.clear();
                        showStatus(tr("The connection closed before reconnecting completed."),
                                   ToastVariant::Danger, QStringLiteral("connection"));
                        break;
                    }
                }
                const auto profileId = sessionProfileIds_.take(event.id);
                if (!profileId.isEmpty() && reconnectingProfile_ == profileId) {
                    reconnectingProfile_.clear();
                    showStatus(tr("The connection closed before reconnecting completed."),
                               ToastVariant::Danger, QStringLiteral("connection"));
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
                    showStatus(tr("Saved connections: %1").arg(error), ToastVariant::Danger,
                               QStringLiteral("connection"));
            });
    connect(workspace_->adapter(), &EngineAdapter::profileConnectFailed, this,
            [this](const QString& error) {
                if (!submittingBrowseProfile_ || submittingBrowseProfileId_.isEmpty()) {
                    showStatus(error, ToastVariant::Danger, QStringLiteral("connection"));
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
