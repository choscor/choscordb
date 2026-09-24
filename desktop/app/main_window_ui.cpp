#include "app/main_window.h"

#include "app/editor_preferences.h"
#include "app/main_window_ui.h"
#include "app/main_window_widgets.h"
#include "app/object_explorer.h"
#include "app/query_workspace.h"
#include "design_system/button/button.h"
#include "design_system/confirmation_dialog/confirmation_dialog.h"
#include "design_system/icons.h"
#include "design_system/menu/menu.h"
#include "design_system/navigation_profile_row/navigation_profile_row.h"
#include "design_system/table/table_style.h"
#include "design_system/text/text.h"
#include "design_system/theme_manager.h"
#include "design_system/toast_region/toast_region.h"
#include "widgets/editor_completion/editor_completion.h"
#include "widgets/search_panel/search_panel.h"
#include "widgets/sidebar_section/sidebar_section.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAbstractItemModel>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDesktopServices>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QTabWidget>
#include <QTableView>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#ifdef CHOSCORDB_DEVELOPMENT_PREVIEW
#include "tools/preview/preview_window.h"
#endif

namespace choscordb {
using namespace main_window_detail;

MainWindow::Ui MainWindow::buildUi() {
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
    auto* saveAs = fileMenu->addAction(tr("Save SQL file as…"));
    saveAs->setObjectName("saveSqlAs");
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
#ifdef Q_OS_MACOS
    // Native menu sections reserve an icon column only when an item has an icon.
    // Give Quit the same label inset as Preferences, updates, and Services.
    quit->setIcon(style()->standardIcon(QStyle::SP_DialogCloseButton));
    quit->setIconVisibleInMenu(true);
#endif
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
    viewMenu->setObjectName("viewMenu");
    auto* helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->setObjectName("helpMenu");
    auto* documentation = helpMenu->addAction(tr("ChoscorDB Help"));
    documentation->setObjectName("openDocumentation");
    documentation->setMenuRole(QAction::NoRole);
    connect(documentation, &QAction::triggered, this,
            [] { QDesktopServices::openUrl(QUrl("https://github.com/choscor/choscordb#readme")); });
    auto* about = helpMenu->addAction(tr("About ChoscorDB"));
    about->setObjectName("aboutChoscorDB");
    about->setMenuRole(QAction::AboutRole);
#ifdef Q_OS_MACOS
    about->setIcon(style()->standardIcon(QStyle::SP_MessageBoxInformation));
    about->setIconVisibleInMenu(true);
#endif
    connect(about, &QAction::triggered, this, [this] {
        auto* dialog = findChild<ConfirmationDialog*>("aboutChoscorDBDialog");
        if (!dialog) {
            const auto version = QApplication::applicationVersion();
            const auto text = version.isEmpty() ? tr("ChoscorDB") : tr("ChoscorDB %1").arg(version);
            dialog = new ConfirmationDialog(QMessageBox::Information, tr("About ChoscorDB"), text,
                                            QMessageBox::Ok, this);
            dialog->setObjectName("aboutChoscorDBDialog");
            dialog->setAttribute(Qt::WA_DeleteOnClose);
        }
        dialog->open();
        dialog->raise();
        dialog->activateWindow();
    });
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
    auto* connectionSection = new SidebarSection(tr("Connections"), navBody);
    connectionSection->titleLabel()->setObjectName("navigatorTitle");
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
    navLayout->setContentsMargins(0, initialMetrics.spacingSmall, 0, initialMetrics.spacingMedium);
    navLayout->setSpacing(initialMetrics.spacingSmall);
    auto* sidebarTabs = new QHBoxLayout;
    sidebarTabs->setSpacing(0);
    const int sidebarInset = design::spacing(design::Spacing::OneHalf);
    sidebarTabs->setContentsMargins(sidebarInset, 0, sidebarInset, 0);
    auto* sidebarPanels = new QStackedWidget(navBody);
    sidebarPanels->setObjectName("sidebarPanels");
    for (const auto& tab : {QPair{QStringLiteral("sidebarConnections"), tr("Connections")},
                            QPair{QStringLiteral("sidebarSaved"), tr("Saved queries")},
                            QPair{QStringLiteral("sidebarHistory"), tr("Recent history")}}) {
        auto* button = new design::Button({}, navBody);
        button->setObjectName(tab.first);
        button->setAccessibleName(tab.second);
        button->setToolTip(tab.second);
        button->setCheckable(true);
        button->setButtonContext(design::ButtonContext::SidebarTab);
        button->setVariant(design::ButtonVariant::Ghost);
        button->setButtonSize(design::ButtonSize::IconSmall);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        button->setDesignIcon(tab.first == "sidebarConnections" ? design::Icon::Database
                              : tab.first == "sidebarSaved"     ? design::Icon::File
                                                                : design::Icon::Code);
        button->setMinimumWidth(0);
        button->setMaximumWidth(QWIDGETSIZE_MAX);
        const int index = sidebarTabs->count();
        connect(button, &QPushButton::clicked, sidebarPanels, [sidebarPanels, navBody, index] {
            sidebarPanels->setCurrentIndex(index);
            for (auto* other : navBody->findChildren<QPushButton*>()) {
                if (other->objectName().startsWith("sidebar"))
                    other->setChecked(
                        other->objectName() ==
                        QStringList{"sidebarConnections", "sidebarSaved", "sidebarHistory"}.at(
                            index));
            }
        });
        sidebarTabs->addWidget(button, 1);
    }
    navLayout->addLayout(sidebarTabs);
    auto* connectionsPanel = new QWidget(sidebarPanels);
    auto* connectionsLayout = new QVBoxLayout(connectionsPanel);
    connectionsLayout->setContentsMargins(sidebarInset, 0, sidebarInset, 0);
    connectionsLayout->setSpacing(design::spacing(design::Spacing::Three));
    connectionSection->addAction(refreshNavigator);
    connectionSection->addAction(disconnectNavigator);
    connectionSection->addAction(addConnection);
    connectionsLayout->addWidget(connectionSection);
    auto* savedConnections = new QListWidget(navBody);
    savedConnections->setObjectName("savedConnections");
    savedConnections->setAccessibleName(tr("Saved database connections"));
    savedConnections->setItemDelegate(new design::NavigationProfileDelegate(savedConnections));
    savedConnections->setSpacing(design::spacing(design::Spacing::Half));
    savedConnections->setProperty("designSurface", "sidebar");
    savedConnections->setMouseTracking(true);
    savedConnections->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto* connectionsEmpty = new design::Text(
        tr("No saved connections yet.\n\nUse + to add a database connection."), connectionSection);
    connectionsEmpty->setObjectName("sidebarConnectionsEmpty");
    connectionsEmpty->setWordWrap(true);
    connectionsEmpty->setForegroundRole(QPalette::PlaceholderText);
    connectionsEmpty->setAlignment(Qt::AlignCenter);
    connectionsEmpty->setMargin(design::spacing(design::Spacing::Three));
    connectionsEmpty->setTextFormat(Qt::PlainText);
    connectionSection->contentLayout()->addWidget(connectionsEmpty);
    const auto updateConnectionsEmpty = [savedConnections, connectionsEmpty] {
        connectionsEmpty->setVisible(savedConnections->count() == 0);
    };
    connect(savedConnections->model(), &QAbstractItemModel::rowsInserted, connectionsEmpty,
            updateConnectionsEmpty);
    connect(savedConnections->model(), &QAbstractItemModel::rowsRemoved, connectionsEmpty,
            updateConnectionsEmpty);
    connect(savedConnections->model(), &QAbstractItemModel::modelReset, connectionsEmpty,
            updateConnectionsEmpty);
    connectionSection->contentLayout()->addWidget(savedConnections);
    auto* objectSection = new SidebarSection(tr("Schema & objects"), navBody);
    connectionsLayout->addWidget(objectSection, 1);
    auto* filter = new QLineEdit;
    filter->setPlaceholderText(tr("Filter objects…"));
    filter->setAccessibleName(tr("Filter database objects"));
    objectSection->contentLayout()->addWidget(filter);
    auto* tree = new QTreeView;
    tree->setObjectName("databaseNavigator");
    tree->setProperty("designSurface", "sidebar");
    auto* navigatorIcons = new NavigatorIconDelegate(tree);
    navigatorIcons->connectionIcon = [this](quint64 connection) {
        const auto driver = workspace_ ? workspace_->driverForConnection(connection) : QString{};
        return driver == "sqlite"     ? design::Icon::SQLite
               : driver == "postgres" ? design::Icon::PostgreSQL
               : driver == "mysql"    ? design::Icon::MySQL
                                      : design::Icon::Database;
    };
    tree->setItemDelegate(navigatorIcons);
    tree->setAccessibleName(tr("Database navigator"));
    tree->setHeaderHidden(true);
    auto* objectsEmpty = new design::Text({}, objectSection);
    objectsEmpty->setObjectName("sidebarObjectsEmpty");
    objectsEmpty->setWordWrap(true);
    objectsEmpty->setForegroundRole(QPalette::PlaceholderText);
    objectsEmpty->setAlignment(Qt::AlignCenter);
    objectsEmpty->setMargin(design::spacing(design::Spacing::Three));
    objectsEmpty->setTextFormat(Qt::PlainText);
    objectSection->contentLayout()->addWidget(objectsEmpty);
    objectSection->contentLayout()->addWidget(tree, 3);
    auto* navigatorStatus = new QLabel(tr("Disconnected"), navBody);
    navigatorStatus->setObjectName("navigatorStatus");
    navigatorStatus->setAccessibleName(tr("Navigator connection status: Disconnected"));
    objectSection->contentLayout()->addWidget(navigatorStatus);
    sidebarPanels->addWidget(connectionsPanel);
    auto* savedPanel = new QWidget(sidebarPanels);
    auto* savedLayout = new QVBoxLayout(savedPanel);
    savedLayout->setContentsMargins(sidebarInset, 0, sidebarInset, 0);
    auto* savedSection = new SidebarSection(tr("Saved queries"), savedPanel);
    savedLayout->addWidget(savedSection);
    auto* savedSearch = new QLineEdit(savedSection);
    savedSearch->setObjectName("sidebarSavedSearch");
    savedSearch->setPlaceholderText(tr("Filter saved queries…"));
    savedSearch->setAccessibleName(tr("Filter saved SQL files"));
    savedSection->contentLayout()->addWidget(savedSearch);
    auto* savedStatus = new design::Text({}, savedSection);
    savedStatus->setObjectName("sidebarSavedStatus");
    savedStatus->setWordWrap(true);
    savedStatus->setForegroundRole(QPalette::PlaceholderText);
    savedStatus->setAlignment(Qt::AlignCenter);
    savedStatus->setMargin(design::spacing(design::Spacing::Three));
    savedStatus->setTextFormat(Qt::PlainText);
    savedSection->contentLayout()->addWidget(savedStatus);
    auto* savedFiles = new QTreeWidget(savedSection);
    savedFiles->setObjectName("sidebarSavedFiles");
    savedFiles->setAccessibleName(tr("Saved SQL files"));
    savedFiles->setHeaderHidden(true);
    savedFiles->setItemDelegate(new NavigatorIconDelegate(savedFiles));
    savedFiles->setProperty("designSurface", "sidebar");
    savedSection->contentLayout()->addWidget(savedFiles, 1);
    sidebarPanels->addWidget(savedPanel);
    auto* historyPanel = new QWidget(sidebarPanels);
    auto* historyLayout = new QVBoxLayout(historyPanel);
    historyLayout->setContentsMargins(sidebarInset, 0, sidebarInset, 0);
    auto* historySection = new SidebarSection(tr("Recent history"), historyPanel);
    historyLayout->addWidget(historySection);
    const auto alignSidebarHeadings = [connectionSection, savedSection, historySection,
                                       addConnection] {
        for (auto* section : {connectionSection, savedSection, historySection})
            section->titleLabel()->setMinimumHeight(addConnection->sizeHint().height());
    };
    connect(theme_, &design::ThemeManager::metricsChanged, this, alignSidebarHeadings);
    alignSidebarHeadings();
    auto* historySearch = new QLineEdit(historySection);
    historySearch->setObjectName("sidebarHistorySearch");
    historySearch->setPlaceholderText(tr("Filter recent history…"));
    historySearch->setAccessibleName(tr("Filter recent query history"));
    historySection->contentLayout()->addWidget(historySearch);
    auto* historyStatus = new design::Text({}, historySection);
    historyStatus->setObjectName("sidebarHistoryStatus");
    historyStatus->setWordWrap(true);
    historyStatus->setForegroundRole(QPalette::PlaceholderText);
    historyStatus->setAlignment(Qt::AlignCenter);
    historyStatus->setMargin(design::spacing(design::Spacing::Three));
    historyStatus->setTextFormat(Qt::PlainText);
    historySection->contentLayout()->addWidget(historyStatus);
    auto* historyItems = new QListWidget(historySection);
    historyItems->setObjectName("sidebarHistoryItems");
    historyItems->setAccessibleName(tr("Recent query history"));
    historyItems->setProperty("designSurface", "sidebar");
    historySection->contentLayout()->addWidget(historyItems, 1);
    sidebarPanels->addWidget(historyPanel);
    navLayout->addWidget(sidebarPanels, 1);
    navBody->findChild<QPushButton*>("sidebarConnections")->setChecked(true);
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
    toolbar->setObjectName("queryToolbar");
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto* connections = new QComboBox;
    connections->setObjectName("connectionSelector");
    connections->setMinimumContentsLength(initialMetrics.connectionLabelCharacters);
    connections->setAccessibleName(tr("SQL document connection target"));
    connections->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    connections->addItem(tr("No active connection"));
    connections->setEnabled(false);
    toolbar->addWidget(connections);
    auto* saveButton = new design::Button({}, toolbar);
    saveButton->setDesignIcon(design::Icon::File);
    saveButton->setToolTip(tr("Save"));
    saveButton->setObjectName("saveSqlButton");
    saveButton->setButtonSize(design::ButtonSize::IconSmall);
    saveButton->setButtonContext(design::ButtonContext::EditorAction);
    saveButton->setVariant(design::ButtonVariant::Outline);
    saveButton->setAccessibleName(tr("Save SQL file"));
    saveButton->hide();
    connect(saveButton, &QPushButton::clicked, save, &QAction::trigger);
    connect(save, &QAction::changed, saveButton,
            [save, saveButton] { saveButton->setEnabled(save->isEnabled()); });
    auto* run = queryMenu->addAction(tr("Run statement"));
    run->setObjectName("runStatement");
    run->setShortcut(QKeySequence("Ctrl+Return"));
    run->setEnabled(false);
    auto* runButton = new design::Button({}, toolbar);
    runButton->setDesignIcon(design::Icon::Run);
    runButton->setVariant(design::ButtonVariant::Default);
    runButton->setToolTip(tr("Run"));
    runButton->setObjectName("runStatementButton");
    runButton->setAccessibleName(tr("Run selection or current statement"));
    runButton->setButtonSize(design::ButtonSize::IconSmall);
    runButton->setButtonContext(design::ButtonContext::EditorAction);
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
    auto* cancelButton = new design::Button({}, toolbar);
    cancelButton->setDesignIcon(design::Icon::Cancel);
    cancelButton->setToolTip(tr("Cancel"));
    cancelButton->setObjectName("cancelQueryButton");
    cancelButton->setAccessibleName(tr("Cancel"));
    cancelButton->setButtonSize(design::ButtonSize::IconSmall);
    cancelButton->setButtonContext(design::ButtonContext::EditorAction);
    cancelButton->setVariant(design::ButtonVariant::Outline);
    cancelButton->setEnabled(false);
    cancelButton->hide();
    connect(cancelButton, &QPushButton::clicked, cancel, &QAction::trigger);
    connect(cancel, &QAction::changed, cancelButton, [cancel, cancelButton] {
        cancelButton->setEnabled(cancel->isEnabled());
        cancelButton->setAccessibleName(cancel->text());
        cancelButton->setToolTip(cancel->text());
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
    toolbar->addWidget(mode);
    const auto addToolbarAction = [toolbar](QAction* action, const char* name, design::Icon icon) {
        auto* button = new design::Button({}, toolbar);
        button->setObjectName(name);
        button->setAccessibleName(action->text());
        button->setToolTip(action->text());
        button->setDesignIcon(icon);
        button->setVariant(design::ButtonVariant::Outline);
        button->setButtonSize(design::ButtonSize::IconSmall);
        button->setButtonContext(design::ButtonContext::EditorAction);
        button->setEnabled(action->isEnabled());
        toolbar->addWidget(button);
        connect(button, &QPushButton::clicked, action, &QAction::trigger);
        connect(action, &QAction::changed, button,
                [button, action] { button->setEnabled(action->isEnabled()); });
    };
    addToolbarAction(commitAction, "toolbarCommit", design::Icon::Commit);
    addToolbarAction(rollbackAction, "toolbarRollback", design::Icon::Rollback);
    auto* querySettings = queryMenu->addAction(tr("Query settings…"));
    querySettings->setObjectName("querySettings");
    addToolbarAction(querySettings, "toolbarQuerySettings", design::Icon::Settings);
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
        workspaceTabs->workspaceBar()->setHeaderVisible(
            sql || workspaceTabs->workspaceBar()->property("recoveryActive").toBool());
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
    auto* addSqlTab = new design::Button({}, editors_);
    addSqlTab->setObjectName("addSqlTabButton");
    addSqlTab->setAccessibleName(tr("New SQL query tab"));
    addSqlTab->setToolTip(tr("New SQL query tab"));
    addSqlTab->setDesignIcon(design::Icon::Add);
    addSqlTab->setButtonSize(design::ButtonSize::Icon);
    addSqlTab->setVariant(design::ButtonVariant::Ghost);
    editors_->setCornerWidget(addSqlTab, Qt::TopRightCorner);
    connect(addSqlTab, &QPushButton::clicked, newQuery, &QAction::trigger);
    editors_->tabBar()->setUsesScrollButtons(true);
    editors_->tabBar()->setExpanding(false);
    editors_->tabBar()->setElideMode(Qt::ElideRight);
    editors_->tabBar()->setProperty("designTabVariant", "document");
    new HoveredTabCloseVisibility(editors_->tabBar());
    editors_->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(editors_->tabBar(), &QWidget::customContextMenuRequested, this,
            [this](const QPoint& position) {
                auto* bar = editors_->tabBar();
                const int clicked = bar->tabAt(position);
                if (clicked < 0)
                    return;
                auto* menu = new QMenu(this);
                menu->setObjectName("editorTabContextMenu");
                menu->setAttribute(Qt::WA_DeleteOnClose);
                const auto addCloseAction = [this, menu](const QString& label,
                                                         const QList<QPointer<QWidget>>& targets) {
                    auto* action = menu->addAction(label);
                    action->setEnabled(!targets.isEmpty());
                    connect(action, &QAction::triggered, this, [this, targets] {
                        for (const auto& target : targets) {
                            if (!target)
                                continue;
                            const int index = editors_->indexOf(target);
                            if (index < 0)
                                continue;
                            emit editors_->tabCloseRequested(index);
                            // A rejected close cancels the rest of the batch.
                            if (target && editors_->indexOf(target) >= 0)
                                break;
                        }
                    });
                };
                QList<QPointer<QWidget>> others, all, right;
                for (int index = editors_->count() - 1; index >= 0; --index) {
                    auto* document = editors_->widget(index);
                    all.append(document);
                    if (index != clicked)
                        others.append(document);
                    if (index > clicked)
                        right.append(document);
                }
                addCloseAction(tr("Close"), {editors_->widget(clicked)});
                addCloseAction(tr("Close Others"), others);
                addCloseAction(tr("Close All"), all);
                addCloseAction(tr("Close to the Right"), right);
                design::popupContextMenu(*menu, bar->mapToGlobal(position));
            });
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
    auto* toolbarHost = new QWidget;
    toolbarHost->setObjectName("queryToolbarContainer");
    auto* toolbarLayout = new QVBoxLayout(toolbarHost);
    toolbarLayout->setContentsMargins(initialMetrics.spacingSmall, initialMetrics.spacingSmall,
                                      initialMetrics.spacingSmall, initialMetrics.spacingSmall);
    toolbarLayout->setSpacing(0);
    toolbarLayout->addWidget(toolbar);
    workspaceTabs->workspaceBar()->setHeader(toolbarHost);
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
    grid->horizontalHeader()->setStretchLastSection(false);
    grid->horizontalHeader()->setResizeContentsPrecision(50);
    grid->verticalHeader()->setDefaultSectionSize(initialMetrics.sqlResultRowHeight);
    grid->horizontalHeader()->setFixedHeight(initialMetrics.sqlResultHeaderHeight);
    resultLayout->addWidget(grid, 1);
    auto* resultFooter = new QWidget;
    resultFooter->setObjectName("sqlResultFooter");
    auto* pager = new QHBoxLayout(resultFooter);
    pager->setContentsMargins(initialMetrics.spacingMedium, initialMetrics.spacingSmall,
                              initialMetrics.spacingMedium, initialMetrics.spacingSmall);
    pager->addWidget(empty);
    auto* compactState = new design::Text(tr("Disconnected"), resultFooter);
    compactState->setObjectName("executionStateCompact");
    compactState->setTypographyRole(design::TypographyRole::Small);
    pager->addWidget(compactState);
    pager->addStretch(1);
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
    auto* exportResult = new design::Button({});
    exportResult->setButtonSize(design::ButtonSize::IconSmall);
    exportResult->setVariant(design::ButtonVariant::Outline);
    exportResult->setAccessibleName(tr("Export"));
    exportResult->setToolTip(tr("Export"));
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
    const auto addGridAction = [toolbar](const QString& label, const char* name, design::Icon icon,
                                         bool inToolbar = true) {
        auto* button = new design::Button({}, toolbar);
        button->setAccessibleName(label);
        button->setToolTip(label);
        button->setDesignIcon(icon);
        button->setObjectName(name);
        button->setButtonSize(design::ButtonSize::IconSmall);
        button->setVariant(design::ButtonVariant::Outline);
        button->setEnabled(false);
        if (inToolbar)
            toolbar->addWidget(button);
        else
            button->hide();
        return button;
    };
    // Keep the shared edit controls as command/state bindings for the table menu.
    auto* addResultRow =
        addGridAction(tr("Add row"), "queryResultAddRow", design::Icon::Add, false);
    auto* deleteResultRows =
        addGridAction(tr("Delete rows"), "queryResultDeleteRows", design::Icon::Close, false);
    auto* restoreResultRows =
        addGridAction(tr("Restore rows"), "queryResultRestoreRows", design::Icon::Refresh, false);
    auto* nullResultCell =
        addGridAction(tr("Set NULL"), "queryResultSetNull", design::Icon::Square, false);
    auto* discardResultEdits =
        addGridAction(tr("Discard"), "queryResultDiscardEdits", design::Icon::Cancel, false);
    auto* applyResultEdits =
        addGridAction(tr("Apply"), "queryResultApplyEdits", design::Icon::Check, false);
    toolbar->addWidget(exportResult);
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
    preferencesAction->setMenuRole(QAction::PreferencesRole);
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
    auto* startStatus = new design::Text(tr("PostgreSQL · MySQL · SQLite"), startFooter);
    startStatus->setTypographyRole(design::TypographyRole::Small);
    startStatus->setForegroundRole(QPalette::PlaceholderText);
    startActions->addWidget(startStatus);
    startActions->addStretch();
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
    return {
        .fileMenu = fileMenu,
        .newQuery = newQuery,
        .open = open,
        .save = save,
        .saveAs = saveAs,
        .quit = quit,
        .editMenu = editMenu,
        .queryMenu = queryMenu,
        .newConnection = newConnection,
        .viewMenu = viewMenu,
        .navigator = navigator,
        .addConnection = addConnection,
        .refreshNavigator = refreshNavigator,
        .disconnectNavigator = disconnectNavigator,
        .sidebarPanels = sidebarPanels,
        .savedConnections = savedConnections,
        .filter = filter,
        .tree = tree,
        .objectsEmpty = objectsEmpty,
        .navigatorStatus = navigatorStatus,
        .savedSearch = savedSearch,
        .savedStatus = savedStatus,
        .savedFiles = savedFiles,
        .historySearch = historySearch,
        .historyStatus = historyStatus,
        .historyItems = historyItems,
        .resetLayout = resetLayout,
        .toolbar = toolbar,
        .connections = connections,
        .run = run,
        .cancel = cancel,
        .cancelButton = cancelButton,
        .mode = mode,
        .commitAction = commitAction,
        .rollbackAction = rollbackAction,
        .querySettings = querySettings,
        .splitter = splitter,
        .workspaceTabs = workspaceTabs,
        .toolbarHost = toolbarHost,
        .results = results,
        .empty = empty,
        .grid = grid,
        .compactState = compactState,
        .previousPage = previousPage,
        .nextPage = nextPage,
        .exportResult = exportResult,
        .addResultRow = addResultRow,
        .deleteResultRows = deleteResultRows,
        .restoreResultRows = restoreResultRows,
        .nullResultCell = nullResultCell,
        .discardResultEdits = discardResultEdits,
        .applyResultEdits = applyResultEdits,
        .messages = messages,
        .searchActions = searchActions,
        .toast = toast,
    };
}
} // namespace choscordb
