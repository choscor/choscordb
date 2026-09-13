#include "app/main_window.h"
#include "app/appearance_controller.h"
#include "app/editor_preferences.h"
#include "app/navigator_controller.h"
#include "app/query_workspace.h"
#include "app/workspace_recovery.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/components.h"
#include "design_system/icons.h"
#include "design_system/platform_accessibility.h"
#include "design_system/theme_manager.h"
#include "design_system/typography.h"
#ifdef CHOSCORDB_DEVELOPMENT_PREVIEW
#include "design_system/preview_window.h"
#endif
#include "models/navigator_model.h"
#include "widgets/confirmation_dialog.h"
#include "widgets/editor_completion.h"
#include "widgets/history_dock.h"
#include "widgets/search_panel.h"
#include "widgets/sql_editor.h"
#include "widgets/toast_region.h"
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QStyleHints>
#include <QTabBar>
#include <QTabWidget>
#include <QTableView>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

#include <utility>

int qInitResources_resources();

namespace choscordb {

namespace {

class ContextualActionVisibility final : public QObject {
  public:
    ContextualActionVisibility(QWidget* region, QList<QWidget*> actions)
        : QObject(region), region_(region), actions_(std::move(actions)) {
        region_->installEventFilter(this);
        connect(qApp, &QApplication::focusChanged, this,
                [this](QWidget*, QWidget*) { updateForCurrentInput(); });
        setActionsVisible(false);
    }

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == region_) {
            if (event->type() == QEvent::Enter) {
                setActionsVisible(true);
            } else if (event->type() == QEvent::Leave) {
                QTimer::singleShot(0, this, [this] { updateForCurrentInput(); });
            }
        }
        return QObject::eventFilter(watched, event);
    }

  private:
    void updateForCurrentInput() {
        auto* focused = QApplication::focusWidget();
        setActionsVisible(region_->underMouse() || focused == region_ ||
                          (focused != nullptr && region_->isAncestorOf(focused)));
    }

    void setActionsVisible(bool visible) {
        for (auto* action : actions_) {
            action->setVisible(visible);
        }
    }

    QWidget* region_;
    QList<QWidget*> actions_;
};

class ResponsiveQueryToolbar final : public QObject {
  public:
    ResponsiveQueryToolbar(QWidget* window, design::ThemeManager* theme,
                           QList<QAction*> secondaryActions, QList<QToolButton*> compactActions,
                           QAction* overflow)
        : QObject(window), window_(window), theme_(theme), secondaryActions_(secondaryActions),
          compactActions_(std::move(compactActions)), overflow_(overflow) {
        window_->installEventFilter(this);
        connect(theme_, &design::ThemeManager::metricsChanged, this, [this] { updateForWidth(); });
        updateForWidth();
    }

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == window_ &&
            (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
            updateForWidth();
        }
        return QObject::eventFilter(watched, event);
    }

  private:
    void updateForWidth() {
        const bool narrow = window_->width() <= theme_->metrics().narrowWorkspaceWidth;
        for (auto* action : secondaryActions_) {
            action->setVisible(!narrow);
        }
        for (auto* button : compactActions_) {
            button->setToolButtonStyle(narrow ? Qt::ToolButtonIconOnly
                                              : Qt::ToolButtonTextBesideIcon);
            button->setProperty("iconOnly", narrow);
        }
        overflow_->setVisible(narrow);
    }

    QWidget* window_;
    design::ThemeManager* theme_;
    QList<QAction*> secondaryActions_;
    QList<QToolButton*> compactActions_;
    QAction* overflow_;
};

