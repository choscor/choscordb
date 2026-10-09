#include "app/main_window.h"
#include "app/object_data_workspace.h"
#include "app/query_workspace.h"
#include "bridge/engine_adapter.h"
#include "design_system/dialog_presentation/dialog_presentation.h"
#include "design_system/menu/embedded_popup.h"
#include "models/result_table_model.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTableView>
#include <QTimer>
#include <QtTest>

using namespace choscordb;

class CellEditSqlWorkspaceTest : public QObject {
    Q_OBJECT

    static bool openCell(QTableView* grid, const QModelIndex& target) {
        bool opened = false;
        QTimer::singleShot(0, grid, [&] {
            auto* menu = qobject_cast<QMenu*>(design::detail::activeEmbeddedPopup());
            if (!menu)
                return;
            const auto actions = menu->actions();
            menu->close();
            if (actions.size() < 2 || actions[0]->objectName() != "editCell" ||
                actions[0]->text() != QStringLiteral("Edit cell…") || !actions[1]->isSeparator() ||
                !actions[0]->isEnabled())
                return;
            actions[0]->trigger();
            opened = true;
        });
        grid->customContextMenuRequested(grid->visualRect(target).center());
        return opened;
    }

    static bool cellActionMatches(QTableView* grid, const QPoint& position, bool enabled) {
        bool matches = false;
        QTimer::singleShot(0, grid, [&] {
            auto* menu = qobject_cast<QMenu*>(design::detail::activeEmbeddedPopup());
            if (!menu)
                return;
            const auto actions = menu->actions();
            menu->close();
            matches = actions.size() >= 2 && actions[0]->objectName() == "editCell" &&
                      actions[0]->text() == QStringLiteral("Edit cell…") &&
                      actions[1]->isSeparator() && actions[0]->isEnabled() == enabled;
        });
        grid->customContextMenuRequested(position);
        return matches;
    }

    static bool reviewEdits(QPushButton* apply, QWidget* owner, bool accept,
                            QString* reviewText = nullptr) {
        bool reviewed = false;
        QTimer poll;
        connect(&poll, &QTimer::timeout, owner, [&] {
            auto* dialog = qobject_cast<QDialog*>(design::DialogPresentation::activeDialog());
            auto* review = dialog ? dialog->findChild<QPlainTextEdit*>("gridEditReview") : nullptr;
            if (!review)
                return;
            reviewed = true;
            if (reviewText)
                *reviewText = review->toPlainText();
            accept ? dialog->accept() : dialog->reject();
        });
        poll.start(10);
        apply->click();
        poll.stop();
        return reviewed;
    }

  private slots:
    void realDisconnectDiscardsDraftAndDisablesTheOldResult() {
        MainWindow window;
        window.show();
        auto* sql = window.findChild<QueryWorkspace*>();
        sql->connectSqlite(":memory:");
        QTRY_VERIFY(window.findChild<QAction*>("newQuery")->isEnabled());
        window.findChild<QAction*>("newQuery")->trigger();
        auto* editor =
            qobject_cast<SqlEditor*>(window.findChild<QTabWidget*>("editorTabs")->currentWidget());
        auto* run = window.findChild<QAction*>("runStatement");
        auto* messages = window.findChild<QPlainTextEdit*>("queryMessages");
        const auto execute = [&](const QString& statement) {
            QTRY_VERIFY(run->isEnabled());
            messages->clear();
            editor->setText(statement);
            editor->SendScintilla(QsciScintilla::SCI_GOTOPOS, 0);
            run->trigger();
            QTRY_VERIFY(messages->toPlainText().contains("Completed"));
        };
        execute("CREATE TABLE disconnect_rows(id INTEGER PRIMARY KEY, note TEXT)");
        execute("INSERT INTO disconnect_rows VALUES (1, 'original')");
        const auto connection =
            window.findChild<QComboBox*>("connectionSelector")->currentData().toULongLong();
        ObjectDataWorkspace data(sql);
        data.resize(850, 500);
        data.show();
        data.openObject(connection, R"(["main","disconnect_rows"])", "disconnect_rows");
        auto* grid = data.findChild<QTableView*>("objectDataResults");
        auto* model = qobject_cast<ResultTableModel*>(grid->model());
        QTRY_COMPARE(model->rowCount(), 1);
        QTRY_VERIFY(model->index(0, 1).flags() & Qt::ItemIsEditable);
        QVERIFY(openCell(grid, model->index(0, 1)));
        auto* sheet = data.findChild<QDialog*>("cellEditSheet");
        QVERIFY(sheet && sheet->isVisible());
        auto* text = sheet->findChild<QPlainTextEdit*>("cellEditText");
        auto* save = sheet->findChild<QPushButton*>("cellEditSave");
        text->selectAll();
        QTest::keyClicks(text, "obsolete connection draft");
        QVERIFY(!model->hasPendingEdits());
        QVERIFY(sql->adapter()->disconnectConnection(connection));
        QTRY_COMPARE(data.findChild<QLabel*>("objectDataSummary")->property("state").toString(),
                     QString("failed"));
        QVERIFY(data.findChild<QLabel*>("objectDataSummary")
                    ->accessibleDescription()
                    .contains("Connection closed"));
        QTRY_VERIFY(!sheet->isVisible());
        QVERIFY(text->toPlainText().isEmpty());
        save->click();
        QVERIFY(!sheet->isVisible());
        QVERIFY(!model->hasPendingEdits());
        QCOMPARE(model->index(0, 1).data().toString(), QString("original"));
        QVERIFY(cellActionMatches(grid, grid->visualRect(model->index(0, 1)).center(), false));
        QVERIFY(!data.findChild<QPushButton*>("objectDataApply")->isEnabled());
    }

