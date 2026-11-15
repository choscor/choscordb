#include "app/main_window.h"
#include "app/object_data_workspace.h"
#include "app/object_explorer.h"
#include "app/query_workspace.h"
#include "design_system/dialog_presentation/dialog_presentation.h"
#include "design_system/menu/embedded_popup.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAction>
#include <QDateTimeEdit>
#include <QDialog>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTableView>
#include <QTimer>
#include <QtTest>

class GridSetValueTest : public QObject {
    Q_OBJECT
    static bool run(choscordb::MainWindow& window, choscordb::SqlEditor* editor,
                    const QString& sql) {
        auto* action = window.findChild<QAction*>("runStatement");
        auto* messages = window.findChild<QPlainTextEdit*>("queryMessages");
        if (!QTest::qWaitFor([action] { return action->isEnabled(); }))
            return false;
        messages->clear();
        editor->setText(sql);
        action->trigger();
        return QTest::qWaitFor(
            [messages] { return messages->toPlainText().contains("Completed"); });
    }
    static QAction* setValueAction(QTableView* grid, const QModelIndex& cell, const QString& name) {
        QAction* found = nullptr;
        QTimer poll;
        connect(&poll, &QTimer::timeout, grid, [&found, &poll, name] {
            auto* menu = qobject_cast<QMenu*>(choscordb::design::detail::activeEmbeddedPopup());
            if (!menu)
                return;
            poll.stop();
            for (auto* action : menu->actions())
                if (action->menu() && action->menu()->objectName() == "setValueMenu" &&
                    action->isEnabled())
                    found = action->menu()->findChild<QAction*>(name);
            menu->close();
        });
        poll.start(10);
        grid->customContextMenuRequested(grid->visualRect(cell).center());
        return found;
    }

  private slots:
    void setValueStagesDialectExpressionsAndReportsUnsupportedChoices() {
        choscordb::MainWindow window;
        window.show();
        auto* sql = window.findChild<choscordb::QueryWorkspace*>();
        sql->connectSqlite(":memory:");
        QTRY_VERIFY(window.findChild<QAction*>("newQuery")->isEnabled());
        window.findChild<QAction*>("newQuery")->trigger();
        auto* editor = qobject_cast<choscordb::SqlEditor*>(
            window.findChild<QTabWidget*>("editorTabs")->currentWidget());
        QVERIFY(run(window, editor,
                    "CREATE TABLE stamped (id INTEGER PRIMARY KEY, name TEXT, stamp TIMESTAMP);"));
        QVERIFY(run(window, editor, "INSERT INTO stamped VALUES (1, 'before', NULL);"));
        const auto connection = editor->connectionTarget().value_or(0);
        choscordb::ObjectExplorer explorer(sql->adapter());
        auto& data = *new choscordb::ObjectDataWorkspace(sql);
        explorer.installDataWidget(&data);
        connect(&explorer, &choscordb::ObjectExplorer::dataRequested, &data,
                &choscordb::ObjectDataWorkspace::openObject);
        explorer.restoreObject(connection, R"(["main","stamped"])", "stamped", "table");
        explorer.selectPane(5);
        explorer.show();
        explorer.activateRestoredObject();
        auto* grid = data.findChild<QTableView*>("objectDataResults");
        auto* model = grid->model();
        QTRY_COMPARE(model->rowCount(), 1);
        QTRY_VERIFY(model->flags(model->index(0, 2)) & Qt::ItemIsEditable);
        grid->setCurrentIndex(model->index(0, 2));
        grid->findChild<QAction*>("editCell")->trigger();
        auto* sheet = data.window()->findChild<QDialog*>("cellEditSheet");
        QVERIFY(sheet);
        QTRY_VERIFY(sheet->isVisible());
        auto* picker = sheet->findChild<QDateTimeEdit*>("cellEditDateTime");
        QVERIFY(picker && picker->isVisible());
        picker->setDateTime(QDateTime(QDate(2026, 2, 3), QTime(4, 5, 6)));
        QCOMPARE(sheet->findChild<QPlainTextEdit*>("cellEditText")->toPlainText(),
                 QString("2026-02-03 04:05:06"));
        sheet->reject();
        QTRY_VERIFY(!sheet->isVisible());
        grid->setCurrentIndex(model->index(0, 1));
        grid->findChild<QAction*>("editCell")->trigger();
        QTRY_VERIFY(sheet->isVisible());
        QVERIFY(!picker->isVisible());
        sheet->reject();
        QTRY_VERIFY(!sheet->isVisible());
        auto* now = setValueAction(grid, model->index(0, 2), "setValue_now");
        QVERIFY(now);
        now->trigger();
        QCOMPARE(model->index(0, 2).data().toString(), QString("CURRENT_TIMESTAMP"));
        auto* empty = setValueAction(grid, model->index(0, 1), "setValue_empty");
        QVERIFY(empty);
        empty->trigger();
        QCOMPARE(model->index(0, 1).data().toString(), QString());
        auto* fallback = setValueAction(grid, model->index(0, 1), "setValue_default");
        QVERIFY(fallback);
        fallback->trigger();
        QVERIFY(data.findChild<QPlainTextEdit*>("objectDataMessages")
                    ->toPlainText()
                    .contains("SQLite cannot set an existing row to the column default."));
        QString review;
        QTimer reviewPoll;
        connect(&reviewPoll, &QTimer::timeout, &data, [&] {
            auto* dialog =
                qobject_cast<QDialog*>(choscordb::design::DialogPresentation::activeDialog());
            auto* text = dialog ? dialog->findChild<QPlainTextEdit*>("gridEditReview") : nullptr;
            if (!text || !review.isEmpty())
                return;
            review = text->toPlainText();
            dialog->findChild<QPushButton*>("gridEditApply")->click();
        });
        reviewPoll.start(10);
        explorer.findChild<QPushButton*>("objectDataApply")->click();
        QTRY_VERIFY(!review.isEmpty());
        reviewPoll.stop();
        QVERIFY2(review.contains("\"stamp\" = CURRENT_TIMESTAMP"), qPrintable(review));
        QTRY_VERIFY(!explorer.findChild<QPushButton*>("objectDataApply")->isEnabled());
        QVERIFY(run(window, editor,
                    "SELECT name = '' AND stamp IS NOT NULL FROM stamped WHERE id = 1;"));
        auto* results = window.findChild<QTableView*>("queryResults");
        QTRY_COMPARE(results->model()->rowCount(), 1);
        QCOMPARE(results->model()->index(0, 0).data().toString(), QString("1"));
    }
};

QTEST_MAIN(GridSetValueTest)
#include "grid_set_value_test.moc"
