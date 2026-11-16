#include "app/appearance_controller.h"
#include "app/main_window.h"
#include "app/query_workspace.h"
#include "bridge/engine_adapter.h"
#include "confirmation_checks.h"
#include "design_system/tabs/tabs_style.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAction>
#include <QComboBox>
#include <QHelpEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QTableView>
#include <QtTest>

class ConnectionSafetyTest : public QObject {
    Q_OBJECT
    static choscordb::SqlEditor* connect(choscordb::MainWindow& window,
                                         const choscordb::SavedProfile& profile) {
        window.show();
        if (!QTest::qWaitFor([&] {
                return window.findChild<choscordb::AppearanceController*>()->isReady() &&
                       window.findChild<QAction*>("newQuery")->isEnabled();
            }))
            return nullptr;
        window.findChild<QAction*>("newQuery")->trigger();
        auto* editor = qobject_cast<choscordb::SqlEditor*>(
            window.findChild<QTabWidget*>("editorTabs")->currentWidget());
        if (!window.findChild<choscordb::QueryWorkspace*>()->connectSavedProfile(profile))
            return nullptr;
        auto* run = window.findChild<QAction*>("runStatement");
        return QTest::qWaitFor([run] { return run->isEnabled(); }) ? editor : nullptr;
    }
    static choscordb::SavedProfile profile(const QString& environment, const QString& safety) {
        choscordb::SavedProfile value;
        value.id = "tagged-" + safety;
        value.name = "Tagged";
        value.driver = "sqlite";
        value.path = ":memory:";
        value.environment = environment;
        value.safety = safety;
        value.tone = environment == "production" ? "danger" : "success";
        value.tagLabel = environment == "production" ? "Production" : "Development";
        return value;
    }
    static choscordb::SavedProfile normalized(const choscordb::SavedProfile& value) {
        return choscordb::EngineAdapter::normalizeProfileDraft(value, value, {});
    }

  private slots:
    void readOnlyRejectsWritesAndShowsTheTag() {
        choscordb::MainWindow window;
        auto* editor = connect(window, normalized(profile("production", "read_only")));
        QVERIFY(editor);
        auto* tag = window.findChild<QLabel*>("connectionTag");
        QVERIFY(tag && tag->isVisible());
        QCOMPARE(tag->text(), QString("Production"));
        QCOMPARE(tag->property("variant").toString(), QString("error"));
        auto* safety = window.findChild<QComboBox*>("safetyLevel");
        QVERIFY(safety && !safety->isEnabled());
        QCOMPARE(safety->currentData().toString(), QString("read_only"));
        auto* messages = window.findChild<QPlainTextEdit*>("queryMessages");
        editor->setText("CREATE TABLE t (a int)");
        window.findChild<QAction*>("runStatement")->trigger();
        QVERIFY(messages->toPlainText().contains("read-only"));
        editor->setText("SELECT 42");
        window.findChild<QAction*>("runStatement")->trigger();
        auto* grid = window.findChild<QTableView*>("queryResults");
        QTRY_COMPARE(grid->model()->rowCount(), 1);
    }
    void sqlTabsShowTheToneOfTheirConnection() {
        choscordb::MainWindow window;
        auto* editor = connect(window, normalized(profile("production", "read_only")));
        QVERIFY(editor);
        auto* tabs = window.findChild<QTabWidget*>("editorTabs");
        auto* bar = dynamic_cast<choscordb::design::DocumentTabBar*>(tabs->tabBar());
        QVERIFY(bar);
        const int index = tabs->indexOf(editor);
        QTRY_COMPARE(bar->tabTone(index), std::optional(choscordb::design::StatusTone::Danger));
        QCOMPARE(bar->tabTagLabel(index), QString("Production"));
        const auto center = bar->tabRect(index).center();
        QHelpEvent help(QEvent::ToolTip, center, bar->mapToGlobal(center));
        QCoreApplication::sendEvent(bar, &help);
        QCOMPARE(bar->tabToolTip(index), tabs->tabText(index) + " · Production");
        const auto connection =
            window.findChild<choscordb::QueryWorkspace*>()->selectedConnection();
        QVERIFY(connection);
        emit window.objectContextSelected(*connection, R"(["main","sqlite_schema"])",
                                          "sqlite_schema", "table");
        QTRY_COMPARE(tabs->count(), 2);
        QTRY_COMPARE(bar->tabTone(tabs->currentIndex()),
                     std::optional(choscordb::design::StatusTone::Danger));
        editor->setConnectionTarget(std::nullopt);
        QTRY_VERIFY(!bar->tabTone(tabs->indexOf(editor)));
        QVERIFY(bar->tabTagLabel(tabs->indexOf(editor)).isEmpty());
    }
    void aColorOnlyConnectionShowsItsNameInTheTag() {
        choscordb::MainWindow window;
        auto colored = profile("development", "silent");
        colored.environment.clear();
        colored.color = "green";
        QVERIFY(connect(window, normalized(colored)));
        auto* tag = window.findChild<QLabel*>("connectionTag");
        QVERIFY(tag && tag->isVisible());
        QCOMPARE(tag->text(), QString("Tagged"));
        QCOMPARE(tag->property("variant").toString(), QString("success"));
    }
    void confirmAllAsksBeforeEachStatementUntilTheSessionIsSilent() {
        choscordb::MainWindow window;
        auto* editor = connect(window, normalized(profile("development", "confirm_all")));
        QVERIFY(editor);
        QCOMPARE(window.findChild<QLabel*>("connectionTag")->property("variant").toString(),
                 QString("success"));
        editor->setText("SELECT 7");
        {
            const auto prompt = confirmNextPrompt();
            window.findChild<QAction*>("runStatement")->trigger();
            QCOMPARE(prompt.text(),
                     QString("This connection asks for confirmation before each statement."));
        }
        auto* grid = window.findChild<QTableView*>("queryResults");
        QTRY_COMPARE(grid->model()->rowCount(), 1);
        auto* safety = window.findChild<QComboBox*>("safetyLevel");
        QVERIFY(safety->isEnabled());
        const auto silent = safety->findData("silent");
        safety->setCurrentIndex(silent);
        emit safety->activated(silent);
        editor->setText("DROP TABLE IF EXISTS missing");
        window.findChild<QAction*>("runStatement")->trigger();
        QTRY_VERIFY(window.findChild<QPlainTextEdit*>("queryMessages")
                        ->toPlainText()
                        .contains("Completed"));
    }
};

QTEST_MAIN(ConnectionSafetyTest)
#include "connection_safety_test.moc"
