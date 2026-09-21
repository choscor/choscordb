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
#include "modern_ui_test.h"
#include "tools/preview/preview_window.h"
#include "widgets/profile_dialog/profile_dialog.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAction>
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

void ModernUiTest::newLayoutUsesReferenceProportionsAndPreservesSavedPlacement() {
    QTemporaryDir directory;
    const auto path = directory.filePath("layout.sqlite");
    {
        choscordb::MainWindow window(nullptr, path);
        auto* appearance = window.findChild<choscordb::AppearanceController*>();
        QTRY_VERIFY(appearance->isReady());
        QCOMPARE(appearance->persisted().navigatorWidth, quint32(260));
        QCOMPARE(appearance->persisted().editorResultsSplit, quint16(500));
    }
    {
        choscordb::EngineAdapter adapter(nullptr, path);
        QSignalSpy loaded(&adapter, &choscordb::EngineAdapter::appearanceLayoutReady);
        choscordb::AppearanceLayout saved;
        saved.navigatorWidth = 312;
        saved.editorResultsSplit = 610;
        saved.width = 1180;
        saved.height = 780;
        QVERIFY(adapter.setAppearanceLayout(saved, 41));
        QTRY_COMPARE(loaded.count(), 1);
    }
    choscordb::MainWindow restored(nullptr, path);
    restored.show();
    auto* appearance = restored.findChild<choscordb::AppearanceController*>();
    QTRY_VERIFY(appearance->isReady());
    QCOMPARE(appearance->persisted().navigatorWidth, quint32(312));
    QCOMPARE(appearance->persisted().editorResultsSplit, quint16(610));
    QCOMPARE(restored.size(), QSize(1180, 780));
    restored.resize(960, 640);
    QTRY_COMPARE(restored.findChild<QDockWidget*>("navigator")->width(), 312);
    appearance->resetLayout();
    QTRY_COMPARE(appearance->persisted().navigatorWidth, quint32(260));
    QTRY_COMPARE(appearance->persisted().editorResultsSplit, quint16(500));
    QSignalSpy saved(appearance, &choscordb::AppearanceController::saveFinished);
    appearance->stageReset();
    appearance->applyPreview();
    QTRY_COMPARE(saved.count(), 1);
    QVERIFY(saved.first().first().toBool());
    QCOMPARE(appearance->persisted().navigatorWidth, quint32(260));
    QCOMPARE(appearance->persisted().editorResultsSplit, quint16(500));
}

void ModernUiTest::centralScreensPreserveDraftsAndLastCloseReturnsToStart() {
    choscordb::MainWindow window;
    window.resize(960, 640);
    window.show();
    auto* screens = window.findChild<QStackedWidget*>("centralScreens");
    QVERIFY(screens);
    QCOMPARE(screens->currentWidget()->objectName(), QString("startScreen"));
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    QCOMPARE(tabs->count(), 0);
    window.findChild<QAction*>("showSql")->trigger();
    QCOMPARE(screens->currentWidget()->objectName(), QString("sqlScreen"));
    auto* editor = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
    editor->setText("-- retained draft");
    window.findChild<QAction*>("showHistory")->trigger();
    QCOMPARE(screens->currentWidget()->objectName(), QString("sqlScreen"));
    QCOMPARE(window.findChild<QStackedWidget*>("sidebarPanels")->currentIndex(), 2);
    window.findChild<QAction*>("showStart")->trigger();
    QCOMPARE(screens->currentWidget()->objectName(), QString("sqlScreen"));
    window.findChild<QAction*>("showSql")->trigger();
    QCOMPARE(tabs->currentWidget(), editor);
    QCOMPARE(editor->text(), QString("-- retained draft"));
    editor->setModified(false);
    while (tabs->count())
        tabs->tabCloseRequested(0);
    QCOMPARE(screens->currentWidget()->objectName(), QString("startScreen"));
    QCOMPARE(tabs->count(), 0);
    QTRY_VERIFY(window.findChild<QAction*>("newQuery")->isEnabled());
    window.findChild<QAction*>("newQuery")->trigger();
    QCOMPARE(tabs->count(), 1);
    QCOMPARE(screens->currentWidget()->objectName(), QString("sqlScreen"));
    auto* sidebar = window.findChild<QDockWidget*>("navigator");
    QVERIFY(sidebar->isVisible());
    QCOMPARE(sidebar->features(), QDockWidget::NoDockWidgetFeatures);
}

