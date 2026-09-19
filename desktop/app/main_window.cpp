#include "app/main_window.h"
#include "app/appearance_controller.h"
#include "app/editor_preferences.h"
#include "app/navigator_controller.h"
#include "app/object_data_workspace.h"
#include "app/object_explorer.h"
#include "app/query_workspace.h"
#include "app/workspace_recovery.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/button/button.h"
#include "design_system/icons.h"
#include "design_system/item_view/item_view_style.h"
#include "design_system/platform_accessibility.h"
#include "design_system/text/text.h"
#include "design_system/theme_manager.h"
#include "design_system/table/table_style.h"
#ifdef CHOSCORDB_DEVELOPMENT_PREVIEW
#include "tools/preview/preview_window.h"
#endif
#include "design_system/confirmation_dialog/confirmation_dialog.h"
#include "design_system/toast_region/toast_region.h"
#include "models/navigator_model.h"
#include "widgets/editor_completion/editor_completion.h"
#include "widgets/history_dock/history_dock.h"
#include "widgets/search_panel/search_panel.h"
#include "widgets/sql_editor/sql_editor.h"
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
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
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
#include <QWidgetAction>

#include <atomic>
#include <utility>

int qInitResources_resources();

namespace choscordb {

namespace {

class NavigatorIconDelegate final : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const override {
        QStyledItemDelegate::initStyleOption(option, index);
        const auto kind = index.data(NavigatorModel::KindRole).toString();
        const auto role = kind == "connection"                      ? design::Icon::Database
                          : kind == "schema" || kind == "database"  ? design::Icon::Folder
                          : kind == "table" || kind == "view"       ? design::Icon::Table
                          : kind == "index" || kind.contains("key") ? design::Icon::Key
                                                                    : design::Icon::File;
        if (option->widget) {
            const auto colors = design::resolvedThemeForWidget(*option->widget).colors;
            option->icon = design::themedIcon(role, colors.mutedText, 16);
            option->features |= QStyleOptionViewItem::HasDecoration;
            option->decorationSize = QSize(16, 16);
        }
    }
};

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
            if (auto* e = qobject_cast<SqlEditor*>(editors_->currentWidget());
                e && showScreen(Screen::Sql))
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
    const QList<QPair<QString, QString>> screenNames = {{"showStart", tr("Start page")},
                                                        {"showSql", tr("SQL workspace")},
                                                        {"showObjects", tr("Object explorer")},
                                                        {"showHistory", tr("Query history")}};
    QList<QAction*> screenActions;
    for (int i = 0; i < screenNames.size(); ++i) {
        auto* action = viewMenu->addAction(screenNames[i].second);
        action->setObjectName(screenNames[i].first);
        connect(action, &QAction::triggered, this,
                [this, i] { showScreen(static_cast<Screen>(i)); });
        screenActions.append(action);
    }
    auto* quickSwitch = viewMenu->addAction(tr("Quick switch…"));
    quickSwitch->setObjectName("quickSwitch");
    connect(quickSwitch, &QAction::triggered, this, [this, screenActions] {
        auto* menu = new QMenu(this);
        menu->setObjectName("quickSwitchMenu");
        menu->setAttribute(Qt::WA_DeleteOnClose);
        for (auto* action : screenActions)
            menu->addAction(action);
        menu->popup(mapToGlobal(rect().center()));
    });
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
    navigator->setFeatures(QDockWidget::NoDockWidgetFeatures);
    navigator->setAllowedAreas(Qt::LeftDockWidgetArea);
    navigator->toggleViewAction()->setText(tr("Connections"));
    navigator->setTitleBarWidget(new QWidget(navigator));
    auto* navBody = new QWidget(navigator);
    navBody->setProperty("designSurface", "sidebar");
    navBody->setAttribute(Qt::WA_StyledBackground);
    auto* navLayout = new QVBoxLayout(navBody);
    auto* navHeader = new QHBoxLayout;
    auto* navTitle = new design::Text(tr("Connections").toUpper(), navBody);
    navTitle->setObjectName("navigatorTitle");
    navTitle->setTypographyRole(design::TypographyRole::SectionCaption);
    navTitle->setForegroundRole(QPalette::PlaceholderText);
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
    navLayout->setContentsMargins(initialMetrics.sidebarInset, initialMetrics.sidebarTopInset,
                                  initialMetrics.sidebarInset, initialMetrics.spacingMedium);
    navLayout->setSpacing(initialMetrics.spacingMedium);
    navHeader->setSpacing(initialMetrics.spacingSmall);
    navHeader->addWidget(navTitle);
    navHeader->addStretch();
    navHeader->addWidget(refreshNavigator);
    navHeader->addWidget(disconnectNavigator);
    navHeader->addWidget(addConnection);
    navLayout->addLayout(navHeader);
    auto* savedConnections = new QListWidget(navBody);
    savedConnections->setObjectName("savedConnections");
    savedConnections->setAccessibleName(tr("Saved database connections"));
    savedConnections->setItemDelegate(new design::NavigationProfileDelegate(savedConnections));
    savedConnections->setSpacing(design::spacing(design::Spacing::Half));
    savedConnections->setProperty("designSurface", "sidebar");
    savedConnections->setMouseTracking(true);
    savedConnections->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    navLayout->addWidget(savedConnections);
    auto* objectSection = new design::Text(tr("Schema & objects").toUpper(), navBody);
    objectSection->setTypographyRole(design::TypographyRole::SectionCaption);
    objectSection->setForegroundRole(QPalette::PlaceholderText);
    navLayout->addWidget(objectSection);
    auto* filter = new QLineEdit;
    filter->setPlaceholderText(tr("Filter objects…"));
    filter->setAccessibleName(tr("Filter database objects"));
    navLayout->addWidget(filter);
    auto* tree = new QTreeView;
    tree->setProperty("designSurface", "sidebar");
    tree->setItemDelegate(new NavigatorIconDelegate(tree));
    tree->setAccessibleName(tr("Database navigator"));
    tree->setHeaderHidden(true);
    navLayout->addWidget(tree, 3);
    auto* navigatorStatus = new QLabel(tr("Disconnected"), navBody);
    navigatorStatus->setObjectName("navigatorStatus");
    navigatorStatus->setAccessibleName(tr("Navigator connection status: Disconnected"));
    navLayout->addWidget(navigatorStatus);
    new ContextualActionVisibility(navBody, {refreshNavigator, disconnectNavigator});
    navigator->setWidget(navBody);
    addDockWidget(Qt::LeftDockWidgetArea, navigator);
    resizeDocks({navigator}, {initialMetrics.initialNavigatorWidth}, Qt::Horizontal);
    auto* focusConnections = viewMenu->addAction(tr("Connections"));
    connect(focusConnections, &QAction::triggered, tree, [tree] { tree->setFocus(); });
    auto* resetLayout = viewMenu->addAction(tr("Reset layout"));
    resetLayout->setObjectName("resetLayout");
    auto* central = new QWidget;
    central->setObjectName("sqlScreen");
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* toolbar = new QToolBar(tr("Query controls"));
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto* connections = new QComboBox;
    connections->setObjectName("connectionSelector");
    connections->setMinimumContentsLength(initialMetrics.connectionLabelCharacters);
    connections->setAccessibleName(tr("SQL document connection target"));
    connections->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    connections->addItem(tr("No active connection"));
    connections->setEnabled(false);
    toolbar->addWidget(connections);
    auto* toolbarSpacer = new QWidget(toolbar);
    toolbarSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(toolbarSpacer);
    auto* saveButton = new design::Button(tr("Save"), toolbar);
    saveButton->setObjectName("saveSqlButton");
    saveButton->setButtonSize(design::ButtonSize::Small);
    saveButton->setButtonContext(design::ButtonContext::EditorAction);
    saveButton->setVariant(design::ButtonVariant::Outline);
    saveButton->setAccessibleName(tr("Save SQL file"));
    toolbar->addWidget(saveButton);
    connect(saveButton, &QPushButton::clicked, save, &QAction::trigger);
    connect(save, &QAction::changed, saveButton,
            [save, saveButton] { saveButton->setEnabled(save->isEnabled()); });
    auto* run = queryMenu->addAction(tr("Run statement"));
    run->setObjectName("runStatement");
    run->setShortcut(QKeySequence("Ctrl+Return"));
    run->setEnabled(false);
    auto* runButton = new design::Button(tr("Run"), toolbar);
    runButton->setObjectName("runStatementButton");
    runButton->setAccessibleName(tr("Run selection or current statement"));
    runButton->setButtonSize(design::ButtonSize::Small);
    runButton->setButtonContext(design::ButtonContext::EditorAction);
    runButton->setDesignIcon(design::Icon::Run);
    runButton->setEnabled(false);
    toolbar->addWidget(runButton);
    connect(runButton, &QPushButton::clicked, run, &QAction::trigger);
    connect(run, &QAction::changed, runButton, [run, runButton] {
        runButton->setEnabled(run->isEnabled());
        runButton->setToolTip(run->text() + " · " +
                              run->shortcut().toString(QKeySequence::NativeText));
    });
    auto* cancel = queryMenu->addAction(tr("Cancel"));
    cancel->setEnabled(false);
    auto* cancelButton = new design::Button(tr("Cancel"), toolbar);
    cancelButton->setObjectName("cancelQueryButton");
    cancelButton->setButtonSize(design::ButtonSize::Small);
    cancelButton->setButtonContext(design::ButtonContext::EditorAction);
    cancelButton->setVariant(design::ButtonVariant::Outline);
    cancelButton->setEnabled(false);
    auto* cancelWidgetAction = toolbar->addWidget(cancelButton);
    cancelWidgetAction->setVisible(false);
    connect(cancelButton, &QPushButton::clicked, cancel, &QAction::trigger);
    connect(cancel, &QAction::changed, cancelButton, [cancel, cancelButton] {
        cancelButton->setEnabled(cancel->isEnabled());
        cancelButton->setText(cancel->text());
    });
    auto* mode = new QComboBox;
    mode->setObjectName("transactionMode");
    mode->setMinimumContentsLength(initialMetrics.transactionLabelCharacters);
    mode->setAccessibleName(tr("Transaction mode"));
    mode->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    mode->addItems({tr("Auto-commit"), tr("Manual transaction")});
    mode->setEnabled(false);
    auto* commitAction = queryMenu->addAction(tr("Commit"));
    commitAction->setEnabled(false);
    auto* rollbackAction = queryMenu->addAction(tr("Rollback"));
    rollbackAction->setEnabled(false);
    auto* queryOverflow = new QToolButton(this);
    queryOverflow->setProperty("designRole", "menuButton");
    queryOverflow->setObjectName("queryToolbarOverflow");
    queryOverflow->setText(tr("More"));
    queryOverflow->setAccessibleName(tr("Query session and result actions"));
    queryOverflow->setToolTip(tr("Query session and result actions"));
    queryOverflow->setPopupMode(QToolButton::InstantPopup);
    auto* queryOverflowMenu = new QMenu(queryOverflow);
    auto* modeAction = new QWidgetAction(queryOverflowMenu);
    modeAction->setDefaultWidget(mode);
    queryOverflowMenu->addAction(modeAction);
    queryOverflowMenu->addSeparator();
    queryOverflowMenu->addAction(commitAction);
    queryOverflowMenu->addAction(rollbackAction);
    queryOverflow->setMenu(queryOverflowMenu);
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
            design::themedIcon(design::Icon::Run, resolved.colors.text, metrics.iconSmall));
        cancel->setIcon(
            design::themedIcon(design::Icon::Cancel, resolved.colors.text, metrics.iconSmall));
        refreshNavigator->setDesignIcon(design::Icon::Refresh);
        disconnectNavigator->setDesignIcon(design::Icon::Close);
        addConnection->setDesignIcon(design::Icon::Add);
    };
    connect(theme_, &design::ThemeManager::themeChanged, this, refreshIcons);
    connect(theme_, &design::ThemeManager::metricsChanged, this, refreshIcons);
    refreshIcons();
    auto* splitter = new QSplitter(Qt::Vertical);
    editors_ = new QTabWidget;
    editors_->setObjectName("editorTabs");
    connect(theme_, &design::ThemeManager::themeChanged, editors_, [this] {
        const auto icon =
            design::themedIcon(design::Icon::Code, theme_->resolvedTheme().colors.mutedText, 16);
        for (int index = 0; index < editors_->count(); ++index)
            editors_->setTabIcon(index, icon);
    });
    connect(editors_, &QTabWidget::currentChanged, this, [this] {
        if (activeDocument_ && editors_->currentWidget() != activeDocument_ &&
            !allowDocumentChange()) {
            const QSignalBlocker blocker(editors_);
            editors_->setCurrentWidget(activeDocument_);
            activeDocument_->setFocus();
            return;
        }
        activeDocument_ = editors_->currentWidget();
        completion_->setEditor(qobject_cast<SqlEditor*>(editors_->currentWidget()));
        if (workspace_)
            workspace_->documentChanged();
    });
    for (auto* action : editingActions)
        editors_->addAction(action);
    editors_->setTabsClosable(true);
    editors_->setMovable(true);
    editors_->setDocumentMode(true);
    editors_->tabBar()->setUsesScrollButtons(true);
    editors_->tabBar()->setElideMode(Qt::ElideRight);
    editors_->tabBar()->setProperty("designTabVariant", "document");
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
    auto* editorPane = new QWidget;
    auto* editorPaneLayout = new QVBoxLayout(editorPane);
    editorPaneLayout->setContentsMargins(0, 0, 0, 0);
    editorPaneLayout->setSpacing(0);
    editorPaneLayout->addWidget(editors_, 1);
    editorPaneLayout->addWidget(toolbar);
    splitter->addWidget(editorPane);
    auto* resultArea = new QWidget;
    auto* resultAreaLayout = new QVBoxLayout(resultArea);
    resultAreaLayout->setContentsMargins(0, 0, 0, 0);
    resultAreaLayout->setSpacing(0);
    auto* results = new QStackedWidget;
    results->setObjectName("sqlResultViews");
    auto* resultBody = new QWidget;
    auto* resultLayout = new QVBoxLayout(resultBody);
    resultLayout->setContentsMargins(0, 0, 0, 0);
    resultLayout->setSpacing(0);
    auto* empty =
        new design::Text(tr("○ Disconnected · Connect and run a statement to view results."));
    empty->setObjectName("executionSummary");
    empty->setTextFormat(Qt::PlainText);
    empty->setWordWrap(false);
    empty->setTypographyRole(design::TypographyRole::Small);
    empty->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    empty->setMinimumWidth(0);
    empty->setProperty("state", "disconnected");
    empty->setAccessibleName(tr("Execution status: disconnected"));
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
    grid->setFrameShape(QFrame::NoFrame);
    design::configureResultTable(*grid);
    grid->setWordWrap(false);
    grid->horizontalHeader()->setStretchLastSection(true);
    grid->horizontalHeader()->setResizeContentsPrecision(50);
    grid->verticalHeader()->setDefaultSectionSize(initialMetrics.sqlResultRowHeight);
    grid->horizontalHeader()->setFixedHeight(initialMetrics.sqlResultHeaderHeight);
    resultLayout->addWidget(grid, 1);
    auto* resultFooter = new QWidget;
    resultFooter->setObjectName("sqlResultFooter");
    auto* pager = new QHBoxLayout(resultFooter);
    pager->setContentsMargins(initialMetrics.spacingMedium, initialMetrics.spacingSmall,
                              initialMetrics.spacingMedium, initialMetrics.spacingSmall);
    auto* resultView = new QComboBox(resultFooter);
    resultView->setObjectName("resultViewSelector");
    resultView->setAccessibleName(tr("SQL result view"));
    resultView->addItems({tr("Results"), tr("Messages")});
    connect(resultView, &QComboBox::currentIndexChanged, results, &QStackedWidget::setCurrentIndex);
    pager->addWidget(resultView);
    pager->addWidget(empty, 1);
    auto* compactState = new design::Text(tr("Disconnected"), resultFooter);
    compactState->setObjectName("executionStateCompact");
    compactState->setTypographyRole(design::TypographyRole::Small);
    pager->addWidget(compactState);
    auto* previousPage = new design::Button({});
    previousPage->setAccessibleName(tr("Previous page"));
    previousPage->setToolTip(tr("Previous page"));
    previousPage->setButtonSize(design::ButtonSize::IconSmall);
    previousPage->setObjectName("previousPage");
    previousPage->setEnabled(false);
    auto* nextPage = new design::Button({});
    nextPage->setAccessibleName(tr("Next page"));
    nextPage->setToolTip(tr("Next page"));
    nextPage->setButtonSize(design::ButtonSize::IconSmall);
    nextPage->setObjectName("nextPage");
    nextPage->setEnabled(false);
    auto* exportResult = new design::Button(tr("Export"));
    exportResult->setButtonSize(design::ButtonSize::Small);
    exportResult->setObjectName("exportResult");
    exportResult->setEnabled(false);
    for (auto* button : {previousPage, nextPage})
        button->setVariant(design::ButtonVariant::Ghost);
    pager->setSpacing(initialMetrics.spacingMedium);
    previousPage->setDesignIcon(design::Icon::ChevronLeft);
    nextPage->setDesignIcon(design::Icon::ChevronRight);
    exportResult->setDesignIcon(design::Icon::Export);
    pager->addWidget(previousPage);
    pager->addWidget(nextPage);
    pager->addWidget(queryOverflow);
    pager->addWidget(exportResult);
    const auto colorResultFooter = [this, resultFooter] {
        auto palette = resultFooter->palette();
        palette.setColor(QPalette::Window, theme_->resolvedTheme().colors.subtleAccent);
        resultFooter->setAutoFillBackground(true);
        resultFooter->setPalette(palette);
    };
    connect(theme_, &design::ThemeManager::themeChanged, this, colorResultFooter);
    colorResultFooter();
    auto* messages = new QPlainTextEdit;
    messages->setObjectName("queryMessages");
    messages->setReadOnly(true);
    messages->setAccessibleName(tr("Query messages"));
    results->addWidget(resultBody);
    results->addWidget(messages);
    resultAreaLayout->addWidget(results, 1);
    resultAreaLayout->addWidget(resultFooter);
    splitter->addWidget(resultArea);
    splitter->setSizes({initialMetrics.initialEditorHeight, initialMetrics.initialResultsHeight});
    layout->addWidget(splitter);
    search_ = new SearchPanel(
        [this] { return qobject_cast<SqlEditor*>(editors_->currentWidget()); }, this);
    editorPaneLayout->insertWidget(0, search_);
    editMenu->addSeparator();
    QList<QAction*> searchActions;
    auto searchAction = [this, editMenu, &searchActions](const QString& id, const QString& text,
                                                         QKeySequence shortcut, auto method) {
        auto* action = editMenu->addAction(text);
        action->setShortcut(shortcut);
        searchActions.append(action);
        preferences_->addAction(id, action);
        connect(action, &QAction::triggered, this, [this, method] {
            if (editors_->isEnabled() && editors_->currentWidget() && showScreen(Screen::Sql))
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
        if (editors_->isEnabled() && editors_->currentWidget() && showScreen(Screen::Sql))
            completion_->requestCompletion();
    });
    editMenu->addSeparator();
    auto* preferencesAction = editMenu->addAction(tr("Preferences…"));
    preferencesAction->setObjectName("preferences");
    preferencesAction->setShortcut(QKeySequence::Preferences);
    preferences_->addAction("preferences", preferencesAction, false);
    connect(preferencesAction, &QAction::triggered, this, [this] {
        if (allowDocumentChange())
            preferences_->open();
    });
    connect(editors_, &QTabWidget::currentChanged, search_, &SearchPanel::editorChanged);
    screens_ = new QStackedWidget(this);
    screens_->setObjectName("centralScreens");
    auto* start = new QWidget;
    start->setObjectName("startScreen");
    start->setProperty("designSurface", "panel");
    start->setAttribute(Qt::WA_StyledBackground);
    auto* startLayout = new QVBoxLayout(start);
    startLayout->setContentsMargins(0, 0, 0, 0);
    startLayout->addStretch();
    auto* startIcon = new QLabel(start);
    startIcon->setObjectName("startDatabaseIcon");
    startIcon->setAlignment(Qt::AlignCenter);
    const auto colorStartIcon = [this, startIcon] {
        startIcon->setPixmap(
            design::themedIcon(design::Icon::Database, theme_->resolvedTheme().colors.mutedText, 30)
                .pixmap(30, 30));
    };
    colorStartIcon();
    connect(theme_, &design::ThemeManager::themeChanged, startIcon, colorStartIcon);
    startLayout->addWidget(startIcon);
    auto* welcome = new design::Text(tr("No database open"), start);
    welcome->setAlignment(Qt::AlignCenter);
    welcome->setWeight(QFont::Medium);
    startLayout->addWidget(welcome);
    auto* startHint =
        new design::Text(tr("Select a connection in the sidebar or create a new one."), start);
    startHint->setObjectName("startHint");
    startHint->setForegroundRole(QPalette::PlaceholderText);
    startHint->setWordWrap(true);
    startHint->setAlignment(Qt::AlignCenter);
    startLayout->addWidget(startHint);
    startLayout->addStretch();
    auto* startFooter = new QWidget(start);
    startFooter->setObjectName("startFooter");
    startFooter->setProperty("designSurface", "subtle");
    startFooter->setAttribute(Qt::WA_StyledBackground);
    auto* startActions = new QHBoxLayout(startFooter);
    startActions->setContentsMargins(
        design::spacing(design::Spacing::TwoHalf), initialMetrics.spacingSmall,
        design::spacing(design::Spacing::TwoHalf), initialMetrics.spacingSmall);
    auto* startStatus = new design::Text(tr("PostgreSQL · SQLite"), startFooter);
    startStatus->setTypographyRole(design::TypographyRole::Small);
    startStatus->setForegroundRole(QPalette::PlaceholderText);
    startActions->addWidget(startStatus);
    startActions->addStretch();
    auto* startConnect = new design::Button(tr("New connection"), start);
    startConnect->setObjectName("startNewConnection");
    startConnect->setButtonSize(design::ButtonSize::Small);
    startConnect->setDesignIcon(design::Icon::Add);
    connect(startConnect, &QPushButton::clicked, newConnection, &QAction::trigger);
    startActions->addWidget(startConnect);
    startLayout->addWidget(startFooter);
    screens_->addWidget(start);
    screens_->addWidget(central);
    auto* object = new QWidget;
    object->setObjectName("objectScreen");
    auto* objectLayout = new QVBoxLayout(object);
    auto* objectHint =
        new design::Text(tr("Select a table or view in the sidebar to inspect it."), object);
    objectHint->setWordWrap(true);
    objectHint->setAlignment(Qt::AlignCenter);
    objectLayout->addWidget(objectHint);
    screens_->addWidget(object);
    setCentralWidget(screens_);
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
        if (!allowDocumentChange())
            return;
        auto* editor = qobject_cast<SqlEditor*>(editors_->widget(index));
        if (editor->isModified() &&
            ConfirmationDialog::question(this, tr("Close query"), tr("Discard unsaved changes?"),
                                         QMessageBox::Discard | QMessageBox::Cancel,
                                         QMessageBox::Cancel) != QMessageBox::Discard)
            return;
        if (!allowDocumentChange())
            return;
        editors_->removeTab(index);
        editor->deleteLater();
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
    connect(run, &QAction::triggered, this, [this] { showScreen(Screen::Sql); });
    workspace_ =
        new QueryWorkspace({connections, mode, run, cancel, commitAction, rollbackAction,
                            newConnection, nextPage, empty, messages, grid,
                            [this] { return qobject_cast<SqlEditor*>(editors_->currentWidget()); },
                            this, previousPage, exportResult, storagePath},
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
        // A model reset can retain the previous last section's stretch size.
        // Recompute it after fitting the new bounded page.
        grid->horizontalHeader()->setStretchLastSection(false);
        grid->horizontalHeader()->setStretchLastSection(true);
        fitted = true;
    });
    connect(workspace_, &QueryWorkspace::executionStateChanged, this,
            [this, empty, compactState, cancelButton, cancelWidgetAction](const QString& state) {
                const bool active =
                    state == "queued" || state == "running" || state == "cancelling";
                const bool restoreFocus = !active && cancelButton->hasFocus();
                cancelWidgetAction->setVisible(active);
                cancelButton->setVisible(active);
                if (restoreFocus && editors_->currentWidget())
                    editors_->currentWidget()->setFocus();
                empty->setToolTip(empty->text());
                compactState->setText(state);
                compactState->setAccessibleName(tr("Execution status: %1").arg(state));
            });
    auto* objectExplorer = new ObjectExplorer(workspace_->adapter(), screens_);
    installObjectExplorer(objectExplorer);
    auto* objectData = new ObjectDataWorkspace(workspace_, objectExplorer);
    objectExplorer->installDataWidget(objectData);
    connect(objectExplorer, &ObjectExplorer::dataRequested, objectData,
            &ObjectDataWorkspace::openObject);
    connect(objectExplorer, &ObjectExplorer::objectChanged, objectData,
            &ObjectDataWorkspace::invalidate);
    connect(objectData, &ObjectDataWorkspace::busyChanged, objectExplorer,
            &ObjectExplorer::setOperationBusy);
    connect(this, &MainWindow::objectContextSelected, objectExplorer,
            [objectExplorer](quint64 connection, const QString& object, const QString& label,
                             const QString&) {
                objectExplorer->openObject(connection, object, label);
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
                auto* item = new QListWidgetItem(
                    profile.name + "\n" +
                        (profile.driver == "sqlite" ? tr("SQLite") : tr("PostgreSQL")),
                    savedConnections);
                item->setData(Qt::UserRole, QVariant::fromValue(profile));
                item->setData(design::NavigationProfileDelegate::DriverRole, profile.driver);
                item->setIcon(design::themedIcon(design::Icon::Database,
                                                 theme_->resolvedTheme().colors.mutedText, 16));
                item->setToolTip(profile.name);
                if (profile.id == selected)
                    savedConnections->setCurrentItem(item);
            }
            savedConnections->setMaximumHeight(profiles.isEmpty() ? 0 : profiles.size() * 49 + 4);
        });
    connect(workspace_->adapter(), &EngineAdapter::profileSaved, this,
            [refreshProfiles] { refreshProfiles(); });
    connect(workspace_->adapter(), &EngineAdapter::profileDeleted, this,
            [refreshProfiles] { refreshProfiles(); });
    const auto selectProfile = [this, connections](QListWidgetItem* item) {
        if (!item || !allowDocumentChange())
            return;
        const auto profile = item->data(Qt::UserRole).value<SavedProfile>();
        for (int i = 0; i < connections->count(); ++i) {
            if (!connections->itemData(i).isValid())
                continue;
            const auto id = connections->itemData(i).toULongLong();
            if (workspace_->profileIdForConnection(id) == profile.id) {
                if (showScreen(Screen::Object)) {
                    browsingConnection_ = id;
                    emit browsingConnectionChanged(id);
                }
                return;
            }
        }
        if (!pendingBrowseConnection_)
            pendingBrowseConnection_ = workspace_->connectSavedProfile(profile);
    };
    connect(savedConnections, &QListWidget::itemClicked, this, selectProfile);
    connect(savedConnections, &QListWidget::itemActivated, this, selectProfile);
    savedConnections->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(savedConnections, &QWidget::customContextMenuRequested, this,
            [this, savedConnections, connections](const QPoint& position) {
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
                connect(connectAction, &QAction::triggered, this, [this, profile] {
                    if (allowDocumentChange())
                        pendingBrowseConnection_ = workspace_->connectSavedProfile(profile);
                });
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
                menu->popup(savedConnections->viewport()->mapToGlobal(position));
            });
    connect(workspace_, &QueryWorkspace::connectionReady, this, [this](quint64 id) {
        if (pendingBrowseConnection_ != id)
            return;
        pendingBrowseConnection_.reset();
        if (showScreen(Screen::Object)) {
            browsingConnection_ = id;
            emit browsingConnectionChanged(id);
        }
    });
    connect(
        workspace_->adapter(), &EngineAdapter::eventReady, this,
        [this](const BridgeEvent& event) {
            const auto kind = QString::fromUtf8(event.kind.data(), qsizetype(event.kind.size()));
            if (kind == "connection_failed" && pendingBrowseConnection_ == event.id)
                pendingBrowseConnection_.reset();
            if (kind == "disconnected" && browsingConnection_ == event.id)
                browsingConnection_.reset();
        },
        Qt::DirectConnection);
    connect(workspace_->adapter(), &EngineAdapter::profileFailed, this,
            [this, navigatorStatus](quint64 token, const QString& error) {
                if (token == profileListToken_) {
                    navigatorStatus->setText(tr("Saved connections: %1").arg(error));
                    navigatorStatus->setToolTip(error);
                }
            });
    connect(workspace_->adapter(), &EngineAdapter::profileConnectFailed, this,
            [this, navigatorStatus](const QString& error) {
                pendingBrowseConnection_.reset();
                navigatorStatus->setText(error);
                navigatorStatus->setToolTip(error);
                statusBar()->showMessage(error);
            });
    auto* refreshSaved = viewMenu->addAction(tr("Refresh saved connections"));
    refreshSaved->setObjectName("refreshSavedConnections");
    connect(refreshSaved, &QAction::triggered, this, refreshProfiles);
    refreshProfiles();
    connect(workspace_, &QueryWorkspace::documentTargetChanged, emptyActions,
            [connections, emptyActions] {
                emptyActions->setVisible(!connections->currentData().isValid());
            });
    auto* querySettings = queryMenu->addAction(tr("Query settings…"));
    querySettings->setObjectName("querySettings");
    queryOverflowMenu->addSeparator();
    queryOverflowMenu->addAction(querySettings);
    queryOverflowMenu->addAction(open);
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
        connect(
            recovery_, &WorkspaceRecoveryController::restoreCompleted, this,
            [this](bool hasDocuments) { showScreen(hasDocuments ? Screen::Sql : Screen::Start); });
        recovery_->start();
    }
    history_ = new HistoryDock(workspace_->adapter(), this);
    connect(preferences_, &EditorPreferencesController::historyPolicyConfirmed, history_,
            &HistoryDock::applyConfirmedPolicy);
    screens_->addWidget(history_);
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
        if (!editor)
            return;
        if (!editor->restoreDocument(entry.sql.toUtf8(), {}, 0, 0, true)) {
            statusBar()->showMessage(tr("History text could not be opened."));
            return;
        }
        editor->setProfileId(entry.profileId);
        workspace_->documentChanged();
        editor->setProperty("documentTitle", tr("History query %1").arg(nextDocumentNumber_));
        editors_->setTabText(editors_->indexOf(editor),
                             editor->property("documentTitle").toString() + " •");
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
            [this, tree](const QModelIndex& current, const QModelIndex& previous) {
                if (!current.isValid())
                    return;
                if (!allowDocumentChange()) {
                    const QSignalBlocker blocker(tree->selectionModel());
                    tree->selectionModel()->setCurrentIndex(
                        previous, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
                    return;
                }
                auto object = current;
                auto kind = object.data(NavigatorModel::KindRole).toString();
                while (object.isValid() && kind != "table" && kind != "view" && kind != "schema" &&
                       kind != "connection") {
                    object = object.parent();
                    kind = object.data(NavigatorModel::KindRole).toString();
                }
                if (!object.isValid())
                    return;
                const auto connection = object.data(NavigatorModel::ConnectionRole);
                if (!connection.isValid())
                    return;
                browsingConnection_ = connection.toULongLong();
                emit browsingConnectionChanged(*browsingConnection_);
                if ((kind == "table" || kind == "view") && showScreen(Screen::Object))
                    emit objectContextSelected(
                        *browsingConnection_, object.data(NavigatorModel::ObjectIdRole).toString(),
                        object.data(NavigatorModel::QualifiedNameRole).toString(), kind);
            });
    connect(tree->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [tree, refreshNavigator, disconnectNavigator, refreshNavigatorAction,
             disconnectNavigatorAction](const QModelIndex&) {
                const auto current = tree->currentIndex();
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
    const auto openGeneratedSql = [this, connections, toast](quint64 connection,
                                                             const QString& sql) {
        if (databaseClosePending_ || !editors_->isEnabled() ||
            (recovery_ && (!recovery_->isReady() || recovery_->isClosing())))
            return;
        const int target = connections->findData(QVariant::fromValue<qulonglong>(connection));
        if (target < 0) {
            statusBar()->showMessage(tr("The selected connection is no longer available."));
            return;
        }
        if (target != connections->currentIndex() && !connections->isEnabled()) {
            statusBar()->showMessage(
                tr("Finish the active query before switching connections to generate SQL."));
            return;
        }
        const auto bytes = sql.toUtf8();
        if (!sql.isValidUtf16() || bytes.size() > DocumentIo::MaximumBytes) {
            statusBar()->showMessage(tr("Generated SQL exceeds editor limits."));
            return;
        }
        auto* editor = addEditor();
        if (!editor)
            return;
        if (!editor->restoreDocument(bytes, {}, 0, 0, true)) {
            editors_->removeTab(editors_->indexOf(editor));
            editor->deleteLater();
            statusBar()->showMessage(tr("Generated SQL could not be opened."));
            return;
        }
        connections->setCurrentIndex(target);
        editor->setConnectionTarget(connection, connections->itemText(target));
        workspace_->documentChanged();
        editor->setProfileId(workspace_->profileIdForConnection(connection));
        editor->setProperty("documentTitle", tr("Generated SQL %1").arg(nextDocumentNumber_));
        editors_->setTabText(editors_->indexOf(editor),
                             editor->property("documentTitle").toString() + " •");
        editor->setFocus();
        statusBar()->showMessage(tr("SQL generated. Review the draft before running."));
        toast->showNotice(tr("SQL generated. Review the draft before running."));
        if (recovery_)
            recovery_->changed();
    };
    connect(navigatorController, &NavigatorController::sqlGenerated, this, openGeneratedSql);
    connect(objectExplorer, &ObjectExplorer::sqlGenerated, this, openGeneratedSql);

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
    connect(workspace_, &QueryWorkspace::documentTargetChanged, this, rebuildCompletion);
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
    constructing_ = false;
    showScreen(Screen::Start);
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
bool MainWindow::allowDocumentChange() {
    if (!workspace_ || workspace_->navigationAllowed())
        return true;
    statusBar()->showMessage(tr("Finish or cancel the active database work before changing SQL "
                                "documents. Cancel remains in the active workspace."));
    return false;
}
bool MainWindow::showScreen(Screen screen) {
    if (!screens_)
        return false;
    if (screens_->currentIndex() != static_cast<int>(screen) && !allowDocumentChange())
        return false;
    if (screen == Screen::Sql && !editors_->count()) {
        addEditor();
        return true;
    }
    screens_->setCurrentIndex(static_cast<int>(screen));
    if (screen == Screen::History && history_)
        history_->refresh();
    if (screen == Screen::Sql && editors_->currentWidget())
        editors_->currentWidget()->setFocus();
    return true;
}
void MainWindow::installObjectExplorer(QWidget* explorer) {
    const auto index = static_cast<int>(Screen::Object);
    const bool visible = screens_->currentIndex() == index;
    auto* previous = screens_->widget(index);
    screens_->removeWidget(previous);
    screens_->insertWidget(index, explorer);
    previous->deleteLater();
    if (visible)
        screens_->setCurrentIndex(index);
}
void MainWindow::openConnectionQuery(quint64 connection) {
    auto* selector = findChild<QComboBox*>("connectionSelector");
    const auto index = selector->findData(QVariant::fromValue<qulonglong>(connection));
    if (index < 0 || !allowDocumentChange())
        return;
    auto* editor = addEditor();
    if (!editor)
        return;
    editor->setConnectionTarget(connection, selector->itemText(index));
    editor->setProfileId(workspace_->profileIdForConnection(connection));
    workspace_->documentChanged();
}
SqlEditor* MainWindow::addEditor() {
    if (!allowDocumentChange())
        return nullptr;
    auto* editor = new SqlEditor;
    preferences_->addEditor(editor);
    connect(editor, &SqlEditor::connectionTargetChanged, this, [this, editor] {
        if (workspace_ && editors_->currentWidget() == editor)
            workspace_->documentChanged();
    });
    const auto title = tr("Untitled query %1").arg(++nextDocumentNumber_);
    editor->setProperty("documentTitle", title);
    if (workspace_) {
        if (const auto* previous = qobject_cast<SqlEditor*>(editors_->currentWidget())) {
            editor->setConnectionTarget(previous->connectionTarget(), previous->targetLabel());
            editor->setProfileId(previous->property("profileId").toString());
        }
    }
    const int index = editors_->addTab(
        editor,
        design::themedIcon(design::Icon::Code, theme_->resolvedTheme().colors.mutedText, 16),
        title);
    editors_->setCurrentIndex(index);
    if (!constructing_)
        showScreen(Screen::Sql);
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
