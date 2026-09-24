#include "modern_ui_test.h"
#include "app/appearance_controller.h"
#include "app/main_window.h"
#include "app/object_explorer.h"
#include "app/query_workspace.h"
#include "app/workspace_recovery.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/button/button.h"
#include "design_system/dialog_presentation/dialog_presentation.h"
#include "design_system/menu/menu.h"
#include "design_system/theme_manager.h"
#include "design_system/toast_region/toast_region.h"
#include "models/navigator_model.h"
#include "tools/preview/preview_window.h"
#include "widgets/profile_dialog/profile_dialog.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFontInfo>
#include <QHeaderView>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QSysInfo>
#include <QTabBar>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>
#include <Qsci/qscilexersql.h>
#include <Qsci/qsciscintilla.h>

void ModernUiTest::applicationMenusExposeHelpAndAbout() {
    choscordb::MainWindow window;
    window.show();
    auto* help = window.findChild<QMenu*>("helpMenu");
    QVERIFY(help);
    QVERIFY(window.menuBar()->actions().contains(help->menuAction()));
    auto* about = window.findChild<QAction*>("aboutChoscorDB");
    QVERIFY(about);
    QCOMPARE(about->menuRole(), QAction::AboutRole);
    about->trigger();
    auto* dialog = window.findChild<QMessageBox*>("aboutChoscorDBDialog");
    QVERIFY(dialog);
    QVERIFY(dialog->isVisible());
    QVERIFY(dialog->text().contains("ChoscorDB"));
    dialog->close();
    QVERIFY(help->findChild<QAction*>("openDocumentation"));
#ifdef Q_OS_MACOS
    QVERIFY(!about->icon().isNull());
    QVERIFY(about->isIconVisibleInMenu());
    auto* view = window.findChild<QMenu*>("viewMenu");
    QVERIFY(view);
    QVERIFY(view->actions().isEmpty());
    auto* tabs = window.findChild<QAction*>("listEditorTabs");
    QVERIFY(tabs);
    QVERIFY(window.actions().contains(tabs));
#endif
}

void ModernUiTest::resultActionsUseIconsInToolbarAndFootersOnlyContainPagination() {
    choscordb::MainWindow window;
    auto* toolbar = window.findChild<QToolBar*>("queryToolbar");
    auto* footer = window.findChild<QWidget*>("sqlResultFooter");
    QVERIFY(toolbar && footer);

    const auto buttons = footer->findChildren<QPushButton*>();
    QCOMPARE(buttons.size(), 2);
    QVERIFY(footer->isAncestorOf(window.findChild<QPushButton*>("previousPage")));
    QVERIFY(footer->isAncestorOf(window.findChild<QPushButton*>("nextPage")));
    for (const char* name : {"queryResultAddRow", "queryResultDeleteRows", "queryResultRestoreRows",
                             "queryResultSetNull", "queryResultDiscardEdits",
                             "queryResultApplyEdits", "exportResult"}) {
        auto* button = window.findChild<QPushButton*>(name);
        QVERIFY(button);
        QVERIFY(toolbar->isAncestorOf(button));
        QVERIFY(button->text().isEmpty());
        QVERIFY(!button->icon().isNull());
        QVERIFY(!button->accessibleName().isEmpty());
        QVERIFY(!button->toolTip().isEmpty());
    }
    for (auto* action : toolbar->actions()) {
        auto* widget = toolbar->widgetForAction(action);
        QVERIFY(widget != window.findChild<QPushButton*>("queryResultSetNull"));
        QVERIFY(!widget || widget->sizePolicy().horizontalPolicy() != QSizePolicy::Expanding);
    }
    auto* startFooter = window.findChild<QWidget*>("startFooter");
    QVERIFY(startFooter->findChildren<QPushButton*>().isEmpty());
    QVERIFY(!window.findChild<QWidget*>("startToolbar"));
    QVERIFY(!window.findChild<QPushButton*>("startNewConnection"));
}

