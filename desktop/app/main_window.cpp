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
#include "design_system/menu/menu.h"
#include "design_system/navigation_profile_row/navigation_profile_row.h"
#include "design_system/platform_accessibility.h"
#include "design_system/table/table_style.h"
#include "design_system/text/text.h"
#include "design_system/theme_manager.h"
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
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
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
        if (kind == "group") {
            option->icon = QIcon();
            option->features &= ~QStyleOptionViewItem::HasDecoration;
            return;
        }
        const auto role = kind == "connection"                      ? design::Icon::Database
                          : kind == "schema" || kind == "database"  ? design::Icon::Folder
                          : kind == "table" || kind == "view"       ? design::Icon::Table
                          : kind == "index" || kind.contains("key") ? design::Icon::Key
                                                                    : design::Icon::File;
        if (option->widget) {
            const auto colors = design::resolvedThemeForWidget(*option->widget).colors;
            option->icon = design::themedIcon(role, colors.mutedText, 14);
            option->features |= QStyleOptionViewItem::HasDecoration;
            option->decorationSize = QSize(14, 14);
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

class WorkspaceTabBar final : public QTabBar {
  public:
    using QTabBar::QTabBar;

    void setHeader(QWidget* header) {
        header_ = header;
        header_->setParent(this);
        header_->show();
        updateGeometry();
        layoutHeader();
    }

    QSize sizeHint() const override {
        auto size = QTabBar::sizeHint();
        if (header_ && !header_->isHidden())
            size.rheight() += header_->sizeHint().height() + design::spacing(design::Spacing::Half);
        return size;
    }

    QSize minimumSizeHint() const override {
        auto size = QTabBar::minimumSizeHint();
        if (header_ && !header_->isHidden())
            size.rheight() +=
                header_->minimumSizeHint().height() + design::spacing(design::Spacing::Half);
        return size;
    }

    void setHeaderVisible(bool visible) {
        if (!header_ || header_->isHidden() != visible)
            return;
        header_->setVisible(visible);
        updateGeometry();
        layoutHeader();
    }

  protected:
    void resizeEvent(QResizeEvent* event) override {
        QTabBar::resizeEvent(event);
        layoutHeader();
    }

  private:
    void layoutHeader() {
        if (header_ && !header_->isHidden())
            header_->setGeometry(0, QTabBar::sizeHint().height(), width(),
                                 header_->sizeHint().height());
    }
    QPointer<QWidget> header_;
};

class WorkspaceTabs final : public QTabWidget {
  public:
    WorkspaceTabs() { setTabBar(new WorkspaceTabBar(this)); }
    WorkspaceTabBar* workspaceBar() const { return static_cast<WorkspaceTabBar*>(tabBar()); }
};

} // namespace