void ModernUiTest::startListsRealSavedProfilesAndConnectsWithoutExecuting() {
    choscordb::MainWindow window;
    window.show();
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    auto* profiles = window.findChild<QListWidget*>("savedConnections");
    QVERIFY(profiles);
    choscordb::SavedProfile profile;
    profile.id = "start-sqlite";
    profile.name = "Start SQLite";
    profile.path = ":memory:";
    workspace->adapter()->saveProfile(profile, 991);
    QTRY_COMPARE(profiles->count(), 1);
    QVERIFY(profiles->item(0)->text().contains("Start SQLite"));
    QCOMPARE(profiles->visualItemRect(profiles->item(0)).height(), 42);
    QVERIFY(profiles->height() < 100);
    QVERIFY(!profiles->item(0)->icon().isNull());
    auto* startIcon = window.findChild<QLabel*>("startDatabaseIcon");
    QVERIFY(startIcon);
    QCOMPARE(startIcon->pixmap().deviceIndependentSize(), QSizeF(30, 30));
    int queued = 0;
    connect(
        workspace->adapter(), &choscordb::EngineAdapter::eventReady, &window,
        [&](const choscordb::BridgeEvent& event) {
            if (QString::fromUtf8(event.kind.data(), qsizetype(event.kind.size())) ==
                    "query_state" &&
                QString::fromUtf8(event.state.data(), qsizetype(event.state.size())) == "queued")
                ++queued;
        },
        Qt::DirectConnection);
    QSignalSpy connected(workspace, &choscordb::QueryWorkspace::connectionReady);
    QTest::mouseClick(profiles->viewport(), Qt::LeftButton, Qt::NoModifier,
                      profiles->visualItemRect(profiles->item(0)).center());
    QTRY_COMPARE(connected.count(), 1);
    auto* screens = window.findChild<QStackedWidget*>("centralScreens");
    QCOMPARE(screens->currentWidget()->objectName(), QString("startScreen"));
    QCOMPARE(queued, 0);
    QTRY_VERIFY(window.findChild<QAction*>("newQuery")->isEnabled());
    window.findChild<QAction*>("newQuery")->trigger();
    auto* editor = qobject_cast<choscordb::SqlEditor*>(
        window.findChild<QTabWidget*>("editorTabs")->currentWidget());
    editor->setText("SELECT 73");
    auto* run = window.findChild<QAction*>("runStatement");
    QTRY_VERIFY(run->isEnabled());
    run->trigger();
    QTRY_COMPARE(queued, 1);
}

void ModernUiTest::savedProfileSelectionKeepsEditorTargetAndShowsOneTree() {
    choscordb::MainWindow window;
    window.show();
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    auto* profiles = window.findChild<QListWidget*>("savedConnections");
    auto* tree = window.findChild<QTreeView*>("databaseNavigator");
    auto* selector = window.findChild<QComboBox*>("connectionSelector");
    QVERIFY(workspace && profiles && tree && selector);
    choscordb::SavedProfile profile;
    profile.id = "browse-alpha";
    profile.name = "Browse Alpha";
    profile.path = ":memory:";
    workspace->adapter()->saveProfile(profile, 8101);
    profile.id = "browse-beta";
    profile.name = "Browse Beta";
    workspace->adapter()->saveProfile(profile, 8102);
    QTRY_COMPARE(profiles->count(), 2);
    auto click = [profiles](const QString& name) {
        for (int row = 0; row < profiles->count(); ++row)
            if (profiles->item(row)->text().startsWith(name)) {
                QTest::mouseClick(profiles->viewport(), Qt::LeftButton, Qt::NoModifier,
                                  profiles->visualItemRect(profiles->item(row)).center());
                return;
            }
    };
    click("Browse Alpha");
    QTRY_VERIFY(window.browsingConnection().has_value());
    const auto first = *window.browsingConnection();
    QTRY_COMPARE(tree->model()->rowCount(), 1);
    QVERIFY(tree->model()->index(0, 0).data().toString().contains("Browse Alpha"));
    const auto editorTarget = selector->currentData();
    click("Browse Beta");
    QTRY_VERIFY(window.browsingConnection().has_value() && *window.browsingConnection() != first);
    QTRY_COMPARE(tree->model()->rowCount(), 1);
    QVERIFY(tree->model()->index(0, 0).data().toString().contains("Browse Beta"));
    QCOMPARE(selector->currentData(), editorTarget);
}

void ModernUiTest::failedSidebarOpenShowsReasonAndClearsBrowseSelection() {
    choscordb::MainWindow window;
    window.show();
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    auto* profiles = window.findChild<QListWidget*>("savedConnections");
    QVERIFY(workspace && profiles);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    choscordb::SavedProfile profile;
    profile.id = "broken-sidebar";
    profile.name = "Broken Sidebar";
    profile.path = directory.filePath("missing/database.sqlite");
    workspace->adapter()->saveProfile(profile, 8103);
    QTRY_COMPARE(profiles->count(), 1);
    QTest::mouseClick(profiles->viewport(), Qt::LeftButton, Qt::NoModifier,
                      profiles->visualItemRect(profiles->item(0)).center());
    QTRY_VERIFY(window.findChild<QMessageBox*>("sidebarConnectionFailure"));
    auto* dialog = window.findChild<QMessageBox*>("sidebarConnectionFailure");
    QVERIFY(dialog->text().contains("Broken Sidebar"));
    QVERIFY(dialog->text().contains("unable to open", Qt::CaseInsensitive));
    QVERIFY(!window.browsingConnection().has_value());
    QVERIFY(!profiles->currentItem());
    dialog->accept();
}