class HoveredTabCloseVisibility final : public QObject {
  public:
    explicit HoveredTabCloseVisibility(QTabBar* tabs) : QObject(tabs), tabs_(tabs) {
        tabs_->setMouseTracking(true);
        tabs_->installEventFilter(this);
        connect(tabs_, &QTabBar::currentChanged, this, [this] { updateButtons(-1); });
        QTimer::singleShot(0, this, [this] { updateButtons(-1); });
    }

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched != tabs_) {
            return QObject::eventFilter(watched, event);
        }
        if (event->type() == QEvent::MouseMove) {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            updateButtons(tabs_->tabAt(mouse->position().toPoint()));
        } else if (event->type() == QEvent::Leave) {
            updateButtons(-1);
        } else if (event->type() == QEvent::ChildAdded || event->type() == QEvent::LayoutRequest) {
            QTimer::singleShot(0, this, [this] { updateButtons(-1); });
        }
        return QObject::eventFilter(watched, event);
    }

  private:
    void updateButtons(int hovered) {
        const auto side = static_cast<QTabBar::ButtonPosition>(
            tabs_->style()->styleHint(QStyle::SH_TabBar_CloseButtonPosition, nullptr, tabs_));
        for (int index = 0; index < tabs_->count(); ++index) {
            if (auto* button = tabs_->tabButton(index, side)) {
                button->setVisible(index == tabs_->currentIndex() || index == hovered);
            }
        }
    }

    QTabBar* tabs_;
};

} // namespace

MainWindow::MainWindow(QWidget* parent, const QString& storagePath) : QMainWindow(parent) {
    ::qInitResources_resources();
    theme_ = new design::ThemeManager(this);
    theme_->setSystemPalette(qApp->palette());
    theme_->setSystemAppearance(qApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark
                                    ? design::ResolvedAppearance::Dark
                                    : design::ResolvedAppearance::Light);
    theme_->installOn(qApp);
    platformAccessibility_ = new design::PlatformAccessibilityMonitor(theme_, this);
    connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, theme_,
            [this](Qt::ColorScheme scheme) {
                theme_->setSystemAppearance(scheme == Qt::ColorScheme::Dark
                                                ? design::ResolvedAppearance::Dark
                                                : design::ResolvedAppearance::Light);
            });
    preferences_ = new EditorPreferencesController(this);
    completion_ = new EditorCompletionController(this);
    setWindowTitle(tr("ChoscorDB"));
    setWindowIcon(design::themedIcon(design::Icon::AppMark, theme_->resolvedTheme().colors.action,
                                     theme_->metrics().iconLarge));
    const auto initialMetrics = theme_->metrics();
    resize(initialMetrics.defaultWorkspaceWidth, initialMetrics.defaultWorkspaceHeight);
    setMinimumSize(initialMetrics.minimumWorkspaceWidth, initialMetrics.minimumWorkspaceHeight);
    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    auto* newQuery = fileMenu->addAction(tr("New query"));
    newQuery->setObjectName("newQuery");
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
#ifdef CHOSCORDB_DEVELOPMENT_PREVIEW
    auto* previewAction = viewMenu->addAction(tr("Design system preview…"));
    previewAction->setObjectName("openDesignSystemPreview");
    connect(previewAction, &QAction::triggered, this, [this] {
        auto* preview = findChild<design::PreviewWindow*>(QString{}, Qt::FindDirectChildrenOnly);
        if (!preview) {
            preview = new design::PreviewWindow(this);
            preview->setAttribute(Qt::WA_DeleteOnClose);
        }
        preview->show();
        preview->raise();
        preview->activateWindow();
    });