void ModernUiTest::recoveryKeepsUnavailableToolbarActionsDisabled() {
    QTemporaryDir storage;
    choscordb::MainWindow window(nullptr, storage.filePath("workspace.sqlite"));
    window.show();
    auto* recovery = window.findChild<choscordb::WorkspaceRecoveryController*>();
    QTRY_VERIFY(recovery->isReady());
    window.findChild<QAction*>("newQuery")->trigger();
    auto* save = window.findChild<QPushButton*>("saveSqlButton");
    QVERIFY(save->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(recovery, "errorOccurred", Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("Save failed")),
                                      Q_ARG(bool, true)));
    QVERIFY(!save->isEnabled());
    window.findChild<QAction*>("cancelRecoveryClose")->trigger();
    QVERIFY(save->isEnabled());
    for (const char* name : {"runStatementButton", "cancelQueryButton", "queryResultAddRow",
                             "queryResultDeleteRows", "queryResultApplyEdits", "exportResult"}) {
        auto* button = window.findChild<QPushButton*>(name);
        if (button->objectName() == "queryResultAddRow" ||
            button->objectName() == "queryResultDeleteRows" ||
            button->objectName() == "cancelQueryButton" ||
            button->objectName() == "queryResultApplyEdits")
            QVERIFY(button->isHidden());
        else
            QVERIFY(button->isVisible());
        QVERIFY2(!button->isEnabled(), name);
    }
}

void ModernUiTest::startUsesPanelAndMutedSupportingText() {
    choscordb::MainWindow window;
    window.show();
    auto* appearance = window.findChild<choscordb::AppearanceController*>();
    QTRY_VERIFY(appearance->isReady());
    auto* start = window.findChild<QWidget*>("startScreen");
    QVERIFY(start);
    for (const auto& mode : {QString("light"), QString("dark")}) {
        QVERIFY(appearance->preview(mode));
        QCoreApplication::processEvents();
        const auto pixels = start->grab();
        QCOMPARE(
            pixels.toImage().pixelColor(QPoint(start->width() / 2, 60) * pixels.devicePixelRatio()),
            QColor(mode == "light" ? "#ffffff" : "#20272b"));
        auto* hint = start->findChild<QLabel*>("startHint");
        QVERIFY(hint);
        QCOMPARE(hint->foregroundRole(), QPalette::PlaceholderText);
    }
}

void ModernUiTest::freshSidebarUsesResponsiveReferenceWidthsAndSeamlessSections() {
    choscordb::MainWindow window;
    window.show();
    QTRY_VERIFY(window.findChild<choscordb::AppearanceController*>()->isReady());
    window.findChild<choscordb::design::ThemeManager*>()->setMode(
        choscordb::design::ThemeMode::Light);
    auto* sidebar = window.findChild<QDockWidget*>("navigator");
    QTRY_COMPARE(sidebar->width(), 260);
    window.resize(960, 640);
    QTRY_COMPARE(sidebar->width(), 235);
    window.resize(1280, 900);
    QTRY_COMPARE(sidebar->width(), 260);
    auto* title = window.findChild<QLabel*>("navigatorTitle");
    QVERIFY(!window.findChild<QStatusBar*>());
    auto* navBody = sidebar->widget();
    QVERIFY(navBody);
    QCOMPARE(navBody->objectName(), QString("navigatorBody"));
    QVERIFY(qApp->styleSheet().contains("QWidget#navigatorBody"));
    QVERIFY(qApp->styleSheet().contains("qlineargradient"));
    QVERIFY(!qApp->styleSheet().contains("@sidebarGlassTop"));
    QCOMPARE(navBody->layout()->contentsMargins().top(),
             choscordb::design::spacing(choscordb::design::Spacing::One));
    QCOMPARE(title->text(), QString("CONNECTIONS"));
    QCOMPARE(title->font().pixelSize(), 10);
    // Qt stores font tracking in 1/64px units.
    QVERIFY(qAbs(title->font().letterSpacing() - 1.3) < 1.0 / 64);
    auto* tree = window.findChild<QTreeView*>("databaseNavigator");
    const auto pixels = tree->viewport()->grab().toImage();
    const auto navigatorPixel = pixels.pixelColor(QPoint(4, 4) * pixels.devicePixelRatio());
    QCOMPARE(navigatorPixel.alpha(), 0);
    const auto composedSidebar = navBody->grab().toImage();
    const auto treePoint = tree->mapTo(navBody, QPoint(4, 4)) * composedSidebar.devicePixelRatio();
    QVERIFY(composedSidebar.pixelColor(treePoint).lightness() > 220);
    auto* start = window.findChild<QWidget*>("startScreen");
    auto* footer = window.findChild<QWidget*>("startFooter");
    QVERIFY(footer);
    QCOMPARE(footer->mapTo(start, QPoint()).x(), 0);
    QCOMPARE(footer->width(), start->width());
    QCOMPARE(footer->mapTo(start, footer->rect().bottomLeft()).y(), start->rect().bottom());
}