void MainWindow::showToast(const QString& message, ToastVariant variant) {
    if (toast_) {
        const auto title = variant == ToastVariant::Success ? tr("Success")
                           : variant == ToastVariant::Warning ? tr("Warning")
                                                               : tr("Error");
        toast_->showToast(title, message, variant);
    }
}

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
    auto* closeTab = fileMenu->addAction(tr("Close tab"));
    closeTab->setObjectName("closeWorkspaceTab");
    closeTab->setShortcut(QKeySequence::Close);
    connect(closeTab, &QAction::triggered, this, [this] {
        if (editors_ && editors_->currentIndex() >= 0)
            emit editors_->tabCloseRequested(editors_->currentIndex());
    });
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
    navLayout->setContentsMargins(initialMetrics.sidebarInset, initialMetrics.spacingSmall,
                                  initialMetrics.sidebarInset, initialMetrics.spacingMedium);
    navLayout->setSpacing(initialMetrics.spacingSmall);
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
    tree->setObjectName("databaseNavigator");
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
    auto* workspaceTabs = new WorkspaceTabs;
    editors_ = workspaceTabs;
    editors_->setObjectName("editorTabs");
    connect(theme_, &design::ThemeManager::themeChanged, editors_, [this] {
        for (int index = 0; index < editors_->count(); ++index) {
            const auto role = qobject_cast<ObjectExplorer*>(editors_->widget(index))
                                  ? design::Icon::Table
                                  : design::Icon::Code;
            editors_->setTabIcon(
                index, design::themedIcon(role, theme_->resolvedTheme().colors.mutedText, 16));
        }
    });
    connect(editors_, &QTabWidget::currentChanged, this, [this, workspaceTabs] {
        if (activeDocument_ && editors_->currentWidget() != activeDocument_ &&
            !allowDocumentChange()) {
            const QSignalBlocker blocker(editors_);
            editors_->setCurrentWidget(activeDocument_);
            activeDocument_->setFocus();
            return;
        }
        activeDocument_ = editors_->currentWidget();
        if (auto* object = qobject_cast<ObjectExplorer*>(editors_->currentWidget())) {
            lastObjectTab_ = object;
            object->activateRestoredObject();
        }
        const bool sql = qobject_cast<SqlEditor*>(editors_->currentWidget()) != nullptr;
        if (sql)
            lastSqlDocument_ = qobject_cast<SqlEditor*>(editors_->currentWidget());
        workspaceTabs->workspaceBar()->setHeaderVisible(sql);
        if (sqlResultArea_)
            sqlResultArea_->setVisible(sql);
        if (search_ && !sql)
            search_->hide();
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
    workspaceTabs->workspaceBar()->setHeader(toolbar);
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
    toolbar->addWidget(queryOverflow);
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
    sqlResultArea_ = resultArea;
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
    auto* centralHost = new QWidget(this);
    auto* centralHostLayout = new QVBoxLayout(centralHost);
    centralHostLayout->setContentsMargins(0, 0, 0, 0);
    centralHostLayout->setSpacing(0);
    toast_ = new ToastRegion;
    auto* toast = toast_;
    centralHostLayout->addWidget(screens_, 1);
    setCentralWidget(centralHost);
    toast_->attachTo(centralHost);
    auto* completionNote = new QLabel(tr("Loaded objects"));
    completionNote->setObjectName("completionCatalogNote");
    completionNote->setToolTip(tr("Suggestions use loaded navigator objects. Expand nodes for more "
                                  "names; large catalogs may be limited."));
    completionNote->hide();
    centralHostLayout->insertWidget(0, completionNote);
    connect(completion_, &EditorCompletionController::partialCatalog, completionNote,
            &QWidget::setVisible);
    connect(newQuery, &QAction::triggered, this, [this] { addEditor(); });
    connect(addConnection, &QPushButton::clicked, newConnection, &QAction::trigger);
    connect(editors_, &QTabWidget::tabCloseRequested, this, [this](int index) {
        if (!allowDocumentChange())
            return;
        auto* closing = editors_->widget(index);
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
    auto* objectExplorer = makeObjectExplorer();
    initialObjectExplorer_ = objectExplorer;
    connect(this, &MainWindow::objectContextSelected, this,
            [this, tree](quint64 connection, const QString& object, const QString& label,
                         const QString& kind) {
                openObjectTab(connection, object, label, kind,
                              tree->currentIndex().data(NavigatorModel::PropertiesRole).toList());
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
            [this, navigatorStatus](quint64 token, const QString& error) {
                if (token == profileListToken_) {
                    navigatorStatus->setText(tr("Saved connections: %1").arg(error));
                    navigatorStatus->setToolTip(error);
                }
            });
    connect(workspace_->adapter(), &EngineAdapter::profileConnectFailed, this,
            [this, navigatorStatus, savedConnections, connections](const QString& error) {
                navigatorStatus->setText(error);
                navigatorStatus->setToolTip(error);
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
    connect(refreshSaved, &QAction::triggered, this, refreshProfiles);
    refreshProfiles();
    auto* querySettings = queryMenu->addAction(tr("Query settings…"));
    querySettings->setObjectName("querySettings");
    queryOverflowMenu->addSeparator();
    queryOverflowMenu->addAction(querySettings);
    queryOverflowMenu->addAction(open);
    connect(querySettings, &QAction::triggered, workspace_, &QueryWorkspace::showQuerySettings);
    preferences_->initialize(workspace_->adapter());
    connect(
        workspace_->adapter(), &EngineAdapter::eventReady, this,
        [connections, navigatorStatus](const BridgeEvent& event) {
            const auto kind =
                QString::fromUtf8(event.kind.data(), static_cast<qsizetype>(event.kind.size()));
            if (kind == "connected") {
                navigatorStatus->setText(tr("● Connected"));
                navigatorStatus->setProperty("state", "success");
            } else if (kind == "disconnected") {
                const bool connected = connections->currentData().isValid();
                navigatorStatus->setText(connected ? tr("● Connected") : tr("○ Disconnected"));
                navigatorStatus->setProperty("state", connected ? "success" : "disconnected");
            } else if (kind == "connection_failed") {
                navigatorStatus->setText(tr("! Connection failed"));
                navigatorStatus->setProperty("state", "error");
            }
            navigatorStatus->setAccessibleName(
                tr("Navigator connection status: %1").arg(navigatorStatus->text()));
            navigatorStatus->style()->unpolish(navigatorStatus);
            navigatorStatus->style()->polish(navigatorStatus);
        },
        Qt::DirectConnection);
    if (!storagePath.isEmpty()) {
        recovery_ = new WorkspaceRecoveryController(editors_, [this] { return addEditor(); }, this);
        recovery_->setObjectFactory([this](const SavedWorkspaceTab& tab) -> QWidget* {
            auto* explorer = initialObjectExplorer_;
            initialObjectExplorer_ = nullptr;
            if (!explorer)
                explorer = makeObjectExplorer();
            explorer->setProperty("objectProfileId", tab.profileId);
            explorer->setProperty("objectConnection", QVariant::fromValue<qulonglong>(0));
            explorer->setProperty("objectId", tab.objectId);
            explorer->setProperty("objectType", tab.objectType);
            explorer->setProperty("objectLabel", tab.label);
            explorer->restoreObject(std::nullopt, tab.objectId, tab.label, tab.objectType);
            explorer->selectPane(static_cast<int>(tab.pane));
            return explorer;
        });
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
        centralHostLayout->insertWidget(0, recoveryStatus);
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
        connect(recovery_, &WorkspaceRecoveryController::restoreTabsRequested,
                workspace_->adapter(), &EngineAdapter::restoreWorkspaceTabs);
        connect(recovery_, &WorkspaceRecoveryController::saveTabsRequested, workspace_->adapter(),
                &EngineAdapter::saveWorkspaceTabs);
        connect(workspace_->adapter(), &EngineAdapter::workspaceTabsRestored, recovery_,
                &WorkspaceRecoveryController::restoredTabs);
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
            recovery_, &WorkspaceRecoveryController::restoreCompleted, this, [this](bool hasTabs) {
                screens_->setCurrentIndex(static_cast<int>(hasTabs ? Screen::Sql : Screen::Start));
                if (auto* object = qobject_cast<ObjectExplorer*>(editors_->currentWidget()))
                    object->activateRestoredObject();
            });
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
                    toast->showToast(tr("Warning"), warning, ToastVariant::Warning, 0);
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
            showToast(tr("History text could not be opened."), ToastVariant::Danger);
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
    navigatorController_ = navigatorController;
    connect(navigatorController, &NavigatorController::ddlRequested, this,
            [this](quint64 connection, const QString& objectId, const QString& label,
                   const QString& kind, const QVariantList& properties) {
                openObjectTab(connection, objectId, label, kind, properties, 3);
            });
    connect(navigatorController, &NavigatorController::searchStatusChanged, navigatorStatus,
            [this, navigatorStatus](const QString& status) {
                navigatorStatus->setText(
                    status.isEmpty()
                        ? (browsingConnection_ ? tr("● Connected") : tr("○ Disconnected"))
                        : status);
            });
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
                const auto selectedKind = kind;
                while (object.isValid() && kind != "table" && kind != "view" && kind != "index" &&
                       kind != "sequence" && kind != "function" && kind != "schema" &&
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
                if (kind == "table" || kind == "view" || kind == "index" || kind == "sequence" ||
                    kind == "function") {
                    const auto pane = selectedKind == "column"       ? 0
                                      : selectedKind == "index"      ? 1
                                      : selectedKind.contains("key") ? 2
                                      : selectedKind == "ddl"        ? 3
                                      : selectedKind == "data"       ? 4
                                                                     : -1;
                    openObjectTab(*browsingConnection_,
                                  object.data(NavigatorModel::ObjectIdRole).toString(),
                                  object.data(NavigatorModel::QualifiedNameRole).toString(), kind,
                                  object.data(NavigatorModel::PropertiesRole).toList(), pane);
                }
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
            [toast](const QString& error) {
                toast->showToast(tr("Error"), error, ToastVariant::Danger);
            });
    const auto openGeneratedSql = [this, connections](quint64 connection, const QString& sql) {
        if (databaseClosePending_ || !editors_->isEnabled() ||
            (recovery_ && (!recovery_->isReady() || recovery_->isClosing())))
            return;
        const int target = connections->findData(QVariant::fromValue<qulonglong>(connection));
        if (target < 0) {
            showToast(tr("The selected connection is no longer available."), ToastVariant::Danger);
            return;
        }
        if (target != connections->currentIndex() && !connections->isEnabled()) {
            showToast(tr("Finish the active query before switching connections to generate SQL."),
                      ToastVariant::Warning);
            return;
        }
        const auto bytes = sql.toUtf8();
        if (!sql.isValidUtf16() || bytes.size() > DocumentIo::MaximumBytes) {
            showToast(tr("Generated SQL exceeds editor limits."), ToastVariant::Danger);
            return;
        }
        auto* editor = addEditor();
        if (!editor)
            return;
        if (!editor->restoreDocument(bytes, {}, 0, 0, true)) {
            editors_->removeTab(editors_->indexOf(editor));
            editor->deleteLater();
            showToast(tr("Generated SQL could not be opened."), ToastVariant::Danger);
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
        toast_->showToast(tr("SQL generated"), tr("Review the draft before running."),
                          ToastVariant::Success);
        if (recovery_)
            recovery_->changed();
    };
    connect(navigatorController, &NavigatorController::sqlGenerated, this, openGeneratedSql);
    openGeneratedSql_ = openGeneratedSql;

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
    connect(workspace_, &QueryWorkspace::connectionReady, this, [this](quint64 id) {
        const auto profileId = workspace_->profileIdForConnection(id);
        if (profileId.isEmpty())
            return;
        for (int i = 0; i < editors_->count(); ++i) {
            auto* object = qobject_cast<ObjectExplorer*>(editors_->widget(i));
            if (!object || !object->needsConnection())
                continue;
            const auto context = object->property("objectProfileId").toString();
            if (context != QStringLiteral("profile:%1").arg(profileId))
                continue;
            object->setProperty("objectProfileId", QStringLiteral("profile:%1").arg(profileId));
            object->setProperty("objectConnection", QVariant::fromValue<qulonglong>(id));
            object->restoreObject(id, object->property("objectId").toString(),
                                  object->property("objectLabel").toString(),
                                  object->property("objectType").toString());
            if (editors_->currentWidget() == object)
                object->activateRestoredObject();
        }
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
    showToast(tr("Finish or cancel the active database work before changing workspace "
                 "tabs. Cancel remains in the active tab."),
              ToastVariant::Warning);
    return false;
}
bool MainWindow::showScreen(Screen screen) {
    if (!screens_)
        return false;
    if (screen == Screen::Object) {
        if (lastObjectTab_ && editors_->indexOf(lastObjectTab_) >= 0) {
            if (!allowDocumentChange())
                return false;
            editors_->setCurrentWidget(lastObjectTab_);
            screens_->setCurrentIndex(static_cast<int>(Screen::Sql));
            return true;
        }
        for (int i = 0; i < editors_->count(); ++i) {
            if (qobject_cast<ObjectExplorer*>(editors_->widget(i))) {
                if (!allowDocumentChange())
                    return false;
                editors_->setCurrentIndex(i);
                screens_->setCurrentIndex(static_cast<int>(Screen::Sql));
                return true;
            }
        }
        showToast(tr("Open an object from the navigator to show its tab."), ToastVariant::Warning);
        return false;
    }
    if (screen == Screen::Start && editors_->count()) {
        showToast(tr("Close all workspace tabs to return to Start."), ToastVariant::Warning);
        return false;
    }
    if (screen == Screen::Sql && editors_->count() &&
        !qobject_cast<SqlEditor*>(editors_->currentWidget())) {
        if (lastSqlDocument_ && editors_->indexOf(lastSqlDocument_) >= 0) {
            if (!allowDocumentChange())
                return false;
            editors_->setCurrentWidget(lastSqlDocument_);
            screens_->setCurrentIndex(static_cast<int>(Screen::Sql));
            lastSqlDocument_->setFocus();
            return true;
        }
        bool foundSql = false;
        for (int i = 0; i < editors_->count(); ++i) {
            if (qobject_cast<SqlEditor*>(editors_->widget(i))) {
                foundSql = true;
                if (!allowDocumentChange())
                    return false;
                editors_->setCurrentIndex(i);
                break;
            }
        }
        if (!foundSql) {
            addEditor();
            return true;
        }
    }
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
ObjectExplorer* MainWindow::makeObjectExplorer() {
    auto* explorer = new ObjectExplorer(workspace_->adapter(), this);
    auto* data = new ObjectDataWorkspace(workspace_, explorer);
    explorer->installDataWidget(data);
    connect(explorer, &ObjectExplorer::dataRequested, data, &ObjectDataWorkspace::openObject);
    connect(explorer, &ObjectExplorer::objectChanged, data, &ObjectDataWorkspace::invalidate);
    connect(explorer, &ObjectExplorer::paneChanged, this, [this](int) {
        if (recovery_)
            recovery_->changed();
    });
    connect(data, &ObjectDataWorkspace::busyChanged, explorer, &ObjectExplorer::setOperationBusy);
    connect(explorer, &ObjectExplorer::sqlGenerated, this,
            [this](quint64 connection, const QString& sql) {
                if (openGeneratedSql_)
                    openGeneratedSql_(connection, sql);
            });
    connect(explorer, &ObjectExplorer::reconnectRequested, this, [this, explorer] {
        const auto context = explorer->property("objectProfileId").toString();
        const bool sessionContext = context.startsWith("session:");
        const auto profileId = context.startsWith("profile:") ? context.mid(8) : QString{};
        auto* selector = findChild<QComboBox*>("connectionSelector");
        std::optional<quint64> target;
        if (selector) {
            for (int i = 0; i < selector->count(); ++i) {
                if (!selector->itemData(i).isValid())
                    continue;
                const auto id = selector->itemData(i).toULongLong();
                if (!sessionContext && !profileId.isEmpty() &&
                    workspace_->profileIdForConnection(id) == profileId) {
                    target = id;
                    break;
                }
            }
        }
        if (!target && sessionContext)
            target = browsingConnection_;
        if (target) {
            const auto linkedProfile = workspace_->profileIdForConnection(*target);
            const auto reboundContext = sessionContext
                                            ? linkedProfile.isEmpty()
                                                  ? QStringLiteral("session:%1").arg(*target)
                                                  : QStringLiteral("profile:%1").arg(linkedProfile)
                                            : QStringLiteral("profile:%1").arg(profileId);
            for (int i = 0; i < editors_->count(); ++i) {
                auto* existing = qobject_cast<ObjectExplorer*>(editors_->widget(i));
                if (!existing || existing == explorer ||
                    existing->property("objectProfileId").toString() != reboundContext ||
                    existing->property("objectType") != explorer->property("objectType") ||
                    existing->property("objectId") != explorer->property("objectId"))
                    continue;
                if (!allowDocumentChange())
                    return;
                editors_->setCurrentWidget(existing);
                existing->selectPane(explorer->paneIndex());
                editors_->removeTab(editors_->indexOf(explorer));
                explorer->deleteLater();
                if (recovery_)
                    recovery_->changed();
                showToast(tr("This object is already open; its tab is selected."),
                          ToastVariant::Warning);
                return;
            }
            explorer->setProperty("objectProfileId", reboundContext);
            explorer->setProperty("objectConnection", QVariant::fromValue<qulonglong>(*target));
            explorer->openObject(*target, explorer->property("objectId").toString(),
                                 explorer->property("objectLabel").toString(),
                                 explorer->property("objectType").toString());
            if (recovery_)
                recovery_->changed();
            return;
        }
        if (!sessionContext && !profileId.isEmpty() && reconnectProfile_) {
            if (reconnectProfile_(profileId))
                showToast(tr("Reconnecting the saved connection. Select this tab to load fresh "
                             "metadata."),
                          ToastVariant::Warning);
            else
                showToast(tr("The saved connection is unavailable. Restore it in the sidebar, "
                             "then retry."),
                          ToastVariant::Danger);
        } else {
            showToast(tr("Select a live connection in the sidebar, then choose Reconnect again."),
                      ToastVariant::Warning);
        }
    });
    return explorer;
}
void MainWindow::openObjectTab(quint64 connection, const QString& objectId, const QString& label,
                               const QString& kind, const QVariantList& properties, int pane) {
    if (!allowDocumentChange() || objectId.isEmpty())
        return;
    const auto profileId = workspace_->profileIdForConnection(connection);
    const auto context = profileId.isEmpty() ? QStringLiteral("session:%1").arg(connection)
                                             : QStringLiteral("profile:%1").arg(profileId);
    for (int i = 0; i < editors_->count(); ++i) {
        auto* explorer = qobject_cast<ObjectExplorer*>(editors_->widget(i));
        if (!explorer || explorer->property("objectId").toString() != objectId ||
            explorer->property("objectType").toString() != kind)
            continue;
        const auto existingContext = explorer->property("objectProfileId").toString();
        if (existingContext != context)
            continue;
        explorer->setProperty("objectProfileId", context);
        editors_->setCurrentIndex(i);
        screens_->setCurrentIndex(static_cast<int>(Screen::Sql));
        explorer->setProperty("objectConnection", QVariant::fromValue<qulonglong>(connection));
        explorer->setProperty("objectLabel", label);
        explorer->openObject(connection, objectId, label, kind, properties);
        if (pane >= 0)
            explorer->selectPane(pane);
        if (recovery_)
            recovery_->changed();
        return;
    }
    auto* explorer = initialObjectExplorer_;
    initialObjectExplorer_ = nullptr;
    if (!explorer)
        explorer = makeObjectExplorer();
    explorer->setProperty("objectProfileId", context);
    explorer->setProperty("objectConnection", QVariant::fromValue<qulonglong>(connection));
    explorer->setProperty("objectId", objectId);
    explorer->setProperty("objectType", kind);
    explorer->setProperty("objectLabel", label);
    explorer->openObject(connection, objectId, label, kind, properties);
    if (pane >= 0)
        explorer->selectPane(pane);
    const auto icon =
        design::themedIcon(design::Icon::Table, theme_->resolvedTheme().colors.mutedText, 16);
    const int index = editors_->addTab(explorer, icon, label);
    editors_->setCurrentIndex(index);
    screens_->setCurrentIndex(static_cast<int>(Screen::Sql));
    if (recovery_)
        recovery_->changed();
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
        } else if (const auto* object = qobject_cast<ObjectExplorer*>(editors_->currentWidget())) {
            const auto id = object->property("objectConnection").toULongLong();
            auto* selector = findChild<QComboBox*>("connectionSelector");
            const auto index = selector->findData(QVariant::fromValue<qulonglong>(id));
            if (index >= 0) {
                editor->setConnectionTarget(id, selector->itemText(index));
                editor->setProfileId(workspace_->profileIdForConnection(id));
            }
        } else if (auto* selector = findChild<QComboBox*>("connectionSelector")) {
            auto target = selector->currentData().isValid()
                              ? std::optional<quint64>(selector->currentData().toULongLong())
                              : browsingConnection_;
            if (!target) {
                std::optional<quint64> soleConnection;
                for (int i = 0; i < selector->count(); ++i) {
                    if (!selector->itemData(i).isValid())
                        continue;
                    if (soleConnection) {
                        soleConnection.reset();
                        break;
                    }
                    soleConnection = selector->itemData(i).toULongLong();
                }
                target = soleConnection;
            }
            if (target) {
                const auto index = selector->findData(QVariant::fromValue<qulonglong>(*target));
                if (index >= 0) {
                    editor->setConnectionTarget(*target, selector->itemText(index));
                    editor->setProfileId(workspace_->profileIdForConnection(*target));
                }
            }
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