void ModernUiTest::failedSidebarOpenRestoresLastSuccessfulProfile() {
    choscordb::MainWindow window;
    window.show();
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    auto* profiles = window.findChild<QListWidget*>("savedConnections");
    auto* tree = window.findChild<QTreeView*>("databaseNavigator");
    QVERIFY(workspace && profiles && tree);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    choscordb::SavedProfile valid;
    valid.id = "valid-browse";
    valid.name = "Valid Browse";
    valid.path = ":memory:";
    workspace->adapter()->saveProfile(valid, 8104);
    auto broken = valid;
    broken.id = "invalid-browse";
    broken.name = "Invalid Browse";
    broken.path = directory.filePath("missing/database.sqlite");
    workspace->adapter()->saveProfile(broken, 8105);
    QTRY_COMPARE(profiles->count(), 2);
    auto click = [profiles](const QString& name) {
        for (int row = 0; row < profiles->count(); ++row)
            if (profiles->item(row)->text().startsWith(name)) {
                QTest::mouseClick(profiles->viewport(), Qt::LeftButton, Qt::NoModifier,
                                  profiles->visualItemRect(profiles->item(row)).center());
                return;
            }
    };
    click("Valid Browse");
    QTRY_VERIFY(window.browsingConnection().has_value());
    const auto validId = *window.browsingConnection();
    click("Invalid Browse");
    QTRY_VERIFY(window.findChild<QMessageBox*>("sidebarConnectionFailure"));
    QCOMPARE(window.browsingConnection(), std::optional<quint64>(validId));
    QVERIFY(profiles->currentItem()->text().startsWith("Valid Browse"));
    QTRY_COMPARE(tree->model()->rowCount(), 1);
    QVERIFY(tree->model()->index(0, 0).data().toString().contains("Valid Browse"));
    window.findChild<QMessageBox*>("sidebarConnectionFailure")->accept();
}

void ModernUiTest::savedConnectionMenuDuplicatesTheChosenProfileWithoutConnecting() {
    choscordb::MainWindow window;
    window.show();
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    auto* profiles = window.findChild<QListWidget*>("savedConnections");
    choscordb::SavedProfile profile;
    profile.id = "alpha";
    profile.name = "Alpha";
    profile.path = ":memory:";
    workspace->adapter()->saveProfile(profile, 991);
    profile.id = "beta";
    profile.name = "Beta";
    workspace->adapter()->saveProfile(profile, 992);
    QTRY_COMPARE(profiles->count(), 2);
    QListWidgetItem* chosen = nullptr;
    for (int i = 0; i < profiles->count(); ++i)
        if (profiles->item(i)->text().startsWith("Beta"))
            chosen = profiles->item(i);
    QVERIFY(chosen);
    QSignalSpy connected(workspace, &choscordb::QueryWorkspace::connectionReady);
    const auto position = profiles->visualItemRect(chosen).center();
    QContextMenuEvent event(QContextMenuEvent::Mouse, position,
                            profiles->viewport()->mapToGlobal(position));
    QApplication::sendEvent(profiles->viewport(), &event);
    auto* menu = window.findChild<QMenu*>("savedConnectionMenu");
    QVERIFY(menu);
    QTRY_VERIFY(menu->isVisible());
    const QPoint panel =
        menu->mapToGlobal(QPoint()) + QPoint(choscordb::design::detail::menuShadowMargin(),
                                             choscordb::design::detail::menuShadowMargin());
    QCOMPARE(panel, event.globalPos());
    auto* duplicate = menu->findChild<QAction*>("duplicateSavedConnection");
    QVERIFY(duplicate);
    duplicate->trigger();
    menu->close();
    QTRY_COMPARE(profiles->count(), 3);
    bool found = false;
    for (int i = 0; i < profiles->count(); ++i)
        found |= profiles->item(i)->text().startsWith("Beta copy");
    QVERIFY(found);
    QCOMPARE(connected.count(), 0);
    window.findChild<QDialog*>("profileDialog")->reject();
}

void ModernUiTest::appearanceSaveDuringLayoutWriteReturnsRetryAndKeepsPreview() {
    choscordb::MainWindow window;
    auto* appearance = window.findChild<choscordb::AppearanceController*>();
    auto* theme = window.findChild<choscordb::design::ThemeManager*>();
    QTRY_VERIFY(appearance->isReady());
    QSignalSpy layoutSaved(window.findChild<choscordb::QueryWorkspace*>()->adapter(),
                           &choscordb::EngineAdapter::appearanceLayoutReady);
    appearance->resetLayout();
    QVERIFY(appearance->preview("dark"));
    QSignalSpy saved(appearance, &choscordb::AppearanceController::saveFinished);
    appearance->applyPreview();
    QCOMPARE(saved.count(), 1);
    QVERIFY(!saved.first().first().toBool());
    QVERIFY(saved.first().at(1).toString().contains("retry", Qt::CaseInsensitive));
    QCOMPARE(theme->mode(), choscordb::design::ThemeMode::Dark);
    QTRY_COMPARE(layoutSaved.count(), 1);
    QCOMPARE(theme->mode(), choscordb::design::ThemeMode::Dark);
    appearance->applyPreview();
    QTRY_COMPARE(saved.count(), 2);
    QVERIFY(saved.last().first().toBool());
}