    void realEligibilityKeepsExcludedTargetsDisabled() {
        MainWindow window;
        window.show();
        auto* sql = window.findChild<QueryWorkspace*>();
        sql->connectSqlite(":memory:");
        QTRY_VERIFY(window.findChild<QAction*>("newQuery")->isEnabled());
        window.findChild<QAction*>("newQuery")->trigger();
        auto* editor =
            qobject_cast<SqlEditor*>(window.findChild<QTabWidget*>("editorTabs")->currentWidget());
        auto* run = window.findChild<QAction*>("runStatement");
        auto* messages = window.findChild<QPlainTextEdit*>("queryMessages");
        const auto execute = [&](const QString& statement) {
            QTRY_VERIFY(run->isEnabled());
            messages->clear();
            editor->setText(statement);
            editor->SendScintilla(QsciScintilla::SCI_GOTOPOS, 0);
            run->trigger();
            QTRY_VERIFY(messages->toPlainText().contains("Completed"));
        };
        execute("CREATE TABLE policy_rows(id INTEGER PRIMARY KEY, note TEXT, "
                "calculated TEXT GENERATED ALWAYS AS (note || '-calculated') STORED)");
        execute("INSERT INTO policy_rows(id, note) VALUES (1, 'scalar')");
        execute("CREATE TABLE binary_rows(id INTEGER PRIMARY KEY, note TEXT, payload BLOB)");
        execute("INSERT INTO binary_rows VALUES (1, 'scalar', x'00ff')");
        execute("CREATE TABLE deferred_rows(id INTEGER PRIMARY KEY, note TEXT, payload TEXT)");
        execute("INSERT INTO deferred_rows VALUES (1, 'scalar', replace(hex(zeroblob(35000)), '0', "
                "'x'))");
        execute("CREATE VIEW policy_view AS SELECT id, note FROM policy_rows");
        const auto connection =
            window.findChild<QComboBox*>("connectionSelector")->currentData().toULongLong();
        ObjectDataWorkspace data(sql);
        data.resize(900, 600);
        data.show();
        auto* grid = data.findChild<QTableView*>("objectDataResults");
        auto* model = qobject_cast<ResultTableModel*>(grid->model());
        auto* add = data.findChild<QPushButton*>("objectDataAddRow");
        auto* result = data.findChild<QueryWorkspace*>();
        data.openObject(connection, R"(["main","policy_rows"])", "policy_rows");
        QTRY_COMPARE(model->rowCount(), 1);
        QTRY_VERIFY(model->index(0, 1).flags() & Qt::ItemIsEditable);
        QVERIFY(cellActionMatches(grid, grid->visualRect(model->index(0, 1)).center(), true));
        QVERIFY(cellActionMatches(grid, grid->visualRect(model->index(0, 0)).center(), false));
        QVERIFY(cellActionMatches(grid, grid->visualRect(model->index(0, 2)).center(), false));
        const QPoint blank = grid->viewport()->rect().bottomRight() - QPoint(5, 5);
        QVERIFY(!grid->indexAt(blank).isValid());
        QVERIFY(cellActionMatches(grid, blank, false));
        model->markDeleted({model->index(0, 1)}, true);
        QVERIFY(model->deleted()[0]);
        QVERIFY(cellActionMatches(grid, grid->visualRect(model->index(0, 1)).center(), false));
        model->discardEdits();

        QTRY_VERIFY(sql->navigationAllowed());
        QTRY_VERIFY(result->navigationAllowed());
        data.openObject(connection, R"(["main","binary_rows"])", "binary_rows");
        QTRY_COMPARE(model->rowCount(), 1);
        QTRY_VERIFY(add->isEnabled());
        QVERIFY(std::holds_alternative<QByteArray>(model->rows()[0][2]));
        QVERIFY(!(model->index(0, 1).flags() & Qt::ItemIsEditable));
        QVERIFY(cellActionMatches(grid, grid->visualRect(model->index(0, 1)).center(), false));
        QVERIFY(cellActionMatches(grid, grid->visualRect(model->index(0, 2)).center(), false));

        QTRY_VERIFY(sql->navigationAllowed());
        QTRY_VERIFY(result->navigationAllowed());
        data.openObject(connection, R"(["main","deferred_rows"])", "deferred_rows");
        QTRY_COMPARE(model->rowCount(), 1);
        QTRY_VERIFY(add->isEnabled());
        QVERIFY(std::holds_alternative<DeferredValue>(model->rows()[0][2]));
        QVERIFY(!(model->index(0, 1).flags() & Qt::ItemIsEditable));
        QVERIFY(cellActionMatches(grid, grid->visualRect(model->index(0, 1)).center(), false));
        QVERIFY(cellActionMatches(grid, grid->visualRect(model->index(0, 2)).center(), false));

        QTRY_VERIFY(sql->navigationAllowed());
        QTRY_VERIFY(result->navigationAllowed());
        data.openObject(connection, R"(["main","policy_view"])", "policy_view");
        QTRY_COMPARE(model->rowCount(), 1);
        QTRY_VERIFY(result->navigationAllowed());
        QVERIFY(!(model->index(0, 1).flags() & Qt::ItemIsEditable));
        QVERIFY(cellActionMatches(grid, grid->visualRect(model->index(0, 1)).center(), false));

        execute("SELECT count(*) AS count FROM policy_rows");
        auto* sqlGrid = window.findChild<QTableView*>("queryResults");
        QTRY_COMPARE(sqlGrid->model()->rowCount(), 1);
        QTRY_VERIFY(sql->navigationAllowed());
        QVERIFY(!(sqlGrid->model()->index(0, 0).flags() & Qt::ItemIsEditable));
        QVERIFY(cellActionMatches(
            sqlGrid, sqlGrid->visualRect(sqlGrid->model()->index(0, 0)).center(), false));
        QVERIFY(!data.findChild<QDialog*>("cellEditSheet"));
        QVERIFY(!window.findChild<QDialog*>("cellEditSheet"));
    }