void ModernUiTest::savedProfileBadgesDistinguishDriversInBothThemes() {
    choscordb::MainWindow window;
    window.show();
    QTRY_VERIFY(window.findChild<choscordb::AppearanceController*>()->isReady());
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    choscordb::SavedProfile sqlite;
    sqlite.id = "badge-sqlite";
    sqlite.name = "SQLite badge";
    sqlite.path = ":memory:";
    auto postgres = sqlite;
    postgres.id = "badge-postgres";
    postgres.name = "PostgreSQL badge";
    postgres.driver = "postgres";
    postgres.host = "localhost";
    postgres.database = "analytics";
    postgres.user = "analyst";
    workspace->adapter()->saveProfile(sqlite, 881);
    workspace->adapter()->saveProfile(postgres, 882);
    auto* profiles = window.findChild<QListWidget*>("savedConnections");
    QTRY_COMPARE(profiles->count(), 2);
    auto* title = window.findChild<QLabel*>("navigatorTitle");
    const auto* navLayout = qobject_cast<QVBoxLayout*>(title->parentWidget()->layout());
    QVERIFY(navLayout);
    QCOMPARE(navLayout->spacing(), choscordb::design::spacing(choscordb::design::Spacing::One));
    QTRY_COMPARE(profiles->geometry().top() - navLayout->itemAt(0)->geometry().bottom() - 1,
                 navLayout->spacing());
    QTRY_VERIFY(!profiles->verticalScrollBar()->isVisible());
    QCOMPARE(profiles->spacing(), choscordb::design::spacing(choscordb::design::Spacing::Half));
    QCOMPARE(profiles->visualItemRect(profiles->item(1)).top() -
                 profiles->visualItemRect(profiles->item(0)).bottom() - 1,
             profiles->spacing() * 2);
    auto* theme = window.findChild<choscordb::design::ThemeManager*>();
    for (const auto mode :
         {choscordb::design::ThemeMode::Light, choscordb::design::ThemeMode::Dark}) {
        theme->setMode(mode);
        QCoreApplication::processEvents();
        QTRY_VERIFY(profiles->viewport()->rect().contains(
            profiles->visualItemRect(profiles->item(profiles->count() - 1))));
        const auto pixels = profiles->viewport()->grab().toImage();

        for (int row = 0; row < profiles->count(); ++row) {
            auto* item = profiles->item(row);
            const auto driver = item->data(Qt::UserRole).value<choscordb::SavedProfile>().driver;
            // Sample the badge padding, outside the downloaded logo artwork.
            const auto point = profiles->visualItemRect(item).topLeft() + QPoint(11, 22);
            QCOMPARE(pixels.pixelColor(point * pixels.devicePixelRatio()),
                     QColor(driver == "sqlite" ? "#f6f0e6" : "#edf3f9"));
        }
    }
}

void ModernUiTest::completedResultsKeepContentWidthsAndDisableCancel() {
    choscordb::MainWindow window;
    window.show();
    QTRY_VERIFY(window.findChild<choscordb::AppearanceController*>()->isReady());
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    workspace->connectSqlite(":memory:");
    QTRY_VERIFY(window.findChild<QAction*>("newQuery")->isEnabled());
    window.findChild<QAction*>("newQuery")->trigger();
    auto* run = window.findChild<QAction*>("runStatement");
    QTRY_VERIFY(run->isEnabled());
    auto* editor = qobject_cast<choscordb::SqlEditor*>(
        window.findChild<QTabWidget*>("editorTabs")->currentWidget());
    editor->setText("CREATE TABLE previous_result(value INTEGER)");
    run->trigger();
    QTRY_COMPARE(window.findChild<QLabel*>("executionSummary")->property("state").toString(),
                 QString("completed"));
    editor->setText(
        "SELECT 1 AS id, 'Olivia Rhye' AS name, 'olivia@acme.test' AS email, "
        "'Pro' AS plan, 'active' AS status, '2026-09-12' AS created_at, 128 AS total_spent");
    run->trigger();
    auto* grid = window.findChild<QTableView*>("queryResults");
    QTRY_COMPARE(grid->model()->rowCount(), 1);
    QTRY_COMPARE(window.findChild<QLabel*>("executionSummary")->property("state").toString(),
                 QString("completed"));
    QCoreApplication::processEvents();
    QVERIFY(window.findChild<QPushButton*>("cancelQueryButton")->isHidden());
    QVERIFY(!window.findChild<QPushButton*>("cancelQueryButton")->isEnabled());
    auto* header = grid->horizontalHeader();
    QVERIFY(!header->stretchLastSection());
    QList<int> widths;
    for (int column = 0; column < 7; ++column) {
        const auto width = header->sectionSize(column);
        QVERIFY(width >= 80 && width <= 400);
        widths.append(width);
    }
    const int previousViewportWidth = grid->viewport()->width();
    window.resize(window.width() + 200, window.height());
    QTRY_VERIFY(grid->viewport()->width() > previousViewportWidth);
    for (int column = 0; column < 7; ++column)
        QCOMPARE(header->sectionSize(column), widths.at(column));
}