void ModernUiTest::activeWorkKeepsSqlVisibleAndRejectsPreferences() {
    choscordb::MainWindow window;
    window.show();
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    auto* run = window.findChild<QAction*>("runStatement");
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    QTRY_VERIFY(window.findChild<QAction*>("newQuery")->isEnabled());
    window.findChild<QAction*>("newQuery")->trigger();
    auto* editor = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
    workspace->connectSqlite(":memory:");
    QTRY_VERIFY(run->isEnabled());
    editor->setText("WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE "
                    "x<100000000) SELECT sum(x) FROM n");
    run->trigger();
    auto* screens = window.findChild<QStackedWidget*>("centralScreens");
    QCOMPARE(screens->currentWidget()->objectName(), QString("sqlScreen"));
    window.findChild<QAction*>("preferences")->trigger();
    QVERIFY(!window.findChild<QDialog*>("preferencesDialog"));
    for (const auto* route : {"showStart", "showHistory", "showObjects"}) {
        window.findChild<QAction*>(route)->trigger();
        QCOMPARE(screens->currentWidget()->objectName(), QString("sqlScreen"));
    }
    auto* cancel = window.findChild<QPushButton*>("cancelQueryButton");
    QVERIFY(cancel);
    QVERIFY(cancel->isVisible());
    QVERIFY(cancel->isEnabled());
    QTest::mouseClick(cancel, Qt::LeftButton);
    QTRY_VERIFY(run->isEnabled());
    window.findChild<QAction*>("showHistory")->trigger();
    QCOMPARE(screens->currentWidget()->objectName(), QString("sqlScreen"));
    QCOMPARE(window.findChild<QStackedWidget*>("sidebarPanels")->currentIndex(), 2);
}

void ModernUiTest::sqlCompositionKeepsTabsFirstAndPinsResultActions() {
    choscordb::MainWindow window;
    window.show();
    QTRY_VERIFY(window.findChild<choscordb::AppearanceController*>()->isReady());
    window.resize(960, 640);
    QCOMPARE(window.size(), QSize(960, 640));
    window.findChild<QAction*>("showSql")->trigger();
    QCoreApplication::processEvents();
    auto* sql = window.findChild<QWidget*>("sqlScreen");
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    auto* toolbar = window.findChild<QToolBar*>();
    auto* grid = window.findChild<QTableView*>("queryResults");
    auto* exportButton = window.findChild<QPushButton*>("exportResult");
    QVERIFY(tabs->mapTo(sql, QPoint()).y() < toolbar->mapTo(sql, QPoint()).y());
    QVERIFY(toolbar->mapTo(sql, toolbar->rect().bottomLeft()).y() <=
            grid->mapTo(sql, QPoint()).y());
    QCOMPARE(grid->mapTo(sql, QPoint()).x(), 0);
    QCOMPARE(tabs->tabBar()->tabRect(0).height(), 33);
    QVERIFY(tabs->tabBar()->height() > tabs->tabBar()->tabRect(0).height());
    QVERIFY(!tabs->tabIcon(0).isNull());
    QCOMPARE(grid->verticalHeader()->defaultSectionSize(), 35);
    QCOMPARE(grid->horizontalHeader()->height(), 43);
    QVERIFY(grid->showGrid());
    QVERIFY(!grid->wordWrap());
    QVERIFY(!grid->horizontalHeader()->stretchLastSection());
    auto* footer = window.findChild<QWidget*>("sqlResultFooter");
    QVERIFY(footer);
    QVERIFY(footer->mapTo(sql, QPoint()).y() > grid->mapTo(sql, grid->rect().bottomLeft()).y());
    QVERIFY(exportButton->isVisible());
    QVERIFY(sql->rect().contains(QRect(exportButton->mapTo(sql, QPoint()), exportButton->size())));
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    workspace->connectSqlite(":memory:");
    auto* run = window.findChild<QAction*>("runStatement");
    QTRY_VERIFY(run->isEnabled());
    auto* editor = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
    QVERIFY(editor);
    editor->setText("SELECT * FROM missing_table;");
    run->trigger();
    auto* messages = window.findChild<QPlainTextEdit*>("queryMessages");
    QTRY_VERIFY(messages->isVisible());
    QTRY_VERIFY(messages->toPlainText().contains("missing_table"));
    QVERIFY(footer->isVisible());
    QVERIFY(exportButton->isVisible());
    QTRY_VERIFY(run->isEnabled());
    editor->setText("SELECT 1;");
    run->trigger();
    QTRY_COMPARE(grid->model()->rowCount(), 1);
    QTRY_VERIFY(grid->isVisible());
    QVERIFY(!messages->isVisible());
    QVERIFY(footer->isVisible());
}

void ModernUiTest::workspaceHasNoOnboardingActionStrip() {
    choscordb::MainWindow window;
    window.show();
    window.findChild<QAction*>("showSql")->trigger();
    QVERIFY(!window.findChild<QWidget*>("emptyWorkspaceActions"));
}