#endif
    auto* navigator = new QDockWidget(tr("Connections"), this);
    navigator->setObjectName("navigator");
    navigator->toggleViewAction()->setText(tr("Connections"));
    navigator->setTitleBarWidget(new QWidget(navigator));
    auto* navBody = new QWidget(navigator);
    auto* navLayout = new QVBoxLayout(navBody);
    auto* navHeader = new QHBoxLayout;
    auto* navTitle = new design::Text(tr("Connections"), navBody);
    navTitle->setObjectName("navigatorTitle");
    navTitle->setWeight(QFont::DemiBold);
    auto* addConnection = new design::Button(tr("+"), navBody);
    addConnection->setObjectName("navigatorAddConnection");
    addConnection->setAccessibleName(tr("New connection"));
    addConnection->setToolTip(tr("New connection"));
    auto* refreshNavigator = new design::Button(tr("↻"), navBody);
    refreshNavigator->setObjectName("navigatorRefresh");
    refreshNavigator->setAccessibleName(tr("Refresh selected database object"));
    refreshNavigator->setToolTip(tr("Refresh selected database object"));
    refreshNavigator->setEnabled(false);
    auto* disconnectNavigator = new design::Button(tr("×"), navBody);
    disconnectNavigator->setObjectName("navigatorDisconnect");
    disconnectNavigator->setAccessibleName(tr("Disconnect selected session"));
    disconnectNavigator->setToolTip(tr("Disconnect selected session"));
    disconnectNavigator->setEnabled(false);
    for (auto* button : {addConnection, refreshNavigator, disconnectNavigator}) {
        button->setText({});
        button->setVariant(design::ButtonVariant::Ghost);
        button->setButtonSize(design::ButtonSize::IconSmall);
    }
    navLayout->setContentsMargins(initialMetrics.spacingMedium, initialMetrics.spacingMedium,
                                  initialMetrics.spacingMedium, initialMetrics.spacingMedium);
    navLayout->setSpacing(initialMetrics.spacingMedium);
    navHeader->setSpacing(initialMetrics.spacingSmall);
    navHeader->addWidget(navTitle);
    navHeader->addStretch();
    navHeader->addWidget(refreshNavigator);
    navHeader->addWidget(disconnectNavigator);
    navHeader->addWidget(addConnection);
    navLayout->addLayout(navHeader);
    auto* filter = new QLineEdit;
    filter->setPlaceholderText(tr("Filter objects…"));
    filter->setAccessibleName(tr("Filter database objects"));
    navLayout->addWidget(filter);
    auto* tree = new QTreeView;
    tree->setAccessibleName(tr("Database navigator"));
    tree->setHeaderHidden(true);
    navLayout->addWidget(tree);
    auto* navigatorStatus = new QLabel(tr("Disconnected"), navBody);
    navigatorStatus->setObjectName("navigatorStatus");
    navigatorStatus->setAccessibleName(tr("Navigator connection status: Disconnected"));
    navLayout->addWidget(navigatorStatus);
    new ContextualActionVisibility(navBody, {refreshNavigator, disconnectNavigator});
    navigator->setWidget(navBody);
    addDockWidget(Qt::LeftDockWidgetArea, navigator);
    resizeDocks({navigator}, {initialMetrics.initialNavigatorWidth}, Qt::Horizontal);
    viewMenu->addAction(navigator->toggleViewAction());
    auto* resetLayout = viewMenu->addAction(tr("Reset layout"));
    resetLayout->setObjectName("resetLayout");
    auto* central = new QWidget;
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* toolbar = new QToolBar(tr("Query controls"));
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto* connections = new QComboBox;
    connections->setObjectName("connectionSelector");
    connections->setMinimumContentsLength(initialMetrics.connectionLabelCharacters);
    connections->setAccessibleName(tr("Active connection"));
    connections->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    connections->addItem(tr("No active connection"));
    connections->setEnabled(false);
    toolbar->addWidget(connections);
    auto runIcon = design::themedIcon(design::Icon::Run, theme_->resolvedTheme().colors.action,
                                      theme_->metrics().iconSmall);
    auto* run = toolbar->addAction(runIcon, tr("Run statement"));
    run->setObjectName("runStatement");
    run->setShortcut(QKeySequence("Ctrl+Return"));
    run->setEnabled(false);
    queryMenu->addAction(run);
    toolbar->widgetForAction(run)->setObjectName("runStatementButton");
    toolbar->widgetForAction(run)->setProperty("primary", true);
    auto cancelIcon = design::themedIcon(design::Icon::Cancel, theme_->resolvedTheme().colors.text,
                                         theme_->metrics().iconSmall);
    auto* cancel = toolbar->addAction(cancelIcon, tr("Cancel"));
    cancel->setEnabled(false);
    queryMenu->addAction(cancel);
    toolbar->addSeparator();
    auto* mode = new QComboBox;
    mode->setObjectName("transactionMode");
    mode->setMinimumContentsLength(initialMetrics.transactionLabelCharacters);
    mode->setAccessibleName(tr("Transaction mode"));
    mode->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    mode->addItems({tr("Auto-commit"), tr("Manual transaction")});
    mode->setEnabled(false);
    toolbar->addWidget(mode);
    auto* commitAction = queryMenu->addAction(tr("Commit"));
    commitAction->setEnabled(false);
    auto* commitButton = new QToolButton(toolbar);
    commitButton->setObjectName("commitTransactionButton");
    commitButton->setDefaultAction(commitAction);
    commitButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    auto* commitPresentation = toolbar->addWidget(commitButton);
    auto* rollbackAction = queryMenu->addAction(tr("Rollback"));
    rollbackAction->setEnabled(false);
    auto* rollbackButton = new QToolButton(toolbar);
    rollbackButton->setObjectName("rollbackTransactionButton");
    rollbackButton->setDefaultAction(rollbackAction);
    rollbackButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    auto* rollbackPresentation = toolbar->addWidget(rollbackButton);
    auto* queryOverflow = new QToolButton(toolbar);
    queryOverflow->setObjectName("queryToolbarOverflow");
    queryOverflow->setText(tr("More"));
    queryOverflow->setAccessibleName(tr("More query actions"));
    queryOverflow->setToolTip(tr("More query actions"));
    queryOverflow->setPopupMode(QToolButton::InstantPopup);
    auto* queryOverflowMenu = new QMenu(queryOverflow);
    queryOverflowMenu->addAction(commitAction);
    queryOverflowMenu->addAction(rollbackAction);
    queryOverflow->setMenu(queryOverflowMenu);
    auto* overflowPresentation = toolbar->addWidget(queryOverflow);
    new ResponsiveQueryToolbar(this, theme_, {commitPresentation, rollbackPresentation},
                               {qobject_cast<QToolButton*>(toolbar->widgetForAction(run)),
                                qobject_cast<QToolButton*>(toolbar->widgetForAction(cancel))},
                               overflowPresentation);
    preferences_->addAction("run_statement", run);
    preferences_->addAction("cancel_query", cancel);
    preferences_->addAction("commit", commitAction);
    preferences_->addAction("rollback", rollbackAction);
    const auto refreshIcons = [this, run, cancel, addConnection, refreshNavigator,
                               disconnectNavigator, toolbar] {
        const auto resolved = theme_->resolvedTheme();
        const auto metrics = theme_->metrics();
        toolbar->setIconSize(QSize(metrics.iconSmall, metrics.iconSmall));
        setWindowIcon(
            design::themedIcon(design::Icon::AppMark, resolved.colors.action, metrics.iconLarge));
        run->setIcon(
            design::themedIcon(design::Icon::Run, resolved.colors.actionText, metrics.iconSmall));
        cancel->setIcon(
            design::themedIcon(design::Icon::Cancel, resolved.colors.text, metrics.iconSmall));
        refreshNavigator->setDesignIcon(design::Icon::Refresh);
        disconnectNavigator->setDesignIcon(design::Icon::Close);
        addConnection->setDesignIcon(design::Icon::Add);
    };
    connect(theme_, &design::ThemeManager::themeChanged, this, refreshIcons);
    connect(theme_, &design::ThemeManager::metricsChanged, this, refreshIcons);
    refreshIcons();
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
    editors_->tabBar()->setUsesScrollButtons(true);
    editors_->tabBar()->setElideMode(Qt::ElideRight);
    new HoveredTabCloseVisibility(editors_->tabBar());
    auto* listTabs = viewMenu->addAction(tr("Editor tabs…"));
    listTabs->setObjectName("listEditorTabs");
    listTabs->setShortcut(QKeySequence("Ctrl+Shift+L"));
    connect(listTabs, &QAction::triggered, this, [this] {
        auto* menu = new QMenu(this);
        menu->setObjectName("editorTabOverflow");
        menu->setAccessibleName(tr("Open editor tab"));
        menu->setAttribute(Qt::WA_DeleteOnClose);
        for (int index = 0; index < editors_->count(); ++index) {
            auto* action = menu->addAction(editors_->tabText(index));
            action->setCheckable(true);
            action->setChecked(index == editors_->currentIndex());
            connect(action, &QAction::triggered, editors_, [this, index] {
                editors_->setCurrentIndex(index);
                if (auto* editor = editors_->currentWidget())
                    editor->setFocus();
            });
        }
        menu->popup(editors_->tabBar()->mapToGlobal(editors_->tabBar()->rect().bottomLeft()));
    });
    splitter->addWidget(editors_);
    auto* results = new QTabWidget;
    auto* resultBody = new QWidget;
    auto* resultLayout = new QVBoxLayout(resultBody);
    resultLayout->setContentsMargins(initialMetrics.spacingLarge, initialMetrics.spacingLarge,
                                     initialMetrics.spacingLarge, initialMetrics.spacingLarge);
    resultLayout->setSpacing(initialMetrics.spacingMedium);
    auto* empty =
        new design::Text(tr("○ Disconnected · Connect and run a statement to view results."));
    empty->setObjectName("executionSummary");
    empty->setProperty("state", "disconnected");
    empty->setAccessibleName(tr("Execution status: disconnected"));
    resultLayout->addWidget(empty);
    auto* emptyActions = new QWidget(resultBody);
    emptyActions->setObjectName("emptyWorkspaceActions");
    auto* emptyActionsLayout = new QHBoxLayout(emptyActions);
    auto* emptyConnect = new design::Button(tr("Connect"), emptyActions);
    emptyConnect->setObjectName("emptyConnect");
    auto* emptyOpen = new design::Button(tr("Open SQL file"), emptyActions);
    emptyOpen->setObjectName("emptyOpenSql");
    auto* emptyNew = new design::Button(tr("New query"), emptyActions);
    emptyNew->setObjectName("emptyNewQuery");
    emptyOpen->setVariant(design::ButtonVariant::Outline);
    emptyNew->setVariant(design::ButtonVariant::Outline);
    emptyActionsLayout->setContentsMargins(0, initialMetrics.spacingLarge, 0,
                                           initialMetrics.spacingLarge);
    emptyActionsLayout->setSpacing(initialMetrics.spacingMedium);
    emptyActionsLayout->addStretch();
    emptyActionsLayout->addWidget(emptyConnect);
    emptyActionsLayout->addWidget(emptyOpen);
    emptyActionsLayout->addWidget(emptyNew);
    emptyActionsLayout->addStretch();
    resultLayout->addWidget(emptyActions);
    connect(connections, &QComboBox::currentIndexChanged, emptyActions,
            [connections, emptyActions] {
                emptyActions->setVisible(!connections->currentData().isValid());
            });
    auto* grid = new QTableView;
    grid->setObjectName("queryResults");
    grid->setAccessibleName(tr("Query results"));
    grid->setAlternatingRowColors(true);
    resultLayout->addWidget(grid);
    auto* pager = new QHBoxLayout;
    auto* previousPage = new design::Button(tr("Previous page"));
    previousPage->setObjectName("previousPage");
    previousPage->setEnabled(false);
    pager->addWidget(previousPage);
    auto* nextPage = new design::Button(tr("Next page"));
    nextPage->setObjectName("nextPage");
    nextPage->setEnabled(false);
    auto* exportResult = new design::Button(tr("Export…"));
    exportResult->setObjectName("exportResult");
    exportResult->setEnabled(false);
    for (auto* button : {previousPage, nextPage, exportResult})
        button->setVariant(design::ButtonVariant::Outline);
    pager->setSpacing(initialMetrics.spacingMedium);
    previousPage->setDesignIcon(design::Icon::ChevronLeft);
    nextPage->setDesignIcon(design::Icon::ChevronRight);
    exportResult->setDesignIcon(design::Icon::Export);
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
    splitter->setSizes({initialMetrics.initialEditorHeight, initialMetrics.initialResultsHeight});
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
    auto* toast = new ToastRegion(this);
    statusBar()->addWidget(toast, 1);
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
    connect(emptyNew, &QPushButton::clicked, newQuery, &QAction::trigger);
    connect(emptyOpen, &QPushButton::clicked, open, &QAction::trigger);
    connect(emptyConnect, &QPushButton::clicked, newConnection, &QAction::trigger);
    connect(addConnection, &QPushButton::clicked, newConnection, &QAction::trigger);
    connect(editors_, &QTabWidget::tabCloseRequested, this, [this](int index) {
        auto* editor = qobject_cast<SqlEditor*>(editors_->widget(index));
        if (editor->isModified() &&
            ConfirmationDialog::question(this, tr("Close query"), tr("Discard unsaved changes?"),
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
        [this, connections, navigatorStatus](const BridgeEvent& event) {
            const auto kind =
                QString::fromUtf8(event.kind.data(), static_cast<qsizetype>(event.kind.size()));
            if (kind == "connected") {
                statusBar()->showMessage(tr("Connected"));
                navigatorStatus->setText(tr("● Connected"));
                navigatorStatus->setProperty("state", "success");
            } else if (kind == "disconnected") {
                statusBar()->showMessage(connections->currentData().isValid() ? tr("Connected")
                                                                              : tr("Disconnected"));
                const bool connected = connections->currentData().isValid();
                navigatorStatus->setText(connected ? tr("● Connected") : tr("○ Disconnected"));
                navigatorStatus->setProperty("state", connected ? "success" : "disconnected");
            } else if (kind == "connection_failed") {
                navigatorStatus->setText(tr("! Connection failed"));
                navigatorStatus->setProperty("state", "error");
            } else if (kind == "query_state") {
                statusBar()->showMessage(
                    tr("Query · %1")
                        .arg(QString::fromUtf8(event.state.data(),
                                               static_cast<qsizetype>(event.state.size()))));
            }
            navigatorStatus->setAccessibleName(
                tr("Navigator connection status: %1").arg(navigatorStatus->text()));
            navigatorStatus->style()->unpolish(navigatorStatus);
            navigatorStatus->style()->polish(navigatorStatus);
        },
        Qt::DirectConnection);
    if (!storagePath.isEmpty()) {
        recovery_ = new WorkspaceRecoveryController(editors_, [this] { return addEditor(); }, this);
        auto* recoveryStatus = new QWidget;
        auto* recoveryLayout = new QHBoxLayout(recoveryStatus);
        recoveryLayout->setContentsMargins(0, 0, 0, 0);
        auto* recoveryMessage = new QLabel;
        recoveryMessage->setTextFormat(Qt::PlainText);
        auto* retry = new design::Button(tr("Retry recovery"));
        auto* startNew = new design::Button(tr("Start new workspace"));
        startNew->setVariant(design::ButtonVariant::Destructive);
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
                    ConfirmationDialog box(QMessageBox::Warning, tr("Workspace recovery failed"),
                                           error, QMessageBox::NoButton, this);
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
                    else {
                        appearanceCloseApproved_ = false;
                        recovery_->cancelClose();
                    }
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
                    resizeDocks({history_}, {theme_->metrics().initialHistoryHeight}, Qt::Vertical);
                    sized = true;
                }
            });
    viewMenu->addAction(history_->toggleViewAction());
    appearance_ = new AppearanceController(theme_, workspace_->adapter(), this, navigator, splitter,
                                           history_);
    preferences_->setAppearanceController(appearance_);
    connect(resetLayout, &QAction::triggered, appearance_, &AppearanceController::resetLayout);
    connect(appearance_, &AppearanceController::warningChanged, this,
            [toast](const QString& warning) {
                if (warning.isEmpty())
                    toast->clearNotice();
                else
                    toast->showPersistent(warning);
            });
    connect(appearance_, &AppearanceController::flushReady, this, [this] {
        appearanceCloseApproved_ = true;
        QTimer::singleShot(0, this, [this] { close(); });
    });
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
                    ConfirmationDialog box(QMessageBox::Warning, tr("History could not be flushed"),
                                           error, QMessageBox::NoButton, this);
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
                        appearanceCloseApproved_ = false;
                        recoveryCloseApproved_ = false;
                        if (recovery_)
                            recovery_->cancelClose();
                        workspace_->cancelShutdown();
                        history_->setEnabled(true);
                    }
                });
            });
    auto* navigatorController = new NavigatorController(workspace_->adapter(), tree, filter, this);
    auto* refreshNavigatorAction = new QAction(tr("Refresh selected object"), tree);
    refreshNavigatorAction->setObjectName("navigatorRefreshAction");
    refreshNavigatorAction->setEnabled(false);
    auto* disconnectNavigatorAction = new QAction(tr("Disconnect selected session"), tree);
    disconnectNavigatorAction->setObjectName("navigatorDisconnectAction");
    disconnectNavigatorAction->setEnabled(false);
    tree->addAction(newConnection);
    tree->addAction(refreshNavigatorAction);
    tree->addAction(disconnectNavigatorAction);
    connect(refreshNavigatorAction, &QAction::triggered, navigatorController,
            &NavigatorController::refreshCurrent);
    connect(disconnectNavigatorAction, &QAction::triggered, navigatorController,
            &NavigatorController::disconnectCurrent);
    connect(refreshNavigator, &QPushButton::clicked, refreshNavigatorAction, &QAction::trigger);
    connect(disconnectNavigator, &QPushButton::clicked, disconnectNavigatorAction,
            &QAction::trigger);
    connect(tree->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [tree, refreshNavigator, disconnectNavigator, refreshNavigatorAction,
             disconnectNavigatorAction](const QModelIndex& current) {
                const bool canRefresh = current.isValid();
                const bool canDisconnect =
                    current.data(NavigatorModel::KindRole).toString() == "connection";
                refreshNavigator->setEnabled(canRefresh);
                disconnectNavigator->setEnabled(canDisconnect);
                refreshNavigatorAction->setEnabled(canRefresh);
                disconnectNavigatorAction->setEnabled(canDisconnect);
                tree->setAccessibleDescription(
                    current.isValid()
                        ? QObject::tr("Context actions are available in the navigator header.")
                        : QString{});
            });
    connect(navigatorController, &NavigatorController::disconnectRequested, workspace_,
            &QueryWorkspace::disconnectConnection);
    connect(navigatorController, &NavigatorController::generationFailed, this,
            [toast](const QString& error) { toast->showNotice(error); });
    connect(navigatorController, &NavigatorController::sqlGenerated, this,
            [this, connections, toast](quint64 connection, const QString& sql) {
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
                toast->showNotice(tr("SQL generated. Review the draft before running."));
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
    if (appearance_ && !appearanceCloseApproved_ && !appearance_->flush()) {
        event->ignore();
        return;
    }
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
                const auto answer = ConfirmationDialog::question(
                    this, tr("Close workspace"), tr("Discard unsaved changes in the workspace?"),
                    QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
                if (answer != QMessageBox::Discard) {
                    appearanceCloseApproved_ = false;
                    event->ignore();
                    return;
                }
                break;
            }
        }
    }
    if (workspace_ && !workspace_->confirmShutdown()) {
        appearanceCloseApproved_ = false;
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
                    ConfirmationDialog box(QMessageBox::Warning, tr("Open failed"), error,
                                           QMessageBox::Ok, this);
                    box.setTextFormat(Qt::PlainText);
                    box.exec();
                } else
                    editors_->setTabText(editors_->indexOf(editor), QFileInfo(path).fileName());
            });
    connect(editor, &SqlEditor::fileSaved, this,
            [this, editor](const QString& path, const QString& error) {
                if (!error.isEmpty()) {
                    ConfirmationDialog box(QMessageBox::Warning, tr("Save failed"), error,
                                           QMessageBox::Ok, this);
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