void ModernUiTest::findFromHistoryReturnsToExistingSqlAndDoesNotCreateClosedDocuments() {
    choscordb::MainWindow window;
    window.show();
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    QTRY_VERIFY(window.findChild<QAction*>("newQuery")->isEnabled());
    window.findChild<QAction*>("newQuery")->trigger();
    auto* editor = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
    editor->setText("SELECT customer_name FROM customers");
    QVERIFY(window.showScreen(choscordb::MainWindow::Screen::History));
    auto* find = window.findChild<QAction*>("command_find");
    QVERIFY(find);
    find->trigger();
    QCOMPARE(window.findChild<QStackedWidget*>("centralScreens")->currentWidget()->objectName(),
             QString("sqlScreen"));
    QVERIFY(window.findChild<QWidget*>("searchPanel")->isVisible());
    QCOMPARE(tabs->currentWidget(), editor);
    editor->setModified(false);
    while (tabs->count())
        tabs->tabCloseRequested(0);
    find->trigger();
    QCOMPARE(tabs->count(), 0);
    QCOMPARE(window.findChild<QStackedWidget*>("centralScreens")->currentWidget()->objectName(),
             QString("startScreen"));
}
// Opt-in native screenshot fixture. All displayed values come from this isolated
// SQLite database; the application itself contains no screenshot fixtures.