void ModernUiTest::liveAppearanceReachesAlreadyOpenConnectionPanelAndMenu() {
    choscordb::MainWindow window;
    window.show();
    auto* appearance = window.findChild<choscordb::AppearanceController*>();
    QTRY_VERIFY(appearance->isReady());
    QSignalSpy saved(appearance, &choscordb::AppearanceController::saveFinished);
    QVERIFY(appearance->preview("light"));
    appearance->applyPreview();
    QTRY_COMPARE(saved.count(), 1);
    QVERIFY(saved.at(0).at(0).toBool());
    const auto ownerSize = window.size();
    window.findChild<QPushButton*>("navigatorAddConnection")->click();
    auto* profile = window.findChild<choscordb::ProfileDialog*>();
    QVERIFY(profile);
    QVERIFY(profile->isVisible());
    QVERIFY(!profile->isWindow());
    QCOMPARE(profile->window(), &window);
    QCOMPARE(window.size(), ownerSize);
    QVERIFY(window.rect().contains(QRect(profile->mapTo(&window, QPoint()), profile->size())));
    const auto capture = [&](const QString& name) {
        const auto directory = qEnvironmentVariable("CHOSCORDB_TEST_CAPTURE_DIR");
        if (!directory.isEmpty()) {
            QVERIFY(QDir().mkpath(directory));
            QVERIFY(window.grab().save(QDir(directory).filePath(name)));
        }
    };
    capture("connection-overlay-light.png");
    QCOMPARE(choscordb::design::DialogPresentation::activeDialog(), profile);
    QMenu menu(profile);
    menu.addAction("Synthetic action");
    QTRY_VERIFY(profile->isActiveWindow());
    menu.popup(profile->mapToGlobal(QPoint(100, 100)));
    QTRY_VERIFY(menu.isVisible());
    QCoreApplication::processEvents();
    auto surface = [profile] {
        return profile->grab().toImage().pixelColor(profile->width() - 8, profile->height() / 2);
    };
    QCOMPARE(surface(), QColor("#ffffff"));
    QVERIFY(appearance->preview("dark"));
    QCoreApplication::processEvents();
    QCOMPARE(surface(), QColor("#20272b"));
    QCOMPARE(menu.palette().color(QPalette::Window), QColor("#20272b"));
    QVERIFY(profile->isVisible());
    QVERIFY(menu.isVisible());
    appearance->cancelPreview();
    QCoreApplication::processEvents();
    QCOMPARE(surface(), QColor("#ffffff"));
    QCOMPARE(menu.palette().color(QPalette::Window), QColor("#ffffff"));
    QVERIFY(appearance->preview("dark"));
    appearance->applyPreview();
    QTRY_COMPARE(saved.count(), 2);
    QVERIFY(saved.at(1).at(0).toBool());
    QCOMPARE(surface(), QColor("#20272b"));
    menu.hide();
    capture("connection-overlay-dark.png");
    profile->reject();
}

void ModernUiTest::minimumWorkspaceKeepsQueryControlsInsideTheWindow() {
    choscordb::MainWindow window;
    window.show();
    window.findChild<QAction*>("showSql")->trigger();
    auto* appearance = window.findChild<choscordb::AppearanceController*>();
    QTRY_VERIFY(appearance->isReady());
    QVERIFY(appearance->preview("dark"));
    window.resize(960, 640);
    window.show();
    QCoreApplication::processEvents();
    auto* toolbar = window.findChild<QToolBar*>();
    QVERIFY(toolbar);
    toolbar->layout()->invalidate();
    toolbar->layout()->activate();
    QCoreApplication::processEvents();
    const auto rendered = window.grab();
    QVERIFY(!rendered.isNull());
    auto* select = window.findChild<QComboBox*>("connectionSelector");
    QVERIFY(select->isVisible());
    QVERIFY(!select->visibleRegion().isEmpty());
    QVERIFY(window.rect().contains(QRect(select->mapTo(&window, QPoint()), select->size())));
    auto* more = window.findChild<QToolButton*>("queryToolbarOverflow");
    QVERIFY(more);
    QVERIFY(more->isVisible());
    QVERIFY(!more->visibleRegion().isEmpty());
    QVERIFY(window.rect().contains(QRect(more->mapTo(&window, QPoint()), more->size())));
}

void ModernUiTest::workspaceUsesApprovedButtonGeometryAndIcons() {
    choscordb::MainWindow window;
    window.show();
    const QStringList iconNames{"navigatorAddConnection", "navigatorRefresh",
                                "navigatorDisconnect"};
    for (const auto& name : iconNames) {
        auto* button = window.findChild<QPushButton*>(name);
        QVERIFY(button);
        QVERIFY2(qobject_cast<choscordb::design::Button*>(button), qPrintable(name));
        QVERIFY(button->text().isEmpty());
        QVERIFY(!button->icon().isNull());
        QCOMPARE(button->sizeHint(), QSize(30, 30));
    }
    const QList<QPair<QString, int>> actions{
        {"previousPage", 30}, {"nextPage", 30}, {"exportResult", 30}};
    for (const auto& [name, height] : actions) {
        auto* button = window.findChild<QPushButton*>(name);
        QVERIFY(button);
        QVERIFY2(qobject_cast<choscordb::design::Button*>(button), qPrintable(name));
        QCOMPARE(button->sizeHint().height(), height);
    }
    auto* previous = window.findChild<QPushButton*>("previousPage");
    auto* next = window.findChild<QPushButton*>("nextPage");
    QVERIFY(!previous->icon().isNull());
    QVERIFY(!next->icon().isNull());
    QVERIFY(!previous->isEnabled());
    QVERIFY(!next->isEnabled());
}

