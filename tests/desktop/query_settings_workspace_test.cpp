#include "app/main_window.h"
#include "app/query_settings.h"
#include "app/query_workspace.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
class QuerySettingsWorkspaceTest : public QObject {
    Q_OBJECT
  private slots:
    void changingDefaultsDoesNotExtendAnActiveDeadline() {
        choscordb::MainWindow window;
        window.show();
        auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
        auto* controller = window.findChild<choscordb::QueryWorkspace*>()
                               ->findChild<choscordb::QuerySettingsController*>();
        workspace->connectSqlite(":memory:");
        QTRY_VERIFY(window.findChild<QAction*>("newQuery")->isEnabled());
        window.findChild<QAction*>("newQuery")->trigger();
        auto* run = window.findChild<QAction*>("runStatement");
        QTRY_VERIFY(run->isEnabled());
        auto* settings = window.findChild<QAction*>("querySettings");
        settings->trigger();
        auto* dialog = window.findChild<QDialog*>("querySettingsDialog");
        auto* apply = dialog->findChild<QPushButton*>("querySettingsApply");
        QTRY_VERIFY(apply->isEnabled());
        dialog->findChild<QSpinBox*>("queryTimeoutSeconds")->setValue(2);
        apply->click();
        QTRY_COMPARE(controller->preferences().timeoutSeconds, quint32(2));
        dialog->hide();
        QString failure;
        int executions = 0;
        connect(
            workspace->adapter(), &choscordb::EngineAdapter::eventReady, &window,
            [&](const choscordb::BridgeEvent& event) {
                const auto kind =
                    QString::fromUtf8(event.kind.data(), qsizetype(event.kind.size()));
                if (kind == "query_failed")
                    failure = QString::fromUtf8(event.error_kind.data(),
                                                qsizetype(event.error_kind.size()));
                if (kind == "query_state" &&
                    QString::fromUtf8(event.state.data(), qsizetype(event.state.size())) ==
                        "queued")
                    ++executions;
            },
            Qt::DirectConnection);
        auto* tabs = window.findChild<QTabWidget*>("editorTabs");
        auto* editor = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
        editor->setText("WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE "
                        "x<100000000) SELECT sum(x) FROM n");
        editor->SendScintilla(QsciScintilla::SCI_GOTOPOS, 0);
        run->trigger();
        QTRY_COMPARE(executions, 1);
        settings->trigger();
        dialog->findChild<QSpinBox*>("queryTimeoutSeconds")->setValue(0);
        apply->click();
        QTRY_COMPARE(controller->preferences().timeoutSeconds, quint32(0));
        QVERIFY(failure.isEmpty());
        QCOMPARE(executions, 1);
        dialog->hide();
        QTRY_COMPARE_WITH_TIMEOUT(failure, QString("Timeout"), 5000);
        QTRY_VERIFY(run->isEnabled());
        editor->setText("SELECT 7");
        editor->SendScintilla(QsciScintilla::SCI_GOTOPOS, 0);
        run->trigger();
        QTRY_COMPARE(window.findChild<QTableView*>("queryResults")->model()->rowCount(), 1);
        QCOMPARE(executions, 2);
    }
    void defaultsApplyOnlyToFutureQueriesAndSurviveRestart() {
        QTemporaryDir directory;
        const auto path = directory.filePath("metadata.sqlite");
        {
            choscordb::MainWindow window(nullptr, path);
            window.show();
            auto* settings = window.findChild<QAction*>("querySettings");
            QVERIFY(settings);
            auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
            auto* controller = window.findChild<choscordb::QueryWorkspace*>()
                                   ->findChild<choscordb::QuerySettingsController*>();
            QVERIFY(controller);
            auto* run = window.findChild<QAction*>("runStatement");
            auto* tabs = window.findChild<QTabWidget*>("editorTabs");
            QTRY_VERIFY(tabs->isEnabled());
            workspace->connectSqlite(":memory:");
            QTRY_VERIFY(window.findChild<QAction*>("newQuery")->isEnabled());
            window.findChild<QAction*>("newQuery")->trigger();
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
            editor->setText("WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE "
                            "x<1001) SELECT x FROM n");
            editor->SendScintilla(QsciScintilla::SCI_GOTOPOS, 0);
            run->trigger();
            auto* grid = window.findChild<QTableView*>("queryResults");
            QTRY_COMPARE(grid->model()->rowCount(), 1000);
            QCOMPARE(executions, 1);
            settings->trigger();
            auto* dialog = window.findChild<QDialog*>("querySettingsDialog");
            QVERIFY(dialog);
            auto* apply = dialog->findChild<QPushButton*>("querySettingsApply");
            QTRY_VERIFY(apply->isEnabled());
            dialog->findChild<QSpinBox*>("queryPageSize")->setValue(100);
            dialog->findChild<QSpinBox*>("queryTimeoutSeconds")->setValue(7);
            apply->click();
            QTRY_COMPARE(controller->preferences().pageSize, quint32(100));
            QCOMPARE(executions, 1);
            dialog->hide();
            window.findChild<QPushButton*>("nextPage")->click();
            QTRY_COMPARE(grid->model()->rowCount(), 1);
            window.findChild<QPushButton*>("previousPage")->click();
            QTRY_COMPARE(grid->model()->rowCount(), 1000);
            QTRY_VERIFY(run->isEnabled());
            run->trigger();
            QTRY_COMPARE(executions, 2);
            QTRY_COMPARE(grid->model()->rowCount(), 100);
            dialog->show();
            QVERIFY(dialog->grab().save("native-query-settings.png"));
            dialog->close();
            QTimer answer;
            answer.setInterval(10);
            connect(&answer, &QTimer::timeout, &window, [&] {
                auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                if (!box)
                    return;
                for (auto* button : box->buttons())
                    if (box->buttonRole(button) == QMessageBox::DestructiveRole) {
                        answer.stop();
                        button->click();
                        return;
                    }
            });
            answer.start();
            window.close();
            QTRY_VERIFY(!window.isVisible());
        }
        choscordb::MainWindow restored(nullptr, path);
        restored.show();
        auto* controller = restored.findChild<choscordb::QueryWorkspace*>()
                               ->findChild<choscordb::QuerySettingsController*>();
        QTRY_VERIFY(controller->isReady());
        QCOMPARE(controller->preferences().pageSize, quint32(100));
        QCOMPARE(controller->preferences().timeoutSeconds, quint32(7));
        auto* tabs = restored.findChild<QTabWidget*>("editorTabs");
        QTRY_VERIFY(tabs->isEnabled());
        auto* workspace = restored.findChild<choscordb::QueryWorkspace*>();
        int executions = 0;
        connect(
            workspace->adapter(), &choscordb::EngineAdapter::eventReady, &restored,
            [&](const choscordb::BridgeEvent& event) {
                if (QString::fromUtf8(event.kind.data(), qsizetype(event.kind.size())) ==
                        "query_state" &&
                    QString::fromUtf8(event.state.data(), qsizetype(event.state.size())) ==
                        "queued")
                    ++executions;
            },
            Qt::DirectConnection);
        QSignalSpy connected(workspace, &choscordb::QueryWorkspace::connectionReady);
        workspace->connectSqlite(":memory:");
        QTRY_COMPARE(connected.count(), 1);
        auto* run = restored.findChild<QAction*>("runStatement");
        auto* target = restored.findChild<QComboBox*>("connectionSelector");
        QVERIFY(!target->currentData().isValid());
        QVERIFY(target->placeholderText().contains("Disconnected"));
        QVERIFY(!run->isEnabled());
        QCOMPARE(executions, 0);
        target->setCurrentIndex(target->findData(connected.first().first()));
        QTRY_VERIFY(run->isEnabled());
        QCOMPARE(executions, 0);
        run->trigger();
        QTRY_COMPARE(restored.findChild<QTableView*>("queryResults")->model()->rowCount(), 100);
        QCOMPARE(executions, 1);
    }
};
QTEST_MAIN(QuerySettingsWorkspaceTest)
#include "query_settings_workspace_test.moc"
