#include "app/main_window.h"
#include "app/object_data_workspace.h"
#include "app/object_explorer.h"
#include "app/query_workspace.h"
#include "app/result_filter_bar.h"
#include "bridge/engine_adapter.h"
#include "design_system/menu/embedded_popup.h"
#include "models/result_table_model.h"
#include "widgets/export_dialog/export_dialog.h"
#include "widgets/sql_editor/sql_editor.h"
#include "widgets/value_detail_dialog/value_detail_dialog.h"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
class ResultViewWorkspaceTest : public QObject {
    Q_OBJECT
    static void triggerTableAction(QTableView* grid, const QString& label) {
        QTimer::singleShot(0, grid, [label] {
            auto* menu = qobject_cast<QMenu*>(choscordb::design::detail::activeEmbeddedPopup());
            QVERIFY(menu);
            const auto actions = menu->actions();
            menu->close();
            for (auto* action : actions) {
                if (action->text() == label) {
                    QVERIFY(action->isEnabled());
                    action->trigger();
                    return;
                }
            }
            QFAIL(qPrintable("Missing table action: " + label));
        });
        grid->customContextMenuRequested(QPoint(10, 10));
    }

  private slots:
    void duplicateRowUsesTheRowUnderThePointer() {
        choscordb::MainWindow window;
        auto* sql = window.findChild<choscordb::QueryWorkspace*>();
        choscordb::ObjectDataWorkspace data(sql);
        data.resize(800, 500);
        data.show();
        auto* grid = data.findChild<QTableView*>("objectDataResults");
        auto* model = qobject_cast<choscordb::ResultTableModel*>(grid->model());
        choscordb::ResultColumn id{}, name{};
        id.name = "id";
        name.name = "name";
        QVERIFY(model->setPage(
            {id, name}, {{qint64(1), QString("clicked")}, {qint64(2), QString("selected")}}, 0));
        model->setEditableColumns({false, true}, true, true, {false, true});
        grid->setCurrentIndex(model->index(1, 1));
        QCoreApplication::processEvents();
        triggerTableAction(grid, "Duplicate row");
        QCOMPARE(model->rowCount(), 3);
        QVERIFY(model->inserted()[2]);
        QCOMPARE(model->index(2, 0).data().toString(), QString());
        QCOMPARE(model->index(2, 1).data().toString(), QString("clicked"));
    }
    void tableHeadersDoNotOpenParentMenus() {
        choscordb::MainWindow window;
        auto* sql = window.findChild<choscordb::QueryWorkspace*>();
        choscordb::ObjectExplorer explorer(sql->adapter());
        auto* data = new choscordb::ObjectDataWorkspace(sql);
        explorer.installDataWidget(data);
        explorer.selectPane(4);
        explorer.resize(800, 500);
        explorer.show();
        auto* grid = data->findChild<QTableView*>("objectDataResults");
        auto* model = qobject_cast<choscordb::ResultTableModel*>(grid->model());
        choscordb::ResultColumn column{};
        column.name = "value";
        QVERIFY(model->setPage({column}, {{QString("a")}}, 0));
        for (auto* header : {grid->horizontalHeader(), grid->verticalHeader()}) {
            bool menuSeen = false;
            QTimer closePopup;
            connect(&closePopup, &QTimer::timeout, &explorer, [&] {
                if (auto* menu =
                        qobject_cast<QMenu*>(choscordb::design::detail::activeEmbeddedPopup())) {
                    menuSeen = true;
                    menu->close();
                }
            });
            closePopup.start(10);
            auto* viewport = header->viewport();
            const QPoint point(5, 5);
            QContextMenuEvent event(QContextMenuEvent::Mouse, point, viewport->mapToGlobal(point));
            QApplication::sendEvent(viewport, &event);
            QCoreApplication::processEvents();
            QVERIFY(!menuSeen);
        }
    }
    void onlyObjectDataExposesFilterAndSortControls() {
        choscordb::MainWindow window;
        auto* sql = window.findChild<choscordb::QueryWorkspace*>();
        choscordb::ObjectDataWorkspace data(sql);
        window.show();
        data.show();
        auto* sqlGrid = window.findChild<QTableView*>("queryResults");
        QVERIFY(!sqlGrid->parentWidget()->findChild<QWidget*>("resultFilterBar"));
        QVERIFY(!sqlGrid->horizontalHeader()->sectionsClickable());
        QVERIFY(!sqlGrid->horizontalHeader()->isSortIndicatorShown());
        QVERIFY(data.findChild<QTableView*>("objectDataResults")
                    ->horizontalHeader()
                    ->sectionsClickable());
        for (auto* surface : QList<QWidget*>{&data}) {
            auto* bar = surface->findChild<QWidget*>("resultFilterBar");
            QVERIFY(bar);
            QVERIFY(bar->isVisible());
            QVERIFY(bar->findChild<QLineEdit*>("resultFilterSql"));
            QVERIFY(!bar->findChild<QPushButton*>("resultFilterMode"));
            QVERIFY(!bar->findChild<QComboBox*>("resultFilterColumn"));
            QVERIFY(!bar->findChild<QComboBox*>("resultFilterOperator"));
            QVERIFY(!bar->findChild<QLineEdit*>("resultFilterValue"));
            QVERIFY(!bar->findChild<QPushButton*>("resultFilterAdd"));
            QVERIFY(!bar->findChild<QListWidget*>("resultFilterConditions"));
            QVERIFY(bar->findChild<QPushButton*>("resultFilterApply"));
            QVERIFY(bar->findChild<QPushButton*>("resultFilterClear"));
        }
    }
    void sqlResultHeaderClicksPreserveQueryOrder() {
        choscordb::MainWindow window;
        window.show();
        auto* sql = window.findChild<choscordb::QueryWorkspace*>();
        sql->connectSqlite(":memory:");
        QTRY_VERIFY(window.findChild<QAction*>("newQuery")->isEnabled());
        window.findChild<QAction*>("newQuery")->trigger();
        auto* editor = qobject_cast<choscordb::SqlEditor*>(
            window.findChild<QTabWidget*>("editorTabs")->currentWidget());
        auto* run = window.findChild<QAction*>("runStatement");
        QTRY_VERIFY(run->isEnabled());
        editor->setText("SELECT 3 AS value UNION ALL SELECT 1 UNION ALL SELECT 2");
        run->trigger();
        auto* grid = window.findChild<QTableView*>("queryResults");
        QTRY_COMPARE(grid->model()->rowCount(), 3);
        auto* header = grid->horizontalHeader();
        QSignalSpy clicks(header, &QHeaderView::sectionClicked);
        QTest::mouseClick(header->viewport(), Qt::LeftButton, {}, QPoint(10, 5));
        QCOMPARE(clicks.count(), 0);
        QVERIFY(!header->isSortIndicatorShown());
        QCOMPARE(grid->model()->index(0, 0).data().toString(), QString("3"));
        QCOMPARE(grid->model()->index(1, 0).data().toString(), QString("1"));
        QCOMPARE(grid->model()->index(2, 0).data().toString(), QString("2"));
        QVERIFY(!grid->parentWidget()->findChild<QWidget*>("resultFilterBar"));
    }
    void sqlFilterValidatesAndRestoresAppliedDraft() {
        choscordb::ResultFilterBar bar;
        choscordb::ResultColumn column{};
        column.name = "name";
        column.databaseType = "TEXT";
        bar.setColumns({column});
        bar.show();
        auto* sql = bar.findChild<QLineEdit*>("resultFilterSql");
        auto* apply = bar.findChild<QPushButton*>("resultFilterApply");
        auto* clear = bar.findChild<QPushButton*>("resultFilterClear");
        auto* error = bar.findChild<QLabel*>("resultFilterError");
        QSignalSpy requested(&bar, &choscordb::ResultFilterBar::applyRequested);
        apply->click();
        QCOMPARE(requested.count(), 0);
        QVERIFY(error->isVisible());
        sql->setText("name LIKE 'B%' OR name = 'Alice'");
        apply->click();
        QCOMPARE(requested.count(), 1);
        QCOMPARE(bar.conditions().size(), 1);
        QCOMPARE(bar.conditions().first().operation, QString("sql"));
        QCOMPARE(bar.conditions().first().value, QString("name LIKE 'B%' OR name = 'Alice'"));
        QVERIFY(!error->isVisible());
        bar.markApplied(bar.conditions());
        QVERIFY(!bar.findChild<QListWidget*>("resultFilterConditions"));
        sql->setText("name = 'Bob'");
        bar.setBusy(true);
        QVERIFY(!sql->isEnabled());
        QVERIFY(!apply->isEnabled());
        bar.setColumns({column});
        bar.setBusy(false);
        bar.restoreApplied();
        QCOMPARE(sql->text(), QString("name LIKE 'B%' OR name = 'Alice'"));
        QVERIFY(clear->isEnabled());
        for (auto* button : {apply, clear}) {
            QVERIFY(button->text().isEmpty());
            QVERIFY(!button->icon().isNull());
            QVERIFY(!button->accessibleName().isEmpty());
        }
        QSignalSpy cleared(&bar, &choscordb::ResultFilterBar::clearRequested);
        clear->click();
        QCOMPARE(cleared.count(), 1);
        QVERIFY(sql->text().isEmpty());
    }
    void objectDataFiltersAndSortsTheCompletePagedResult() {
        choscordb::MainWindow window;
        window.show();
        auto* sql = window.findChild<choscordb::QueryWorkspace*>();
        sql->connectSqlite(":memory:");
        QTRY_VERIFY(window.findChild<QAction*>("newQuery")->isEnabled());
        window.findChild<QAction*>("newQuery")->trigger();
        auto* editor = qobject_cast<choscordb::SqlEditor*>(
            window.findChild<QTabWidget*>("editorTabs")->currentWidget());
        auto* run = window.findChild<QAction*>("runStatement");
        auto* messages = window.findChild<QPlainTextEdit*>("queryMessages");
        const auto execute = [&](const QString& statement) {
            QTRY_VERIFY(run->isEnabled());
            messages->clear();
            editor->setText(statement);
            run->trigger();
            QTRY_VERIFY(messages->toPlainText().contains("Completed"));
        };
        execute("CREATE TABLE paged_rows(x INTEGER, label TEXT)");
        execute("WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE x<1005) "
                "INSERT INTO paged_rows SELECT x, CASE WHEN x % 2 = 0 THEN 'ALPHA' ELSE 'beta' END "
                "FROM n");
        const auto connection =
            window.findChild<QComboBox*>("connectionSelector")->currentData().toULongLong();
        choscordb::ObjectDataWorkspace data(sql);
        data.resize(900, 600);
        data.show();
        data.openObject(connection, R"(["main","paged_rows"])", "paged_rows");
        auto* grid = data.findChild<QTableView*>("objectDataResults");
        auto* summary = data.findChild<QLabel*>("objectDataSummary");
        auto* next = data.findChild<QPushButton*>("objectDataNext");
        auto* filterBar = grid->parentWidget()->findChild<QWidget*>("resultFilterBar");
        auto* sqlFilter = filterBar->findChild<QLineEdit*>("resultFilterSql");
        auto* apply = filterBar->findChild<QPushButton*>("resultFilterApply");
        auto* clear = filterBar->findChild<QPushButton*>("resultFilterClear");
        QTRY_COMPARE(grid->model()->rowCount(), 1000);
        sqlFilter->setText("x > 3");
        apply->click();
        QTRY_COMPARE(grid->model()->index(0, 0).data().toString(), QString("4"));
        QTRY_COMPARE(grid->model()->rowCount(), 1000);
        QVERIFY(!filterBar->findChild<QListWidget*>("resultFilterConditions"));
        const int x = grid->horizontalHeader()->sectionViewportPosition(0) + 10;
        QTest::mouseClick(grid->horizontalHeader()->viewport(), Qt::LeftButton, {}, QPoint(x, 5));
        QTRY_VERIFY(grid->horizontalHeader()->isSortIndicatorShown());
        QTRY_COMPARE(grid->horizontalHeader()->sortIndicatorOrder(), Qt::AscendingOrder);
        QTRY_COMPARE(summary->property("state").toString(), QString("completed"));
        grid->selectionModel()->select(grid->model()->index(0, 0),
                                       QItemSelectionModel::ClearAndSelect);
        triggerTableAction(grid, "Copy selected rows");
        QCOMPARE(QApplication::clipboard()->text(), QString("4\tALPHA"));
        QTRY_VERIFY(next->isEnabled());
        next->click();
        QTRY_COMPARE(grid->model()->rowCount(), 2);
        QCOMPARE(grid->model()->index(1, 0).data().toString(), QString("1005"));
        data.findChild<QPushButton*>("objectDataPrevious")->click();
        QTRY_COMPARE(grid->model()->rowCount(), 1000);
        QTest::mouseClick(grid->horizontalHeader()->viewport(), Qt::LeftButton, {}, QPoint(x, 5));
        QTRY_COMPARE(grid->model()->index(0, 0).data().toString(), QString("1005"));
        QTRY_COMPARE(summary->property("state").toString(), QString("completed"));
        clear->click();
        QTRY_COMPARE(grid->model()->rowCount(), 1000);
        QCOMPARE(grid->model()->index(0, 0).data().toString(), QString("1005"));
        QTRY_COMPARE(summary->property("state").toString(), QString("completed"));
        QTest::mouseClick(grid->horizontalHeader()->viewport(), Qt::LeftButton, {}, QPoint(x, 5));
        QTRY_COMPARE(grid->model()->index(0, 0).data().toString(), QString("1"));
        QVERIFY(!grid->horizontalHeader()->isSortIndicatorShown());
        execute("CREATE TABLE replacement(value INTEGER)");
        execute("INSERT INTO replacement VALUES (9)");
        data.openObject(connection, R"(["main","replacement"])", "replacement");
        QTRY_COMPARE(grid->model()->rowCount(), 1);
        QTRY_COMPARE(grid->model()->index(0, 0).data().toString(), QString("9"));
        QVERIFY(!clear->isEnabled());
    }
    void objectDataUsesSqlFilterAndKeepsAppliedResultsOnError() {
        choscordb::MainWindow window;
        window.show();
        auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
        workspace->connectSqlite(":memory:");
        QTRY_VERIFY(window.findChild<QAction*>("newQuery")->isEnabled());
        window.findChild<QAction*>("newQuery")->trigger();
        auto* editor = qobject_cast<choscordb::SqlEditor*>(
            window.findChild<QTabWidget*>("editorTabs")->currentWidget());
        auto* run = window.findChild<QAction*>("runStatement");
        auto* messages = window.findChild<QPlainTextEdit*>("queryMessages");
        const auto execute = [&](const QString& statement) {
            QTRY_VERIFY(run->isEnabled());
            messages->clear();
            editor->setText(statement);
            run->trigger();
            QTRY_VERIFY(messages->toPlainText().contains("Completed"));
        };
        execute("CREATE TABLE typed_rows(id INTEGER PRIMARY KEY, amount NUMERIC)");
        execute("INSERT INTO typed_rows VALUES (1, 1.5), (2, 2)");
        const auto connection =
            window.findChild<QComboBox*>("connectionSelector")->currentData().toULongLong();
        choscordb::ObjectDataWorkspace data(workspace);
        data.resize(900, 600);
        data.show();
        data.openObject(connection, R"(["main","typed_rows"])", "typed_rows");
        auto* grid = data.findChild<QTableView*>("objectDataResults");
        auto* filterBar = data.findChild<QWidget*>("resultFilterBar");
        auto* sqlFilter = filterBar->findChild<QLineEdit*>("resultFilterSql");
        auto* apply = filterBar->findChild<QPushButton*>("resultFilterApply");
        auto* clear = filterBar->findChild<QPushButton*>("resultFilterClear");
        auto* error = filterBar->findChild<QLabel*>("resultFilterError");
        QTRY_COMPARE(grid->model()->rowCount(), 2);
        sqlFilter->setText("id = 2 AND amount IN (1.5, 2)");
        apply->click();
        QTRY_COMPARE(grid->model()->rowCount(), 1);
        QCOMPARE(grid->model()->index(0, 0).data().toString(), QString("2"));
        QVERIFY(!filterBar->findChild<QListWidget*>("resultFilterConditions"));
        sqlFilter->setText("id = (");
        apply->click();
        QTRY_VERIFY(error->isVisible());
        QCOMPARE(sqlFilter->text(), QString("id = ("));
        QCOMPARE(grid->model()->index(0, 0).data().toString(), QString("2"));
        sqlFilter->setText("id = 1");
        apply->click();
        QTRY_COMPARE(grid->model()->index(0, 0).data().toString(), QString("1"));
        clear->click();
        QTRY_COMPARE(grid->model()->rowCount(), 2);
        QVERIFY(sqlFilter->text().isEmpty());
    }
};
QTEST_MAIN(ResultViewWorkspaceTest)
#include "result_view_workspace_test.moc"
