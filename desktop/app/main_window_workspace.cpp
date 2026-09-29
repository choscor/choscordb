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
#include <atomic>
#include <memory>

namespace choscordb {
namespace {
QString fromRust(const rust::String& value) {
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

rust::Str pathView(const QByteArray& encoded) {
    return {encoded.constData(), static_cast<size_t>(encoded.size())};
}

struct SavedSqlEntry {
    QString path;
    QString relativePath;
};

struct SavedSqlListing {
    QList<SavedSqlEntry> entries;
    QString error;
    bool hasMore = false;
};

SavedSqlListing loadSavedSql(const QString& root) {
    SavedSqlListing result;
    if (!root.isValidUtf16()) {
        result.error = QObject::tr("Path is not valid Unicode.");
        return result;
    }
    const auto encoded = root.toUtf8();
    const auto dto = saved_sql_list_directory(pathView(encoded));
    result.error = fromRust(dto.error);
    result.hasMore = dto.has_more;
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
    const auto dto = saved_sql_document_identity(pathView(encoded));
    return {fromRust(dto.path), fromRust(dto.error)};
}

QString prepareSavedSqlDirectory(const QString& root) {
    if (!root.isValidUtf16())
        return QObject::tr("Path is not valid Unicode.");
    const auto encoded = root.toUtf8();
    return fromRust(saved_sql_prepare_directory(pathView(encoded)));
}

void elideResultSource(QLabel* source) {
    const auto full = source->property("fullSource").toString();
    const auto visible = source->fontMetrics().elidedText(full, Qt::ElideMiddle, source->width());
    if (source->text() != visible)
        source->setText(visible);
}

void elideResultOutcome(QLabel* outcome) {
    const auto full = outcome->property("fullOutcome").toString();
    if (full.isEmpty())
        return;
    const auto visible = outcome->fontMetrics().elidedText(full, Qt::ElideRight, outcome->width());
    if (outcome->text() != visible)
        outcome->setText(visible);
}

void fitResultFooter(QWidget* footer) {
    if (!footer)
        return;
    auto* source = footer->findChild<QLabel*>("executionSummary");
    auto* outcome = footer->findChild<QLabel*>("executionStateCompact");
    auto* previous = footer->findChild<QPushButton*>("previousPage");
    auto* next = footer->findChild<QPushButton*>("nextPage");
    if (!source || !outcome || !previous || !next)
        return;
    outcome->setMaximumWidth(qMax(outcome->minimumWidth(), footer->width() / 3));
    auto* layout = footer->layout();
    const auto margins = layout->contentsMargins();
    const int spacing = layout->spacing();
    int space =
        footer->width() - margins.left() - margins.right() - previous->sizeHint().width() -
        next->sizeHint().width() - qMin(outcome->sizeHint().width(), outcome->maximumWidth()) -
        source->fontMetrics().horizontalAdvance(QStringLiteral("Untitled query")) - 6 * spacing;
    // Page context survives first; size, row count, then duration yield as space shrinks.
    for (const char* name :
         {"executionPage", "executionDuration", "executionRows", "executionVisibleSize"}) {
        auto* metric = footer->findChild<QLabel*>(QString::fromLatin1(name));
        if (!metric)
            continue;
        const bool show =
            !metric->text().isEmpty() && space >= metric->sizeHint().width() + spacing;
        metric->setVisible(show);
        if (show)
            space -= metric->sizeHint().width() + spacing;
    }
    elideResultSource(source);
    elideResultOutcome(outcome);
}
} // namespace

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::Resize &&
        watched->objectName() == QLatin1String("sqlResultFooter"))
        fitResultFooter(qobject_cast<QWidget*>(watched));
    else if (event->type() == QEvent::Resize &&
             watched->objectName() == QLatin1String("executionSummary"))
        elideResultSource(qobject_cast<QLabel*>(watched));
    else if (event->type() == QEvent::Resize &&
             watched->objectName() == QLatin1String("executionStateCompact"))
        elideResultOutcome(qobject_cast<QLabel*>(watched));
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
    auto* resultFooter = empty->parentWidget();
    resultFooter->installEventFilter(this);
    empty->installEventFilter(this);
    compactState->installEventFilter(this);
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
                            folder->setData(0, NavigatorModel::KindRole, "schema");
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
                    result.hasMore ? QObject::tr("Showing the first %1 files.").arg(1000)
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
            showToast(tr("Saved file cannot be opened while the workspace is busy."),
                      ToastVariant::Warning);
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
                        showToast(tr("Saved file cannot be opened while the workspace is busy."),
                                  ToastVariant::Warning);
                        return;
                    }
                    if (identities.isEmpty() || !identities.front().error.isEmpty()) {
                        pendingSavedOpens->remove(path);
                        showToast(tr("Could not open %1: %2")
                                      .arg(QFileInfo(path).fileName(),
                                           identities.isEmpty() ? tr("Path is invalid.")
                                                                : identities.front().error),
                                  ToastVariant::Danger);
                        return;
                    }
                    for (int i = 0; i < openEditors.size(); ++i) {
                        auto* existing = openEditors.at(i).data();
                        if (existing && existing->filePath() == openPaths.at(i) &&
                            identities.at(i + 1).error.isEmpty() &&
                            identities.at(i + 1).path == identities.front().path) {
                            pendingSavedOpens->remove(path);
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
                                    if (error.isEmpty() || editors_->indexOf(editor) < 0)
                                        return;
                                    showToast(tr("Could not open %1: %2")
                                                  .arg(QFileInfo(openedPath).fileName(), error),
                                              ToastVariant::Danger);
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
                        showToast(
                            tr("Could not create saved SQL directory: %1").arg(savedDirectory),
                            ToastVariant::Danger);
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
    connect(workspace_, &QueryWorkspace::documentTargetChanged, this,
            &MainWindow::refreshResultFooterColor);
    refreshResultFooterColor();
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
            [this, cancelButton, resultFooter](const QString& state) {
                const bool active =
                    state == "queued" || state == "running" || state == "cancelling";
                const bool restoreFocus = !active && cancelButton->hasFocus();
                if (restoreFocus && editors_->currentWidget())
                    editors_->currentWidget()->setFocus();
                fitResultFooter(resultFooter);
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
                for (auto it = pendingBrowseProfiles_.begin(); it != pendingBrowseProfiles_.end();
                     ++it) {
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