    void typedNullAndOmittedDefaultsStayDistinctFromExplicitTextAfterApply() {
        MainWindow window;
        window.show();
        auto* sql = window.findChild<QueryWorkspace*>();
        sql->connectSqlite(":memory:");
        QTRY_VERIFY(window.findChild<QAction*>("newQuery")->isEnabled());
        window.findChild<QAction*>("newQuery")->trigger();
        auto* editor =
            qobject_cast<SqlEditor*>(window.findChild<QTabWidget*>("editorTabs")->currentWidget());
        auto* run = window.findChild<QAction*>("runStatement");
        auto* messages = window.findChild<QPlainTextEdit*>("queryMessages");
        const auto execute = [&](const QString& statement) {
            QTRY_VERIFY(run->isEnabled());
            messages->clear();
            editor->setText(statement);
            editor->SendScintilla(QsciScintilla::SCI_GOTOPOS, 0);
            run->trigger();
            QTRY_VERIFY(messages->toPlainText().contains("Completed"));
        };
        execute("CREATE TABLE typed_sheet_rows(id INTEGER PRIMARY KEY, note TEXT DEFAULT "
                "'db-default')");
        execute("INSERT INTO typed_sheet_rows VALUES (1, NULL), (2, NULL), (3, NULL)");
        const auto connection =
            window.findChild<QComboBox*>("connectionSelector")->currentData().toULongLong();
        ObjectDataWorkspace data(sql);
        data.resize(900, 650);
        data.show();
        data.openObject(connection, R"(["main","typed_sheet_rows"])", "typed_sheet_rows");
        auto* grid = data.findChild<QTableView*>("objectDataResults");
        auto* model = qobject_cast<ResultTableModel*>(grid->model());
        auto* add = data.findChild<QPushButton*>("objectDataAddRow");
        auto* apply = data.findChild<QPushButton*>("objectDataApply");
        QTRY_COMPARE(model->rowCount(), 3);
        QTRY_VERIFY(model->index(0, 1).flags() & Qt::ItemIsEditable);

        QVERIFY(openCell(grid, model->index(0, 1)));
        auto* sheet = data.findChild<QDialog*>("cellEditSheet");
        QVERIFY(sheet && sheet->isVisible());
        auto* text = sheet->findChild<QPlainTextEdit*>("cellEditText");
        auto* status = sheet->findChild<QLabel*>("cellEditStatus");
        auto* save = sheet->findChild<QPushButton*>("cellEditSave");
        QCOMPARE(text->toPlainText(), QString());
        QVERIFY(status->text().contains("SQL NULL"));
        save->click();
        QTRY_VERIFY(!sheet->isVisible());
        QVERIFY(std::holds_alternative<std::monostate>(model->rows()[0][1]));
        QVERIFY(!model->hasPendingEdits());

        for (int row = 1; row <= 2; ++row) {
            QVERIFY(openCell(grid, model->index(row, 1)));
            QCOMPARE(text->toPlainText(), QString());
            QVERIFY(status->text().contains("SQL NULL"));
            QTest::keyClicks(text, row == 1 ? "x" : "NULL");
            if (row == 1)
                QTest::keyClick(text, Qt::Key_Backspace);
            save->click();
            QTRY_VERIFY(!sheet->isVisible());
            QVERIFY(std::holds_alternative<QString>(model->rows()[row][1]));
            QCOMPARE(std::get<QString>(model->rows()[row][1]),
                     row == 1 ? QString() : QString("NULL"));
        }
        for (int row = 3; row <= 5; ++row) {
            QTRY_VERIFY(add->isEnabled());
            add->click();
            QCOMPARE(model->rowCount(), row + 1);
            QVERIFY(model->setData(model->index(row, 0), QString::number(row + 1)));
            QVERIFY(openCell(grid, model->index(row, 1)));
            QCOMPARE(text->toPlainText(), QString());
            QVERIFY(status->text().contains("default", Qt::CaseInsensitive));
            QVERIFY(status->text().contains("omitted", Qt::CaseInsensitive));
            if (row != 3) {
                QTest::keyClicks(text, row == 4 ? "x" : "NULL");
                if (row == 4)
                    QTest::keyClick(text, Qt::Key_Backspace);
            }
            save->click();
            QTRY_VERIFY(!sheet->isVisible());
            QCOMPARE(model->touched()[row][1], row != 3);
        }

        QTRY_VERIFY(apply->isEnabled());
        QVERIFY(reviewEdits(apply, &data, true));
        QTRY_VERIFY(!model->hasPendingEdits());
        QTRY_VERIFY(sql->navigationAllowed());
        execute("SELECT id, note, typeof(note), note IS NULL, length(note) "
                "FROM typed_sheet_rows ORDER BY id");
        auto* database = window.findChild<QTableView*>("queryResults");
        QTRY_COMPARE(database->model()->rowCount(), 6);
        auto* databaseModel = qobject_cast<ResultTableModel*>(database->model());
        QVERIFY(std::holds_alternative<std::monostate>(databaseModel->rows()[0][1]));
        QCOMPARE(database->model()->index(0, 2).data().toString(), QString("null"));
        QCOMPARE(database->model()->index(0, 3).data().toString(), QString("1"));
        const QStringList expected = {QString(), "NULL", "db-default", QString(), "NULL"};
        for (int row = 1; row <= 5; ++row) {
            QCOMPARE(database->model()->index(row, 0).data().toString(), QString::number(row + 1));
            QCOMPARE(database->model()->index(row, 1).data().toString(), expected[row - 1]);
            QCOMPARE(database->model()->index(row, 2).data().toString(), QString("text"));
            QCOMPARE(database->model()->index(row, 3).data().toString(), QString("0"));
            QCOMPARE(database->model()->index(row, 4).data().toString(),
                     QString::number(expected[row - 1].size()));
        }
    }

