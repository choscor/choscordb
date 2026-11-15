#include "app/appearance_controller.h"
#include "app/editor_preferences.h"
#include "app/main_window.h"
#include "app/object_explorer.h"
#include "app/query_workspace.h"
#include "design_system/quick_search/quick_search_dialog.h"
#include "models/shortcut_catalog.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAction>
#include <QSignalSpy>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QtTest>

class WorkspaceShortcutsTest : public QObject {
    Q_OBJECT
    static bool ready(choscordb::MainWindow& window) {
        window.show();
        return QTest::qWaitFor([&] {
            return window.findChild<choscordb::AppearanceController*>()->isReady() &&
                   window.findChild<QAction*>("newQuery")->isEnabled();
        });
    }

  private slots:
    void catalogListsTheCommandsWithoutConflicts() {
        QTemporaryDir folder;
        choscordb::MainWindow window(nullptr, folder.filePath("settings.store"));
        QVERIFY(ready(window));
        const auto catalog = window.findChild<choscordb::EditorPreferencesController*>()->catalog();
        const auto validation = choscordb::validateShortcuts({}, catalog);
        QVERIFY2(validation.ok(), qPrintable(validation.message));
        QHash<QString, QString> defaults;
        for (const auto& command : catalog)
            defaults.insert(command.id, command.defaultSequence);
        const QList<QPair<QString, QString>> expected = {
            {"switch_connection", "Ctrl+Shift+K"},
            {"reload_tab", "Ctrl+R"},
            {"previous_pane", "Ctrl+Alt+["},
            {"next_pane", "Ctrl+Alt+]"},
            {"previous_page", "Ctrl+Alt+Left"},
            {"next_page", "Ctrl+Alt+Right"},
            {"export_result", "Ctrl+Shift+E"},
            {"toggle_results", "Ctrl+`"},
            {"select_tab_1", "Ctrl+1"},
            {"select_tab_9", "Ctrl+9"},
            {"grid_apply_edits_alternate", "Ctrl+Shift+P"},
            {"refresh", "F5"},
        };
        for (const auto& [id, sequence] : expected)
            QCOMPARE(defaults.value(id), sequence);
        QVERIFY(defaults.contains("replace"));
        QVERIFY(QStringList({"", "Ctrl+H"}).contains(defaults.value("replace")));
    }
    void tabNumbersAndTheResultsToggleChangeTheView() {
        QTemporaryDir folder;
        choscordb::MainWindow window(nullptr, folder.filePath("settings.store"));
        QVERIFY(ready(window));
        for (int i = 0; i < 3; ++i)
            window.findChild<QAction*>("newQuery")->trigger();
        auto* tabs = window.findChild<QTabWidget*>("editorTabs");
        QCOMPARE(tabs->count(), 3);
        QVERIFY(window.findChild<QAction*>("queryResultApplyEdits")
                    ->shortcuts()
                    .contains(QKeySequence("Ctrl+Shift+P")));
        window.findChild<QAction*>("command_select_tab_2")->trigger();
        QCOMPARE(tabs->currentIndex(), 1);
        window.findChild<QAction*>("command_select_tab_9")->trigger();
        QCOMPARE(tabs->currentIndex(), 1);
        auto* results = window.findChild<QSplitter*>("workspaceSplitter")->widget(1);
        QVERIFY(results->isVisible());
        auto* toggle = window.findChild<QAction*>("toggleResults");
        toggle->trigger();
        QVERIFY(!results->isVisible());
        QVERIFY(!toggle->isChecked());
        window.findChild<QAction*>("command_select_tab_1")->trigger();
        QVERIFY(!results->isVisible());
        toggle->trigger();
        QVERIFY(results->isVisible());
    }
    void switchConnectionMovesTheTabToAnotherConnection() {
        QTemporaryDir folder;
        for (const auto* name : {"a.sqlite", "b.sqlite"}) {
            QFile file(folder.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        choscordb::MainWindow window(nullptr, folder.filePath("settings.store"));
        QVERIFY(ready(window));
        window.findChild<QAction*>("newQuery")->trigger();
        auto* editor = qobject_cast<choscordb::SqlEditor*>(
            window.findChild<QTabWidget*>("editorTabs")->currentWidget());
        auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
        QSignalSpy connected(workspace, &choscordb::QueryWorkspace::connectionReady);
        workspace->connectSqlite(folder.filePath("a.sqlite"));
        QTRY_COMPARE(connected.count(), 1);
        workspace->connectSqlite(folder.filePath("b.sqlite"));
        QTRY_COMPARE(connected.count(), 2);
        QTRY_COMPARE(workspace->liveConnections().size(), 2);
        QTRY_VERIFY(editor->connectionTarget().has_value());
        const auto current = *editor->connectionTarget();
        const auto first = connected.at(0).at(0).toULongLong();
        const auto other = current == first ? connected.at(1).at(0).toULongLong() : first;
        window.findChild<QAction*>("command_switch_connection")->trigger();
        auto* picker = window.findChild<choscordb::design::QuickSearchDialog*>("connectionPicker");
        QVERIFY(picker);
        QTRY_VERIFY(picker->isVisible());
        QCOMPARE(picker->results().size(), 2);
        int marked = 0;
        for (const auto& row : picker->results())
            marked += row.id == QString::number(current) && row.context == "Current";
        QCOMPARE(marked, 1);
        emit picker->activated(QString::number(other));
        QTRY_COMPARE(editor->connectionTarget(), std::optional<quint64>(other));
    }
    void objectPanesStepOverHiddenPanes() {
        choscordb::EngineAdapter adapter;
        choscordb::ObjectExplorer explorer(&adapter);
        auto* tabs = explorer.findChild<QTabBar*>("objectTabs");
        explorer.restoreObject(std::nullopt, R"(["main","v"])", "main.v", "view");
        QVERIFY(!tabs->isTabVisible(4));
        explorer.selectPane(3);
        explorer.stepPane(1);
        QCOMPARE(tabs->currentIndex(), 5);
        explorer.stepPane(-1);
        QCOMPARE(tabs->currentIndex(), 3);
        QCOMPARE(explorer.reload(), QString("Reconnect this tab before you reload it."));
    }
};

QTEST_MAIN(WorkspaceShortcutsTest)
#include "workspace_shortcuts_test.moc"