void ModernUiTest::developmentMenuOpensIndependentPreview() {
    choscordb::MainWindow window;
    auto* action = window.findChild<QAction*>("openDesignSystemPreview");
    QVERIFY(action);
    const auto theme = window.findChild<choscordb::design::ThemeManager*>()->resolvedTheme();
    action->trigger();
    auto* preview = window.findChild<choscordb::design::PreviewWindow*>();
    QVERIFY(preview);
    QVERIFY(preview->isVisible());
    QVERIFY(!preview->isModal());
    QCOMPARE(window.findChild<choscordb::design::ThemeManager*>()->resolvedTheme(), theme);
    preview->close();
}

void ModernUiTest::workspaceProvidesDiscoverableModernControls() {
    choscordb::MainWindow window;
    window.resize(1000, 700);
    window.show();

    auto* navigatorTitle = window.findChild<QLabel*>("navigatorTitle");
    auto* addConnection = window.findChild<QPushButton*>("navigatorAddConnection");
    auto* summary = window.findChild<QLabel*>("executionSummary");
    auto* toast = window.findChild<choscordb::ToastRegion*>("toastRegion");
    auto* resetLayout = window.findChild<QAction*>("resetLayout");

    QVERIFY(navigatorTitle);
    QCOMPARE(navigatorTitle->text(), QString("CONNECTIONS"));
    QVERIFY(addConnection);
    QVERIFY(!addConnection->accessibleName().isEmpty());
    QVERIFY(!addConnection->toolTip().isEmpty());
    QVERIFY(summary);
    QCOMPARE(summary->property("state").toString(), QString("disconnected"));
    QVERIFY(!summary->accessibleName().isEmpty());
    QVERIFY(toast);
    QVERIFY(!toast->accessibleName().isEmpty());
    QVERIFY(toast->isHidden());
    auto* host = window.centralWidget();
    const auto contentBefore = window.findChild<QStackedWidget*>()->geometry();
    toast->showToast("Warning", "First notice", choscordb::ToastVariant::Warning, 10000);
    toast->showToast("Warning", "Replacement notice", choscordb::ToastVariant::Warning, 10000);
    QVERIFY(toast->text().contains("Replacement notice"));
    QCOMPARE(toast->property("variant").toString(), QString("warning"));
    QCoreApplication::processEvents();
    QCOMPARE(toast->parentWidget(), host);
    QCOMPARE(window.findChild<QStackedWidget*>()->geometry(), contentBefore);
    QVERIFY(toast->geometry().right() <= host->width());
    QVERIFY(toast->geometry().bottom() <= host->height());
    QVERIFY(toast->geometry().right() > host->width() / 2);
    QVERIFY(toast->geometry().bottom() > host->height() / 2);
    window.resize(1200, 800);
    QCoreApplication::processEvents();
    QCOMPARE(toast->geometry().right(), host->width() - 17);
    QCOMPARE(toast->geometry().bottom(), host->height() - 17);
    window.showToast("Could not save", choscordb::ToastVariant::Danger);
    QCOMPARE(toast->property("variant").toString(), QString("danger"));
    QVERIFY(toast->text().contains("Could not save"));
    QVERIFY(resetLayout);
    auto* run = window.findChild<QAction*>("runStatement");
    QVERIFY(run);
    QVERIFY(!run->icon().isNull());
    QVERIFY(QFile::exists(":/icons/app-mark.svg"));
}

void ModernUiTest::navigatorContextActionsSupportKeyboardFocusAndMenus() {
    choscordb::MainWindow window;
    auto* tree = window.findChild<QTreeView*>("databaseNavigator");
    auto* refresh = window.findChild<QPushButton*>("navigatorRefresh");
    auto* disconnect = window.findChild<QPushButton*>("navigatorDisconnect");
    auto* refreshAction = window.findChild<QAction*>("navigatorRefreshAction");
    auto* disconnectAction = window.findChild<QAction*>("navigatorDisconnectAction");
    QVERIFY(tree);
    QVERIFY(refresh);
    QVERIFY(disconnect);
    QVERIFY(refreshAction);
    QVERIFY(disconnectAction);
    QCOMPARE(tree->contextMenuPolicy(), Qt::CustomContextMenu);
    QVERIFY(refresh->isHidden());
    QVERIFY(disconnect->isHidden());
    QVERIFY(tree->actions().contains(refreshAction));
    QVERIFY(tree->actions().contains(disconnectAction));

    window.show();
    tree->setFocus();
    QTRY_VERIFY(refresh->isVisible());
    QTRY_VERIFY(disconnect->isVisible());
}