    void sqlAliasSheetUsesVerifiedColumnAndPreservesCanceledReview() {
        MainWindow window;
        window.resize(1000, 750);
        window.show();
        auto* sql = window.findChild<QueryWorkspace*>();
        sql->connectSqlite(":memory:");
        QTRY_VERIFY(window.findChild<QAction*>("newQuery")->isEnabled());
        window.findChild<QAction*>("newQuery")->trigger();
        auto* editor =
            qobject_cast<SqlEditor*>(window.findChild<QTabWidget*>("editorTabs")->currentWidget());
        auto* run = window.findChild<QAction*>("runStatement");
        auto* messages = window.findChild<QPlainTextEdit*>("queryMessages");
        const auto execute = [&](const QString& statement) {
            QTRY_VERIFY(run->isEnabled());
            messages->clear();
            editor->setText(statement);
            editor->SendScintilla(QsciScintilla::SCI_GOTOPOS, 0);
            run->trigger();
            QTRY_VERIFY(messages->toPlainText().contains("Completed"));
        };
        execute("CREATE TABLE sql_sheet_rows(id INTEGER PRIMARY KEY, note TEXT)");
        execute("INSERT INTO sql_sheet_rows VALUES (1, 'first'), (2, 'second')");
        execute(
            "SELECT id, note AS shown, upper(note) AS computed FROM sql_sheet_rows ORDER BY id");
        auto* grid = window.findChild<QTableView*>("queryResults");
        auto* model = qobject_cast<ResultTableModel*>(grid->model());
        auto* apply = window.findChild<QPushButton*>("queryResultApplyEdits");
        QTRY_COMPARE(model->rowCount(), 2);
        QTRY_VERIFY(model->index(1, 1).flags() & Qt::ItemIsEditable);
        QVERIFY(!(model->index(1, 2).flags() & Qt::ItemIsEditable));
        QVERIFY(model->setData(model->index(0, 1), "first staged"));
        grid->setCurrentIndex(model->index(0, 1));
        QVERIFY(openCell(grid, model->index(1, 1)));
        auto* sheet = window.findChild<QDialog*>("cellEditSheet");
        QVERIFY(sheet && sheet->isVisible());
        auto* text = sheet->findChild<QPlainTextEdit*>("cellEditText");
        QCOMPARE(text->toPlainText(), QString("second"));
        text->selectAll();
        QTest::keyClicks(text, "saved via alias");
        QCOMPARE(model->index(1, 1).data().toString(), QString("second"));
        sheet->findChild<QPushButton*>("cellEditSave")->click();
        QTRY_VERIFY(!sheet->isVisible());
        QCOMPARE(model->index(1, 1).data().toString(), QString("saved via alias"));
        QCOMPARE(model->index(0, 1).data().toString(), QString("first staged"));
        QTRY_VERIFY(apply->isEnabled());

        const auto connection =
            window.findChild<QComboBox*>("connectionSelector")->currentData().toULongLong();
        ObjectDataWorkspace observer(sql);
        observer.resize(850, 500);
        observer.show();
        observer.openObject(connection, R"(["main","sql_sheet_rows"])", "sql_sheet_rows");
        auto* database = observer.findChild<QTableView*>("objectDataResults");
        QTRY_COMPARE(database->model()->rowCount(), 2);
        QCOMPARE(database->model()->index(0, 1).data().toString(), QString("first"));
        QCOMPARE(database->model()->index(1, 1).data().toString(), QString("second"));
        QString review;
        QVERIFY(reviewEdits(apply, &window, false, &review));
        QVERIFY(review.contains("UPDATE"));
        QVERIFY(review.contains("\"note\""));
        QVERIFY(!review.contains("\"shown\""));
        QVERIFY(!review.contains("\"computed\""));
        QVERIFY(model->hasPendingEdits());
        QCOMPARE(model->index(1, 1).data().toString(), QString("saved via alias"));
        observer.openObject(connection, R"(["main","sql_sheet_rows"])", "sql_sheet_rows");
        QTRY_COMPARE(observer.findChild<QLabel*>("objectDataSummary")->property("state").toString(),
                     QString("completed"));
        QCOMPARE(database->model()->index(1, 1).data().toString(), QString("second"));

        QVERIFY(reviewEdits(apply, &window, true));
        QTRY_VERIFY(!model->hasPendingEdits());
        QTRY_VERIFY(!apply->isEnabled());
        QTRY_VERIFY(sql->navigationAllowed());
        observer.openObject(connection, R"(["main","sql_sheet_rows"])", "sql_sheet_rows");
        QTRY_COMPARE(database->model()->index(0, 1).data().toString(), QString("first staged"));
        QTRY_COMPARE(database->model()->index(1, 1).data().toString(), QString("saved via alias"));
    }