void ModernUiTest::captureScreenFixtures() {
    const auto output = qEnvironmentVariable("CHOSCORDB_SCREEN_CAPTURE_DIR");
    if (output.isEmpty())
        QSKIP("Set CHOSCORDB_SCREEN_CAPTURE_DIR to capture deterministic screens.");
    QVERIFY(QDir().mkpath(output));
    QJsonArray captures;
    for (const auto& mode : {QString("light"), QString("dark")}) {
        for (const auto& size : {QSize(1280, 900), QSize(960, 640)}) {
            QTemporaryDir directory;
            QVERIFY(directory.isValid());
            choscordb::MainWindow window(nullptr, directory.filePath("settings.sqlite"));
            window.show();
            auto* appearance = window.findChild<choscordb::AppearanceController*>();
            QTRY_VERIFY(appearance->isReady());
            QVERIFY(appearance->preview(mode));
            QSignalSpy appearanceSaved(appearance, &choscordb::AppearanceController::saveFinished);
            appearance->applyPreview();
            QTRY_COMPARE(appearanceSaved.count(), 1);
            QVERIFY(appearanceSaved.first().first().toBool());
            window.resize(size);
            QTRY_COMPARE(window.size(), size);
            auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
            auto* profiles = window.findChild<QListWidget*>("savedConnections");
            choscordb::SavedProfile profile;
            profile.id = "visual-sqlite";
            profile.name = "Commerce · local";
            profile.path = directory.filePath("commerce.sqlite");
            workspace->adapter()->saveProfile(profile, 701);
            auto analytics = profile;
            analytics.id = "visual-analytics";
            analytics.name = "Analytics · staging";
            analytics.path = directory.filePath("analytics.sqlite");
            workspace->adapter()->saveProfile(analytics, 702);
            auto sandbox = profile;
            sandbox.id = "visual-sandbox";
            sandbox.name = "Sandbox";
            sandbox.path = ":memory:";
            workspace->adapter()->saveProfile(sandbox, 703);
            QTRY_COMPARE(profiles->count(), 3);
            const auto capture = [&](const QString& state, bool settle = true) {
                if (settle) {
                    QCoreApplication::processEvents();
                    // Let native layout settle after separately asserted data readiness.
                    QTest::qWait(60);
                }
                if (state == "sql-results") {
                    auto* result = window.findChild<QTableView*>("queryResults");
                    const auto pixels = result->viewport()->grab().toImage();
                    // Sample below the text baseline: at narrow widths the last
                    // numeric value legitimately reaches the right edge.
                    const int y = result->visualRect(result->model()->index(1, 0)).bottom() - 4;
                    QCOMPARE(pixels.pixelColor(QPoint(result->viewport()->width() - 3, y) *
                                               pixels.devicePixelRatio()),
                             QColor(mode == "light" ? "#f2f2f2" : "#303030"));
                }
                const auto name = QString("%1-%2x%3-%4.png")
                                      .arg(mode)
                                      .arg(size.width())
                                      .arg(size.height())
                                      .arg(state);
                const auto pixmap = window.grab();
                QVERIFY(pixmap.save(QDir(output).filePath(name)));
                if (auto* dialog = qobject_cast<QDialog*>(
                        choscordb::design::DialogPresentation::activeDialog())) {
                    const auto dialogName = name.chopped(4) + "-dialog.png";
                    const auto dialogPixmap = dialog->grab();
                    QVERIFY(dialogPixmap.save(QDir(output).filePath(dialogName)));
                    const auto offset =
                        dialog->mapToGlobal(QPoint()) - window.mapToGlobal(QPoint());
                    captures.append(QJsonObject{
                        {"file", dialogName},
                        {"owner", name},
                        {"state", state},
                        {"theme", mode},
                        {"dialog", true},
                        {"ownerRelativeX", offset.x()},
                        {"ownerRelativeY", offset.y()},
                        {"clientWidth", dialog->width()},
                        {"clientHeight", dialog->height()},
                        {"devicePixelRatio", dialogPixmap.devicePixelRatio()},
                        {"activeModal",
                         choscordb::design::DialogPresentation::activeDialog(&window) == dialog}});
                }
                auto* activeEditor = qobject_cast<choscordb::SqlEditor*>(
                    window.findChild<QTabWidget*>("editorTabs")->currentWidget());
                const QFontInfo editorFont(activeEditor->lexer()->font(QsciLexerSQL::Default));
                captures.append(QJsonObject{
                    {"file", name},
                    {"editorFontFamily", editorFont.family()},
                    {"editorFontPixelSize", editorFont.pixelSize()},
                    {"editorLineHeight", static_cast<int>(activeEditor->SendScintilla(
                                             QsciScintilla::SCI_TEXTHEIGHT, 0UL))},
                    {"theme", mode},
                    {"state", state},
                    {"clientWidth", window.width()},
                    {"clientHeight", window.height()},
                    {"devicePixelRatio", pixmap.devicePixelRatio()},
                    {"sidebarWidth", window.findChild<QDockWidget*>("navigator")->width()},
                    {"uiFontFamily", QFontInfo(window.font()).family()},
                    {"uiFontPixelSize", QFontInfo(window.font()).pixelSize()},
                    {"logicalDpiY", window.logicalDpiY()},
                    {"platform", QGuiApplication::platformName()}});
            };
            capture("start");
            QSignalSpy connected(workspace, &choscordb::QueryWorkspace::connectionReady);
            QListWidgetItem* commerce = nullptr;
            for (int row = 0; row < profiles->count(); ++row)
                if (profiles->item(row)->data(Qt::UserRole).value<choscordb::SavedProfile>().id ==
                    profile.id)
                    commerce = profiles->item(row);
            QVERIFY(commerce);
            QTest::mouseClick(profiles->viewport(), Qt::LeftButton, Qt::NoModifier,
                              profiles->visualItemRect(commerce).center());
            QTRY_COMPARE(connected.count(), 1);
            const auto connection = connected.at(0).at(0).toULongLong();
            window.openConnectionQuery(connection);
            auto* editor = qobject_cast<choscordb::SqlEditor*>(
                window.findChild<QTabWidget*>("editorTabs")->currentWidget());
            auto* run = window.findChild<QAction*>("runStatement");
            auto* summary = window.findChild<QLabel*>("executionSummary");
            editor->setText(
                "CREATE TABLE customers (id INTEGER PRIMARY KEY, name TEXT NOT NULL, "
                "email TEXT UNIQUE, plan TEXT NOT NULL, status TEXT NOT NULL DEFAULT 'active', "
                "created_at DATE, total_spent NUMERIC, metadata TEXT DEFAULT '{}', avatar "
                "BLOB)");
            run->trigger();
            QTRY_COMPARE(summary->property("state").toString(), QString("completed"));
            // Literal customer fixture from docs/mvp-design/prototype.js.
            // SQLite's real affinity/types remain visible instead of pretending PostgreSQL.
            editor->setText(
                "INSERT INTO customers(id,name,email,plan,status,created_at,total_spent) "
                "VALUES "
                "(1,'Olivia Rhye','olivia@acme.test','Pro','active','2026-09-12',128.00),"
                "(2,'Phoenix Baker','phoenix@acme.test','Team','active','2026-09-12',249.00),"
                "(3,'Lana Steiner','lana@acme.test','Pro','active','2026-09-11',128.00),"
                "(4,'Demi Wilkinson','demi@acme.test','Starter','trialing','2026-09-11',0.00),"
                "(5,'Drew Cano','drew@acme.test','Pro','active','2026-09-10',128.00),"
                "(6,'Natali Craig','natali@acme.test','Team','active','2026-09-10',249.00),"
                "(7,'Orlando "
                "Diggs','orlando@acme.test','Starter','inactive','2026-09-09',NULL),"
                "(8,'Andi Lane','andi@acme.test','Pro','active','2026-09-09',128.00)");
            run->trigger();
            QTRY_COMPARE(summary->property("state").toString(), QString("completed"));
            editor->setText("-- A closer look at our newest customers\n"
                            "SELECT id, name, email, plan, status,\n"
                            "       created_at, total_spent\nFROM main.customers\n"
                            "WHERE created_at >= '2026-09-01'\n"
                            "ORDER BY created_at DESC, id\nLIMIT 1000;");
            capture("sql-ready");
            run->trigger();
            auto* grid = window.findChild<QTableView*>("queryResults");
            QTRY_COMPARE(grid->model()->rowCount(), 8);
            QTRY_COMPARE(summary->property("state").toString(), QString("completed"));
            auto* tree = window.findChild<QTreeView*>("databaseNavigator");
            auto* proxy = qobject_cast<QSortFilterProxyModel*>(tree->model());
            auto* navigator = window.findChild<choscordb::NavigatorModel*>();
            QVERIFY(proxy && navigator);
            const auto root = navigator->index(0, 0);
            navigator->fetchMore(root);
            QTRY_VERIFY(root.data(choscordb::NavigatorModel::ChildrenLoadedRole).toBool());
            const auto schema = navigator->index(0, 0, root);
            navigator->fetchMore(schema);
            QTRY_VERIFY(schema.data(choscordb::NavigatorModel::ChildrenLoadedRole).toBool());
            tree->expand(proxy->mapFromSource(root));
            tree->expand(proxy->mapFromSource(schema));
            capture("sql-results");
            auto* explorer = window.findChild<choscordb::ObjectExplorer*>();
            QVERIFY(explorer);
            explorer->openObject(connection, R"(["main","customers"])", "main.customers");
            QVERIFY(window.showScreen(choscordb::MainWindow::Screen::Object));
            auto* metadata = explorer->findChild<QTableView*>("objectMetadata");
            QTRY_COMPARE(metadata->model()->rowCount(), 9);
            capture("object-columns");
            auto* panes = explorer->findChild<QTabBar*>("objectTabs");
            auto* objectStatus = explorer->findChild<QLabel*>("objectStatus");
            panes->setCurrentIndex(1);
            QTRY_COMPARE(objectStatus->property("state").toString(), QString("loaded"));
            capture("object-indexes");
            panes->setCurrentIndex(2);
            QTRY_COMPARE(objectStatus->property("state").toString(), QString("loaded"));
            capture("object-keys");
            panes->setCurrentIndex(3);
            QTRY_VERIFY(explorer->findChild<QPlainTextEdit*>("objectDdl")
                            ->toPlainText()
                            .contains("CREATE TABLE"));
            capture("object-ddl");
            panes->setCurrentIndex(4);
            QTRY_COMPARE(explorer->findChild<QTableView*>("objectDataResults")->model()->rowCount(),
                         8);
            QTRY_VERIFY(window.findChild<QPushButton*>("objectDataExport")->isEnabled());
            capture("object-data");
            QVERIFY(window.showScreen(choscordb::MainWindow::Screen::History));
            QTRY_VERIFY(window.findChild<QListWidget*>("sidebarHistoryItems")->count() >= 3);
            capture("history");
            window.findChild<QAction*>("preferences")->trigger();
            auto* preferences = window.findChild<QDialog*>("preferencesDialog");
            QVERIFY(preferences);
            QTRY_VERIFY(preferences->findChild<QPushButton*>("preferencesApply")->isEnabled());
            auto* sections = preferences->findChild<QTabWidget*>("preferencesSections");
            for (int section = 0; section < sections->count(); ++section) {
                sections->setCurrentIndex(section);
                capture(QString("preferences-%1").arg(section));
            }
            preferences->reject();
            QVERIFY(window.showScreen(choscordb::MainWindow::Screen::Sql));
            window.findChild<QPushButton*>("exportResult")->click();
            auto* exportDialog = window.findChild<QDialog*>("exportDialog");
            QVERIFY(exportDialog);
            capture("export");
            exportDialog->reject();
            workspace->showProfiles();
            auto* profileDialog = window.findChild<choscordb::ProfileDialog*>();
            QVERIFY(profileDialog);
            QTRY_VERIFY(profileDialog->isVisible());
            capture("connection");
            profileDialog->reject();
            editor->setText("SELECT missing_column FROM customers");
            run->trigger();
            QTRY_COMPARE(summary->property("state").toString(), QString("failed"));
            capture("sql-error");
            editor->setText("WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n "
                            "WHERE x<100000000) SELECT sum(x) FROM n");
            run->trigger();
            window.findChild<QPushButton*>("cancelQueryButton")->click();
            QCOMPARE(summary->property("state").toString(), QString("cancelling"));
            // Capture the actual local pending state before dispatching the
            // asynchronously acknowledged terminal event; never synthesize it.
            capture("sql-cancelling", false);
            QTRY_VERIFY(workspace->navigationAllowed());
            editor->setModified(false);
        }
    }
    QFile manifest(QDir(output).filePath("manifest.json"));
    QVERIFY(manifest.open(QIODevice::WriteOnly));
    const QJsonObject metadata{
        {"qtVersion", QString::fromLatin1(qVersion())},
        {"os", QSysInfo::prettyProductName()},
        {"platform", QGuiApplication::platformName()},
        {"captures", captures},
        {"mapping",
         "Native captures exclude OS title/menu chrome. Original browser 1280x900 and 960x640 "
         "captures retain their 57px mock chrome. The separate client-aligned reference uses "
         "viewport heights +57 (1280x957 and 960x697); crop its top57px to compare native "
         "client rectangles at1:1 logical pixels without scaling. Raw approved-size captures "
         "remain available to audit the native minimum client height difference."},
        {"fixture", "Isolated real SQLite customers table, 8 prototype rows, 7 query columns "
                    "and 9 metadata columns; three saved SQLite profiles. "
                    "SQLite affinity/types and real elapsed times differ from the PostgreSQL "
                    "mock reference. No user storage, credentials, or network."}};
    QVERIFY(manifest.write(QJsonDocument(metadata).toJson()) > 0);
}

QTEST_MAIN(ModernUiTest)

void ModernUiTest::queryToolbarShowsOnlyRequestedControls() {
    choscordb::MainWindow window;
    window.show();
    window.findChild<QAction*>("newQuery")->trigger();
    auto* toolbar = window.findChild<QToolBar*>("queryToolbar");
    QVERIFY(toolbar);
    for (const char* name : {"saveSqlButton", "cancelQueryButton", "queryResultDiscardEdits",
                             "queryResultApplyEdits"}) {
        auto* control = window.findChild<QWidget*>(name);
        QVERIFY2(!control || control->isHidden(), name);
    }
    auto* run = window.findChild<choscordb::design::Button*>("runStatementButton");
    QVERIFY(run);
    QVERIFY(run->isVisible());
    QCOMPARE(run->variant(), choscordb::design::ButtonVariant::Default);
}