void ModernUiTest::transactionControlsStayInMoreMenuAtBothWidths() {
    choscordb::MainWindow window;
    window.findChild<QAction*>("showSql")->trigger();
    window.show();
    auto* overflow = window.findChild<QToolButton*>("queryToolbarOverflow");
    QVERIFY(overflow);
    QCOMPARE(overflow->text(), QString("More"));
    QCOMPARE(overflow->toolButtonStyle(), Qt::ToolButtonTextOnly);
    QVERIFY(overflow->icon().isNull());
    auto* mode = overflow->menu()->findChild<QComboBox*>("transactionMode");
    QVERIFY(mode);
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    workspace->connectSqlite(":memory:");
    QTRY_VERIFY(window.findChild<QAction*>("runStatement")->isEnabled());
    for (const int width : {960, 1280}) {
        window.resize(width, 640);
        QTRY_VERIFY(overflow->isVisible());
        QVERIFY(!mode->isVisible());
        overflow->menu()->popup(overflow->mapToGlobal(overflow->rect().topLeft()));
        QTRY_VERIFY(mode->isVisible());
        mode->setFocus();
        QTest::keyClick(mode, Qt::Key_End);
        QCOMPARE(mode->currentIndex(), 1);
        QVERIFY(window.findChild<QAction*>("command_commit")->isEnabled());
        overflow->menu()->hide();
    }
}

void ModernUiTest::inactiveEditorCloseButtonAppearsOnHover() {
    choscordb::MainWindow window;
    auto* editors = window.findChild<QTabWidget*>("editorTabs");
    auto* newQuery = window.findChild<QAction*>("newQuery");
    QVERIFY(editors);
    QVERIFY(newQuery);
    newQuery->trigger();
    newQuery->trigger();
    QCOMPARE(editors->count(), 2);
    window.show();
    QCoreApplication::processEvents();

    auto* tabs = editors->tabBar();
    const auto side = static_cast<QTabBar::ButtonPosition>(
        tabs->style()->styleHint(QStyle::SH_TabBar_CloseButtonPosition, nullptr, tabs));
    const int inactive = editors->currentIndex() == 0 ? 1 : 0;
    auto* close = tabs->tabButton(inactive, side);
    QVERIFY(close);
    QVERIFY(!close->isVisible());
    QTest::mouseMove(tabs, tabs->tabRect(inactive).center());
    QTRY_VERIFY(close->isVisible());
    QEvent leave(QEvent::Leave);
    QCoreApplication::sendEvent(tabs, &leave);
    QTRY_VERIFY(!close->isVisible());
}

void ModernUiTest::paletteUpdatePreservesCompleteEditorState() {
    const auto originalPalette = qApp->palette();
    const auto originalStyleSheet = qApp->styleSheet();
    choscordb::design::ThemeManager theme;
    theme.installOn(qApp);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    choscordb::SqlEditor editor;
    editor.setText("SELECT alpha FROM sample;\nSELECT beta;");
    const auto path = directory.filePath("theme-state.sql");
    QSignalSpy saved(&editor, &choscordb::SqlEditor::fileSaved);
    editor.saveFile(path);
    QTRY_COMPARE(saved.count(), 1);
    QCOMPARE(saved.at(0).at(0).toString(), path);
    QVERIFY(saved.at(0).at(1).toString().isEmpty());
    editor.setModified(true);
    editor.setProfileId("profile-a");
    editor.SendScintilla(QsciScintilla::SCI_SETSEL, 7UL, 12L);
    editor.SendScintilla(QsciScintilla::SCI_SETFIRSTVISIBLELINE, 1);
    editor.insert("x");
    QVERIFY(editor.isUndoAvailable());
    const auto* lexer = editor.lexer();
    const auto keywordMeaning = lexer->description(QsciLexerSQL::Keyword);
    const auto bytes = editor.text().toUtf8();
    const auto cursor = editor.SendScintilla(QsciScintilla::SCI_GETCURRENTPOS);
    const auto anchor = editor.SendScintilla(QsciScintilla::SCI_GETANCHOR);
    const auto firstLine = editor.SendScintilla(QsciScintilla::SCI_GETFIRSTVISIBLELINE);

    theme.setMode(choscordb::design::ThemeMode::Dark);
    QCoreApplication::processEvents();

    QCOMPARE(editor.text().toUtf8(), bytes);
    QCOMPARE(editor.SendScintilla(QsciScintilla::SCI_GETCURRENTPOS), cursor);
    QCOMPARE(editor.SendScintilla(QsciScintilla::SCI_GETANCHOR), anchor);
    QCOMPARE(editor.SendScintilla(QsciScintilla::SCI_GETFIRSTVISIBLELINE), firstLine);
    QVERIFY(editor.isModified());
    QVERIFY(editor.isUndoAvailable());
    QCOMPARE(editor.filePath(), path);
    QCOMPARE(editor.lexer(), lexer);
    QCOMPARE(editor.lexer()->description(QsciLexerSQL::Keyword), keywordMeaning);
    editor.undo();
    QVERIFY(editor.isRedoAvailable());
    editor.redo();
    QCOMPARE(editor.text().toUtf8(), bytes);
    QCOMPARE(editor.property("profileId").toString(), QString("profile-a"));
    theme.installOn(nullptr);
    qApp->setPalette(originalPalette);
    qApp->setStyleSheet(originalStyleSheet);
}