    void tableSheetSaveRemainsLocalUntilReviewedApply() {
        MainWindow window;
        window.show();
        auto* sql = window.findChild<QueryWorkspace*>();
        sql->connectSqlite(":memory:");
        QTRY_VERIFY(window.findChild<QAction*>("newQuery")->isEnabled());
        window.findChild<QAction*>("newQuery")->trigger();
        auto* editor =
            qobject_cast<SqlEditor*>(window.findChild<QTabWidget*>("editorTabs")->currentWidget());
        auto* run = window.findChild<QAction*>("runStatement");
        auto* messages = window.findChild<QPlainTextEdit*>("queryMessages");
        auto* sqlGrid = window.findChild<QTableView*>("queryResults");
        const auto execute = [&](const QString& statement) {
            QTRY_VERIFY(run->isEnabled());
            messages->clear();
            editor->setText(statement);
            editor->SendScintilla(QsciScintilla::SCI_GOTOPOS, 0);
            run->trigger();
            QTRY_VERIFY(messages->toPlainText().contains("Completed"));
        };
        execute("CREATE TABLE sheet_rows(id INTEGER PRIMARY KEY, note TEXT)");
        execute("INSERT INTO sheet_rows VALUES (1, 'first'), (2, 'second')");
        const auto connection =
            window.findChild<QComboBox*>("connectionSelector")->currentData().toULongLong();
        ObjectDataWorkspace data(sql);
        data.resize(900, 600);
        data.show();
        data.openObject(connection, R"(["main","sheet_rows"])", "sheet_rows");
        auto* grid = data.findChild<QTableView*>("objectDataResults");
        auto* model = qobject_cast<ResultTableModel*>(grid->model());
        auto* apply = data.findChild<QPushButton*>("objectDataApply");
        QTRY_COMPARE(model->rowCount(), 2);
        QTRY_VERIFY(model->index(1, 1).flags() & Qt::ItemIsEditable);
        QVERIFY(model->setData(model->index(0, 1), "other staged"));
        grid->setCurrentIndex(model->index(0, 1));
        QVERIFY(openCell(grid, model->index(1, 1)));
        auto* sheet = data.findChild<QDialog*>("cellEditSheet");
        QVERIFY(sheet && sheet->isVisible());
        auto* text = sheet->findChild<QPlainTextEdit*>("cellEditText");
        QCOMPARE(text->toPlainText(), QString("second"));
        text->selectAll();
        QTest::keyClicks(text, "  saved");
        QTest::keyClick(text, Qt::Key_Return);
        QTest::keyClicks(text, "line 'two'  ");
        const QString saved = "  saved\nline 'two'  ";
        QCOMPARE(text->toPlainText(), saved);
        QCOMPARE(model->index(1, 1).data().toString(), QString("second"));
        sheet->findChild<QPushButton*>("cellEditSave")->click();
        QTRY_VERIFY(!sheet->isVisible());
        QCOMPARE(model->index(1, 1).data(Qt::EditRole).toString(), saved);
        QCOMPARE(model->index(0, 1).data().toString(), QString("other staged"));
        QVERIFY(model->hasPendingEdits());
        QTRY_VERIFY(apply->isEnabled());
        QVERIFY(!design::DialogPresentation::activeDialog());

        execute("SELECT note FROM sheet_rows ORDER BY id");
        QTRY_COMPARE(sqlGrid->model()->rowCount(), 2);
        QCOMPARE(sqlGrid->model()->index(0, 0).data().toString(), QString("first"));
        QCOMPARE(sqlGrid->model()->index(1, 0).data().toString(), QString("second"));
        QString review;
        QVERIFY(reviewEdits(apply, &data, false, &review));
        QVERIFY(review.contains("UPDATE"));
        QCOMPARE(model->index(1, 1).data(Qt::EditRole).toString(), saved);
        QVERIFY(model->hasPendingEdits());
        execute("SELECT note FROM sheet_rows ORDER BY id");
        QCOMPARE(sqlGrid->model()->index(1, 0).data().toString(), QString("second"));

        QVERIFY(reviewEdits(apply, &data, true));
        QTRY_VERIFY(!model->hasPendingEdits());
        QTRY_VERIFY(!apply->isEnabled());
        execute("SELECT note FROM sheet_rows ORDER BY id");
        QCOMPARE(sqlGrid->model()->index(0, 0).data().toString(), QString("other staged"));
        QCOMPARE(sqlGrid->model()->index(1, 0).data(Qt::EditRole).toString(), saved);
    }
};

QTEST_MAIN(CellEditSqlWorkspaceTest)
#include "cell_edit_sql_workspace_test.moc"
