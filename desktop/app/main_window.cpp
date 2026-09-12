#include "app/main_window.h"
#include "app/editor_preferences.h"
#include "app/navigator_controller.h"
#include "app/query_workspace.h"
#include "app/workspace_recovery.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "models/navigator_model.h"
#include "widgets/editor_completion.h"
#include "widgets/history_dock.h"
#include "widgets/search_panel.h"
#include "widgets/sql_editor.h"
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableView>
#include <QToolBar>
#include <QTreeView>
#include <QVBoxLayout>
namespace choscordb {
MainWindow::MainWindow(QWidget* parent, const QString& storagePath) : QMainWindow(parent) {
    preferences_ = new EditorPreferencesController(this);
    completion_ = new EditorCompletionController(this);
    setWindowTitle(tr("ChoscorDB"));
    resize(1280, 900);
    setMinimumSize(960, 640);
    setStyleSheet(QStringLiteral(
        "QToolBar { spacing: 8px; padding: 8px; border-bottom: 1px solid palette(mid); }"
        "QTabBar::tab { padding: 10px 16px; }"
        "QDockWidget::title { padding: 10px; }"
        "QTreeView::item { min-height: 28px; }"
        "QHeaderView::section { padding: 8px 12px; }"
        "QToolButton#runStatementButton:enabled { background: #83d4b1; color: #112b21; padding: "
        "6px 12px; border-radius: 4px; }"));
    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    auto* newQuery = fileMenu->addAction(tr("New query"));
    newQuery->setShortcut(QKeySequence::New);
    auto* open = fileMenu->addAction(tr("Open SQL file…"));
    open->setShortcut(QKeySequence::Open);
    auto* save = fileMenu->addAction(tr("Save SQL file…"));
    save->setShortcut(QKeySequence::Save);
    fileMenu->addSeparator();
    auto* quit =
        fileMenu->addAction(tr("Quit"), QKeySequence::Quit, qApp, &QApplication::closeAllWindows);
    preferences_->addAction("quit", quit, false);
    preferences_->addAction("new_query", newQuery);
    preferences_->addAction("open_sql_file", open);
    preferences_->addAction("save_sql_file", save);
    auto* editMenu = menuBar()->addMenu(tr("&Edit"));
    QList<QAction*> editingActions;
    auto editorAction = [this, editMenu, &editingActions](const QString& id, const QString& title,
                                                          QKeySequence shortcut, auto method) {
        auto* action = editMenu->addAction(title);
        action->setShortcut(shortcut);
        preferences_->addAction(id, action);
        editingActions.append(action);
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        connect(action, &QAction::triggered, this, [this, method] {
            if (!editors_->isEnabled())
                return;
            if (auto* e = qobject_cast<SqlEditor*>(editors_->currentWidget()))
                (e->*method)();
        });
    };
    editorAction("undo", tr("Undo"), QKeySequence::Undo, &SqlEditor::undo);
    editorAction("redo", tr("Redo"), QKeySequence::Redo, &SqlEditor::redo);
    editMenu->addSeparator();
    editorAction("cut", tr("Cut"), QKeySequence::Cut, &SqlEditor::cut);
    editorAction("copy", tr("Copy"), QKeySequence::Copy, &SqlEditor::copy);
    editorAction("paste", tr("Paste"), QKeySequence::Paste, &SqlEditor::paste);
    auto* queryMenu = menuBar()->addMenu(tr("&Query"));
    auto* connectionMenu = menuBar()->addMenu(tr("&Connection"));
    auto* newConnection = connectionMenu->addAction(tr("New SQLite session…"));

    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    auto* navigator = new QDockWidget(tr("Connections"), this);
    navigator->setObjectName("navigator");
    auto* navBody = new QWidget(navigator);
    auto* navLayout = new QVBoxLayout(navBody);
    auto* filter = new QLineEdit;
    filter->setPlaceholderText(tr("Filter objects…"));
    filter->setAccessibleName(tr("Filter database objects"));
    navLayout->addWidget(filter);
    auto* tree = new QTreeView;
    tree->setAccessibleName(tr("Database navigator"));
    tree->setHeaderHidden(true);
    navLayout->addWidget(tree);
    navigator->setWidget(navBody);
    addDockWidget(Qt::LeftDockWidgetArea, navigator);
    resizeDocks({navigator}, {245}, Qt::Horizontal);
    viewMenu->addAction(navigator->toggleViewAction());
    auto* central = new QWidget;
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* toolbar = new QToolBar(tr("Query controls"));
    toolbar->setIconSize(QSize(16, 16));
    auto* connections = new QComboBox;
    connections->setObjectName("connectionSelector");
    connections->addItem(tr("No active connection"));
    connections->setEnabled(false);
    toolbar->addWidget(connections);
    auto* run = toolbar->addAction(tr("▶ Run statement"));
    run->setObjectName("runStatement");
    run->setShortcut(QKeySequence("Ctrl+Return"));
    run->setEnabled(false);
    queryMenu->addAction(run);
    toolbar->widgetForAction(run)->setObjectName("runStatementButton");
    auto* cancel = toolbar->addAction(tr("Cancel"));
    cancel->setEnabled(false);
    queryMenu->addAction(cancel);
    toolbar->addSeparator();
    auto* mode = new QComboBox;
    mode->addItems({tr("Auto-commit"), tr("Manual transaction")});
    mode->setEnabled(false);
    toolbar->addWidget(mode);
    auto* commitAction = toolbar->addAction(tr("Commit"));
    commitAction->setEnabled(false);
    queryMenu->addAction(commitAction);
    auto* rollbackAction = toolbar->addAction(tr("Rollback"));
    rollbackAction->setEnabled(false);
    queryMenu->addAction(rollbackAction);
    preferences_->addAction("run_statement", run);
    preferences_->addAction("cancel_query", cancel);
    preferences_->addAction("commit", commitAction);
    preferences_->addAction("rollback", rollbackAction);
    layout->addWidget(toolbar);
    auto* splitter = new QSplitter(Qt::Vertical);
    editors_ = new QTabWidget;
    editors_->setObjectName("editorTabs");
    connect(editors_, &QTabWidget::currentChanged, this, [this] {
        completion_->setEditor(qobject_cast<SqlEditor*>(editors_->currentWidget()));
    });
    for (auto* action : editingActions)
        editors_->addAction(action);
    editors_->setTabsClosable(true);
    editors_->setMovable(true);
    editors_->setDocumentMode(true);
    splitter->addWidget(editors_);
    auto* results = new QTabWidget;
    auto* resultBody = new QWidget;
    auto* resultLayout = new QVBoxLayout(resultBody);
    auto* empty = new QLabel(tr("Connect to a database and run a statement to view results."));
    empty->setAlignment(Qt::AlignCenter);
    resultLayout->addWidget(empty);
    auto* grid = new QTableView;
    grid->setObjectName("queryResults");
    grid->setAccessibleName(tr("Query results"));
    grid->setAlternatingRowColors(true);
    resultLayout->addWidget(grid);
    auto* pager = new QHBoxLayout;
    auto* previousPage = new QPushButton(tr("← Previous page"));
    previousPage->setObjectName("previousPage");
    previousPage->setEnabled(false);
    pager->addWidget(previousPage);
    auto* nextPage = new QPushButton(tr("Next page →"));
    nextPage->setObjectName("nextPage");
    nextPage->setEnabled(false);
    auto* exportResult = new QPushButton(tr("Export…"));
    exportResult->setObjectName("exportResult");
    exportResult->setEnabled(false);
    pager->addWidget(exportResult);
    pager->addStretch();
    pager->addWidget(nextPage);
    resultLayout->addLayout(pager);
    auto* messages = new QPlainTextEdit;
    messages->setObjectName("queryMessages");
    messages->setReadOnly(true);
    messages->setAccessibleName(tr("Query messages"));
    results->addTab(resultBody, tr("Results"));
    results->addTab(messages, tr("Messages"));
    splitter->addWidget(results);
    splitter->setSizes({420, 340});
    layout->addWidget(splitter);
    search_ = new SearchPanel(
        [this] { return qobject_cast<SqlEditor*>(editors_->currentWidget()); }, this);
    layout->insertWidget(1, search_);
    editMenu->addSeparator();
    QList<QAction*> searchActions;
    auto searchAction = [this, editMenu, &searchActions](const QString& id, const QString& text,
                                                         QKeySequence shortcut, auto method) {
        auto* action = editMenu->addAction(text);
        action->setShortcut(shortcut);
        searchActions.append(action);
        preferences_->addAction(id, action);
        connect(action, &QAction::triggered, this, [this, method] {
            if (editors_->isEnabled())
                (search_->*method)();
        });
    };
    searchAction("find", tr("Find…"), QKeySequence::Find, &SearchPanel::showFind);
    searchAction("replace", tr("Replace…"), QKeySequence::Replace, &SearchPanel::showReplace);
    searchAction("find_next", tr("Find next"), QKeySequence::FindNext, &SearchPanel::findNext);
    searchAction("find_previous", tr("Find previous"), QKeySequence::FindPrevious,
                 &SearchPanel::findPrevious);
    editMenu->addSeparator();
    auto* completeAction = editMenu->addAction(tr("Complete SQL"));
    completeAction->setObjectName("completeSql");
#ifdef Q_OS_MAC
    completeAction->setShortcut(QKeySequence(Qt::META | Qt::Key_Space));
#else
    completeAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Space));