void ModernUiTest::appearancePreviewsPersistAndRestoreAcrossRestart() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath("ui.sqlite");
    {
        choscordb::MainWindow window(nullptr, path);
        auto* appearance = window.findChild<choscordb::AppearanceController*>();
        auto* theme = window.findChild<choscordb::design::ThemeManager*>();
        QVERIFY(appearance);
        QVERIFY(theme);
        QTRY_VERIFY(appearance->isReady());
        QSignalSpy warning(appearance, &choscordb::AppearanceController::warningChanged);
        QVERIFY(appearance->preview("dark"));
        QCOMPARE(theme->mode(), choscordb::design::ThemeMode::Dark);
        const auto acceptedAccent = theme->accent();
        QVERIFY(!appearance->preview("sepia"));
        QCOMPARE(theme->accent(), acceptedAccent);
        QVERIFY(!warning.isEmpty());
        QVERIFY(!warning.last().at(0).toString().isEmpty());
        QSignalSpy saved(appearance, &choscordb::AppearanceController::saveFinished);
        appearance->applyPreview();
        QTRY_COMPARE(saved.count(), 1);
        QVERIFY2(saved.at(0).at(0).toBool(), qPrintable(saved.at(0).at(1).toString()));
        appearance->resetLayout();
        QCOMPARE(theme->mode(), choscordb::design::ThemeMode::Dark);
        QTest::qWait(400);
    }
    {
        choscordb::MainWindow window(nullptr, path);
        auto* appearance = window.findChild<choscordb::AppearanceController*>();
        auto* theme = window.findChild<choscordb::design::ThemeManager*>();
        QVERIFY(appearance);
        QVERIFY(theme);
        QTRY_VERIFY(appearance->isReady());
        QCOMPARE(theme->mode(), choscordb::design::ThemeMode::Dark);
        appearance->reset();
        QTRY_COMPARE(theme->mode(), choscordb::design::ThemeMode::System);
        QCOMPARE(theme->density(), choscordb::design::Density::Compact);
    }
}

void ModernUiTest::preferencesUseSectionNavigationAndCancelableLivePreview() {
    choscordb::MainWindow window;
    window.show();
    auto* appearance = window.findChild<choscordb::AppearanceController*>();
    auto* theme = window.findChild<choscordb::design::ThemeManager*>();
    QTRY_VERIFY(appearance->isReady());
    window.findChild<QAction*>("preferences")->trigger();
    auto* dialog = window.findChild<QDialog*>("preferencesDialog");
    QVERIFY(dialog);
    auto* sections = dialog->findChild<QTabWidget*>("preferencesSections");
    auto* mode = dialog->findChild<QComboBox*>("appearanceTheme");
    auto* density = dialog->findChild<QComboBox*>("appearanceDensity");
    auto* accent = dialog->findChild<QComboBox*>("appearanceAccent");
    auto* custom = dialog->findChild<QLineEdit*>("appearanceCustomAccent");
    auto* status = dialog->findChild<QLabel*>("appearanceStatus");
    auto* apply = dialog->findChild<QPushButton*>("preferencesApply");
    QVERIFY(sections);
    QCOMPARE(sections->count(), 5);
    QCOMPARE(dialog->layout()->contentsMargins().left(), 0);
    QTRY_VERIFY(apply->isEnabled());
    mode->setCurrentIndex(mode->findData("dark"));
    QVERIFY(!density);
    QVERIFY(!accent);
    QVERIFY(!custom);
    QCOMPARE(mode->count(), 3);
    QCOMPARE(theme->mode(), choscordb::design::ThemeMode::Dark);
    QCOMPARE(dialog->layout()->contentsMargins().left(), 0);
    QVERIFY(apply->isEnabled());
    theme->setForcedContrast(true);
    QVERIFY(status->text().contains("high-contrast", Qt::CaseInsensitive));
    theme->setForcedContrast(false);
    dialog->reject();
    QCOMPARE(theme->mode(), choscordb::design::ThemeMode::System);
    QCOMPARE(theme->density(), choscordb::design::Density::Compact);
}

void ModernUiTest::executionStripKeepsVisibleAndAccessibleTerminalState() {
    choscordb::MainWindow window;
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    auto* run = window.findChild<QAction*>("runStatement");
    auto* summary = window.findChild<QLabel*>("executionSummary");
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    QTRY_VERIFY(window.findChild<QAction*>("newQuery")->isEnabled());
    window.findChild<QAction*>("newQuery")->trigger();
    workspace->connectSqlite(":memory:");
    QTRY_VERIFY(run->isEnabled());
    auto* editor = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
    editor->setText("SELECT missing FROM nowhere");
    run->trigger();
    QTRY_COMPARE(summary->property("state").toString(), QString("failed"));
    QTRY_VERIFY(summary->accessibleName().contains("failed", Qt::CaseInsensitive));
    editor->setText("SELECT 1 AS value");
    run->trigger();
    QTRY_COMPARE(summary->property("state").toString(), QString("completed"));
    QTRY_VERIFY(summary->text().contains("ms"));
    QVERIFY(summary->accessibleName().contains("Completed"));
}
