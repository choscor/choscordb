#include "app/main_window.h"
#include "app/query_settings.h"
#include "app/query_workspace.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAction>
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableView>
#include <QtTest>

class QuerySettingsCorruptionTest : public QObject {
    Q_OBJECT
  private slots:
    void corruptSettingsKeepQueriesUsableAndRequireExplicitRepair() {
        const auto path = qEnvironmentVariable("CHOSCORDB_TEST_CORRUPT_QUERY_SETTINGS_DATABASE");
        if (path.isEmpty())
            QSKIP("Run through scripts/integration/query_settings_fixture.py to seed corruption.");

        choscordb::MainWindow window(nullptr, path);
        auto* controller = window.findChild<choscordb::QueryWorkspace*>()
                               ->findChild<choscordb::QuerySettingsController*>();
        auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
        QVERIFY(controller);
        QVERIFY(workspace);
        QSignalSpy failures(controller, &choscordb::QuerySettingsController::failed);
        window.show();
        QTRY_VERIFY(controller->isReady());
        QCOMPARE(failures.count(), 1);
        QVERIFY(!failures.first().at(0).toString().isEmpty());
        QCOMPARE(controller->preferences().pageSize,
                 choscordb::EngineAdapter::queryPreferenceLimits().defaultPageSize);
        QCOMPARE(controller->preferences().timeoutSeconds, quint32(0));

        auto* tabs = window.findChild<QTabWidget*>("editorTabs");
        auto* run = window.findChild<QAction*>("runStatement");
        auto* grid = window.findChild<QTableView*>("queryResults");
        QVERIFY(tabs);
        QVERIFY(run);
        QVERIFY(grid);
        QTRY_VERIFY(tabs->isEnabled());
        workspace->connectSqlite(":memory:");
        QTRY_VERIFY(run->isEnabled());
        int executions = 0;
        connect(
            workspace->adapter(), &choscordb::EngineAdapter::eventReady, &window,
            [&](const choscordb::BridgeEvent& event) {
                if (QString::fromUtf8(event.kind.data(), qsizetype(event.kind.size())) ==
                        "query_state" &&
                    QString::fromUtf8(event.state.data(), qsizetype(event.state.size())) ==
                        "queued")
                    ++executions;
            },
            Qt::DirectConnection);
        auto* editor = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
        QVERIFY(editor);
        editor->setText("WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n "
                        "WHERE x<1001) SELECT x FROM n");
        editor->SendScintilla(QsciScintilla::SCI_GOTOPOS, 0);
        run->trigger();
        QTRY_COMPARE(grid->model()->rowCount(), 1000);
        QCOMPARE(executions, 1);

        auto* settings = window.findChild<QAction*>("querySettings");
        QVERIFY(settings);
        settings->trigger();
        auto* dialog = window.findChild<QDialog*>("querySettingsDialog");
        QVERIFY(dialog);
        auto* apply = dialog->findChild<QPushButton*>("querySettingsApply");
        auto* reset = dialog->findChild<QPushButton*>("querySettingsReset");
        auto* status = dialog->findChild<QLabel*>("querySettingsStatus");
        auto* size = dialog->findChild<QSpinBox*>("queryPageSize");
        QVERIFY(apply);
        QVERIFY(reset);
        QVERIFY(status);
        QVERIFY(size);
        QTRY_VERIFY(reset->isEnabled());
        QVERIFY(!apply->isEnabled());
        QVERIFY(!status->text().isEmpty());
        reset->click();
        QVERIFY(apply->isEnabled());
        size->setValue(222);
        apply->click();
        QTRY_COMPARE(controller->preferences().pageSize, quint32(222));
        QCOMPARE(controller->preferences().timeoutSeconds, quint32(0));
        QCOMPARE(executions, 1);
        QCOMPARE(grid->model()->rowCount(), 1000);
        dialog->close();
    }
};
QTEST_MAIN(QuerySettingsCorruptionTest)
#include "query_settings_corruption_test.moc"