#endif
    preferences_->addAction("complete", completeAction, false);
    searchActions.append(completeAction);
    connect(completeAction, &QAction::triggered, this, [this] {
        if (editors_->isEnabled())
            completion_->requestCompletion();
    });
    editMenu->addSeparator();
    auto* preferencesAction = editMenu->addAction(tr("Preferences…"));
    preferencesAction->setObjectName("preferences");
    preferencesAction->setShortcut(QKeySequence::Preferences);
    preferences_->addAction("preferences", preferencesAction, false);
    connect(preferencesAction, &QAction::triggered, preferences_,
            &EditorPreferencesController::open);
    connect(editors_, &QTabWidget::currentChanged, search_, &SearchPanel::editorChanged);
    setCentralWidget(central);
    statusBar()->showMessage(tr("Disconnected · No background queries"));
    statusBar()->addPermanentWidget(new QLabel(tr("UTF-8   SQL")));
    auto* completionNote = new QLabel(tr("Loaded objects"));
    completionNote->setObjectName("completionCatalogNote");
    completionNote->setToolTip(tr("Suggestions use loaded navigator objects. Expand nodes for more "
                                  "names; large catalogs may be limited."));
    completionNote->hide();
    statusBar()->addPermanentWidget(completionNote);
    connect(completion_, &EditorCompletionController::partialCatalog, completionNote,
            &QWidget::setVisible);
    connect(newQuery, &QAction::triggered, this, [this] { addEditor(); });
    connect(editors_, &QTabWidget::tabCloseRequested, this, [this](int index) {
        auto* editor = qobject_cast<SqlEditor*>(editors_->widget(index));
        if (editor->isModified() &&
            QMessageBox::question(this, tr("Close query"), tr("Discard unsaved changes?"),
                                  QMessageBox::Discard | QMessageBox::Cancel,
                                  QMessageBox::Cancel) != QMessageBox::Discard)
            return;
        editors_->removeTab(index);
        editor->deleteLater();
        if (!editors_->count())
            addEditor();
        if (recovery_)
            recovery_->changed();
    });
    connect(open, &QAction::triggered, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, tr("Open SQL file"), {},
                                                       tr("SQL files (*.sql);;All files (*)"));
        if (path.isEmpty())
            return;
        auto* editor = addEditor();
        editor->openFile(path);
    });
    connect(save, &QAction::triggered, this, [this] {
        auto* editor = qobject_cast<SqlEditor*>(editors_->currentWidget());
        if (!editor)
            return;
        auto path = editor->filePath();
        if (path.isEmpty())
            path = QFileDialog::getSaveFileName(this, tr("Save SQL file"), {},
                                                tr("SQL files (*.sql)"));
        if (path.isEmpty())
            return;
        editor->saveFile(path);
    });
    addEditor();
    workspace_ =
        new QueryWorkspace({connections, mode, run, cancel, commitAction, rollbackAction,
                            newConnection, nextPage, empty, messages, grid,
                            [this] { return qobject_cast<SqlEditor*>(editors_->currentWidget()); },
                            this, previousPage, exportResult, storagePath},
                           this);
    auto* querySettings = queryMenu->addAction(tr("Query settings…"));
    querySettings->setObjectName("querySettings");
    connect(querySettings, &QAction::triggered, workspace_, &QueryWorkspace::showQuerySettings);
    preferences_->initialize(workspace_->adapter());
    connect(
        workspace_->adapter(), &EngineAdapter::eventReady, this,
        [this, connections](const BridgeEvent& event) {
            const auto kind =
                QString::fromUtf8(event.kind.data(), static_cast<qsizetype>(event.kind.size()));
            if (kind == "connected")
                statusBar()->showMessage(tr("Connected"));
            else if (kind == "disconnected")
                statusBar()->showMessage(connections->currentData().isValid() ? tr("Connected")
                                                                              : tr("Disconnected"));
            else if (kind == "query_state")
                statusBar()->showMessage(
                    tr("Query · %1")
                        .arg(QString::fromUtf8(event.state.data(),
                                               static_cast<qsizetype>(event.state.size()))));
        },
        Qt::DirectConnection);
    if (!storagePath.isEmpty()) {
        recovery_ = new WorkspaceRecoveryController(editors_, [this] { return addEditor(); }, this);
        auto* recoveryStatus = new QWidget;
        auto* recoveryLayout = new QHBoxLayout(recoveryStatus);
        recoveryLayout->setContentsMargins(0, 0, 0, 0);
        auto* recoveryMessage = new QLabel;
        recoveryMessage->setTextFormat(Qt::PlainText);
        auto* retry = new QPushButton(tr("Retry recovery"));
        auto* startNew = new QPushButton(tr("Start new workspace"));
        recoveryLayout->addWidget(recoveryMessage);
        recoveryLayout->addWidget(retry);
        recoveryLayout->addWidget(startNew);
        statusBar()->addWidget(recoveryStatus, 1);
        recoveryStatus->hide();
        connect(retry, &QPushButton::clicked, recovery_, &WorkspaceRecoveryController::retry);
        connect(startNew, &QPushButton::clicked, recovery_,
                &WorkspaceRecoveryController::startEmpty);
        connect(recovery_, &WorkspaceRecoveryController::mutationEnabled, this,
                [newQuery, open, save, editMenu, searchActions](bool enabled) {
                    editMenu->setEnabled(enabled);
                    for (auto* action : searchActions)
                        action->setEnabled(enabled);
                    newQuery->setEnabled(enabled);
                    open->setEnabled(enabled);
                    save->setEnabled(enabled);
                });
        connect(recovery_, &WorkspaceRecoveryController::restoreRequested, workspace_->adapter(),
                &EngineAdapter::restoreWorkspace);
        connect(recovery_, &WorkspaceRecoveryController::saveRequested, workspace_->adapter(),
                &EngineAdapter::saveWorkspace);
        connect(workspace_->adapter(), &EngineAdapter::workspaceRestored, recovery_,
                &WorkspaceRecoveryController::restored);
        connect(workspace_->adapter(), &EngineAdapter::workspaceSaved, recovery_,
                &WorkspaceRecoveryController::saved);
        connect(workspace_->adapter(), &EngineAdapter::recoveryFailed, recovery_,
                &WorkspaceRecoveryController::failed);
        connect(recovery_, &WorkspaceRecoveryController::persistenceSucceeded, recoveryStatus,
                &QWidget::hide);
        connect(
            recovery_, &WorkspaceRecoveryController::errorOccurred, this,
            [this, recoveryStatus, recoveryMessage, startNew](const QString& error, bool closing) {
                recoveryMessage->setText(error);
                startNew->setVisible(!recovery_->isReady());
                recoveryStatus->show();
                if (!closing)
                    return;
                // Queue the modal choice after synchronous transport rejection has unwound.
                QTimer::singleShot(0, this, [this, error] {
                    if (!recovery_->isClosing())
                        return;
                    QMessageBox box(QMessageBox::Warning, tr("Workspace recovery failed"), error,
                                    QMessageBox::NoButton, this);
                    box.setTextFormat(Qt::PlainText);
                    auto* retryButton = box.addButton(tr("Retry"), QMessageBox::AcceptRole);
                    auto* discardButton =
                        box.addButton(tr("Close without recovery"), QMessageBox::DestructiveRole);
                    auto* cancelButton = box.addButton(QMessageBox::Cancel);
                    box.setDefaultButton(cancelButton);
                    box.exec();
                    if (box.clickedButton() == retryButton)
                        recovery_->retry();
                    else if (box.clickedButton() == discardButton)
                        recovery_->closeWithoutRecovery();
                    else
                        recovery_->cancelClose();
                });
            });
        connect(recovery_, &WorkspaceRecoveryController::closeReady, this, [this] {
            recoveryCloseApproved_ = true;
            QTimer::singleShot(0, this, [this] { close(); });
        });
        connect(recovery_, &WorkspaceRecoveryController::mutationEnabled, search_,
                &QWidget::setEnabled);
        recovery_->start();
    }
    history_ = new HistoryDock(workspace_->adapter(), this);
    addDockWidget(Qt::BottomDockWidgetArea, history_);
    history_->hide();
    connect(history_, &QDockWidget::visibilityChanged, this,
            [this, sized = false](bool visible) mutable {
                if (visible && !sized) {
                    resizeDocks({history_}, {360}, Qt::Vertical);
                    sized = true;
                }
            });
    viewMenu->addAction(history_->toggleViewAction());
    if (recovery_) {
        history_->setEnabled(recovery_->isReady() && !recovery_->isClosing());
        connect(recovery_, &WorkspaceRecoveryController::mutationEnabled, history_,
                &QWidget::setEnabled);
    }
    connect(history_, &HistoryDock::openRequested, this, [this](const SavedHistoryEntry& entry) {
        if (databaseClosePending_ ||
            (recovery_ && (!recovery_->isReady() || recovery_->isClosing())))
            return;
        auto* editor = addEditor();
        if (!editor->restoreDocument(entry.sql.toUtf8(), {}, 0, 0, true)) {
            statusBar()->showMessage(tr("History text could not be opened."));
            return;
        }
        editor->setProfileId(entry.profileId);
        editors_->setTabText(editors_->indexOf(editor), tr("History query •"));
        if (recovery_)
            recovery_->changed();
    });
    connect(workspace_->adapter(), &EngineAdapter::eventReady, this,
            [this](const BridgeEvent& event) {
                const auto kind =
                    QString::fromUtf8(event.kind.data(), static_cast<qsizetype>(event.kind.size()));
                if (history_->isVisible() && (kind == "query_finished" || kind == "query_failed"))
                    history_->refresh();
            });
    connect(workspace_->adapter(), &EngineAdapter::shutdownReady, this, [this] {
        databaseClosePending_ = false;
        databaseCloseApproved_ = true;
        QTimer::singleShot(0, this, [this] { close(); });
    });
    connect(workspace_->adapter(), &EngineAdapter::shutdownFailed, this,
            [this](const QString& error, bool retryable) {
                QTimer::singleShot(0, this, [this, error, retryable] {
                    setEnabled(true);
                    QMessageBox box(QMessageBox::Warning, tr("History could not be flushed"), error,
                                    QMessageBox::NoButton, this);
                    box.setTextFormat(Qt::PlainText);
                    auto* retry =
                        retryable ? box.addButton(tr("Retry"), QMessageBox::AcceptRole) : nullptr;
                    auto* discard =
                        box.addButton(tr("Close without history"), QMessageBox::DestructiveRole);
                    auto* cancel = box.addButton(QMessageBox::Cancel);
                    box.setDefaultButton(cancel);
                    box.exec();
                    if (retry && box.clickedButton() == retry) {
                        setEnabled(false);
                        workspace_->beginShutdown();
                    } else if (box.clickedButton() == discard) {
                        workspace_->shutdown();
                        databaseCloseApproved_ = true;
                        databaseClosePending_ = false;
                        QTimer::singleShot(0, this, [this] { close(); });
                    } else {
                        databaseClosePending_ = false;
                        recoveryCloseApproved_ = false;
                        if (recovery_)
                            recovery_->cancelClose();
                        workspace_->cancelShutdown();
                        history_->setEnabled(true);
                    }
                });
            });
    auto* navigatorController = new NavigatorController(workspace_->adapter(), tree, filter, this);
    connect(navigatorController, &NavigatorController::disconnectRequested, workspace_,
            &QueryWorkspace::disconnectConnection);
    connect(navigatorController, &NavigatorController::generationFailed, this,
            [this](const QString& error) { statusBar()->showMessage(error); });
    connect(navigatorController, &NavigatorController::sqlGenerated, this,
            [this, connections](quint64 connection, const QString& sql) {
                if (databaseClosePending_ || !editors_->isEnabled() ||
                    (recovery_ && (!recovery_->isReady() || recovery_->isClosing())))
                    return;
                const int target =
                    connections->findData(QVariant::fromValue<qulonglong>(connection));
                if (target < 0) {
                    statusBar()->showMessage(tr("The selected connection is no longer available."));
                    return;
                }
                if (target != connections->currentIndex() && !connections->isEnabled()) {
                    statusBar()->showMessage(tr(
                        "Finish the active query before switching connections to generate SQL."));
                    return;
                }
                const auto bytes = sql.toUtf8();
                if (!sql.isValidUtf16() || bytes.size() > DocumentIo::MaximumBytes) {
                    statusBar()->showMessage(tr("Generated SQL exceeds editor limits."));
                    return;
                }
                auto* editor = addEditor();
                if (!editor->restoreDocument(bytes, {}, 0, 0, true)) {
                    editors_->removeTab(editors_->indexOf(editor));
                    editor->deleteLater();
                    statusBar()->showMessage(tr("Generated SQL could not be opened."));
                    return;
                }
                connections->setCurrentIndex(target);
                editor->setProfileId(workspace_->profileIdForConnection(connection));
                editors_->setTabText(editors_->indexOf(editor), tr("Generated SQL •"));
                editor->setFocus();
                statusBar()->showMessage(tr("SQL generated. Review the draft before running."));
                if (recovery_)
                    recovery_->changed();
            });
    auto rebuildCompletion = [this, connections, model = navigatorController->model()] {
        if (!connections->currentData().isValid()) {
            completion_->setCatalog(CompletionService{});
            return;
        }
        const auto limits = CompletionService::limits();
        auto snapshot =
            model->completionSnapshot(connections->currentData().toULongLong(),
                                      limits.maxMetadataEntries, limits.maxMetadataBytes);
        QList<CompletionCandidate> items;
        items.reserve(static_cast<qsizetype>(snapshot.objects.size()));
        for (auto& object : snapshot.objects)
            items.append(
                {std::move(object.name), std::move(object.qualifiedName), std::move(object.kind)});
        completion_->setCatalog(CompletionService(std::move(items), snapshot.partial));
    };
    connect(connections, &QComboBox::currentIndexChanged, this, rebuildCompletion);
    connect(navigatorController->model(), &NavigatorModel::completionChanged, this,
            [connections, rebuildCompletion](quint64 id) {
                if (id == connections->currentData().toULongLong())
                    rebuildCompletion();
            });
    rebuildCompletion();
    connect(workspace_, &QueryWorkspace::connectionReady, navigatorController,
            [navigatorController, connections](quint64 id) {
                navigatorController->addConnection(id, connections->itemText(connections->findData(
                                                           QVariant::fromValue<qulonglong>(id))));
            });
}
void MainWindow::closeEvent(QCloseEvent* event) {
    if (databaseCloseApproved_) {
        event->accept();
        return;
    }
    if (databaseClosePending_) {
        event->ignore();
        return;
    }
    if (recovery_ && !recoveryCloseApproved_) {
        event->ignore();
        recovery_->requestClose();
        return;
    }
    if (!recovery_) {
        for (int i = 0; i < editors_->count(); ++i) {
            auto* editor = qobject_cast<SqlEditor*>(editors_->widget(i));
            if (editor && editor->isModified()) {
                const auto answer = QMessageBox::question(
                    this, tr("Close workspace"), tr("Discard unsaved changes in the workspace?"),
                    QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
                if (answer != QMessageBox::Discard) {
                    event->ignore();
                    return;
                }
                break;
            }
        }
    }
    if (workspace_ && !workspace_->confirmShutdown()) {
        recoveryCloseApproved_ = false;
        if (recovery_)
            recovery_->cancelClose();
        event->ignore();
        return;
    }
    if (workspace_) {
        databaseClosePending_ = true;
        history_->setEnabled(false);
        setEnabled(false);
        event->ignore();
        workspace_->beginShutdown();
        return;
    }
    event->accept();
}
SqlEditor* MainWindow::addEditor() {
    auto* editor = new SqlEditor;
    preferences_->addEditor(editor);
    const int index = editors_->addTab(editor, tr("Untitled query"));
    editors_->setCurrentIndex(index);
    connect(editor, &SqlEditor::modificationChanged, this, [this, editor](bool modified) {
        auto title = editor->filePath().isEmpty() ? editor->property("documentTitle").toString()
                                                  : QFileInfo(editor->filePath()).fileName();
        if (title.isEmpty())
            title = tr("Untitled query");
        editors_->setTabText(editors_->indexOf(editor), title + (modified ? " •" : ""));
    });
    connect(editor, &SqlEditor::fileOpened, this,
            [this, editor](const QString& path, const QString& error) {
                if (!error.isEmpty()) {
                    QMessageBox box(QMessageBox::Warning, tr("Open failed"), error, QMessageBox::Ok,
                                    this);
                    box.setTextFormat(Qt::PlainText);
                    box.exec();
                } else
                    editors_->setTabText(editors_->indexOf(editor), QFileInfo(path).fileName());
            });
    connect(editor, &SqlEditor::fileSaved, this,
            [this, editor](const QString& path, const QString& error) {
                if (!error.isEmpty()) {
                    QMessageBox box(QMessageBox::Warning, tr("Save failed"), error, QMessageBox::Ok,
                                    this);
                    box.setTextFormat(Qt::PlainText);
                    box.exec();
                } else
                    editors_->setTabText(editors_->indexOf(editor),
                                         QFileInfo(path).fileName() +
                                             (editor->isModified() ? " •" : ""));
            });
    if (recovery_) {
        recovery_->watchEditor(editor);
        recovery_->changed();
    }
    return editor;
}
} // namespace choscordb
