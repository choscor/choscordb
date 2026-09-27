#include "app/main_window.h"
#include "app/object_data_workspace.h"
#include "app/object_explorer.h"
#include "app/query_workspace.h"
#include "app/result_filter_bar.h"
#include "bridge/engine_adapter.h"
#include "design_system/dialog_presentation/dialog_presentation.h"
#include "design_system/menu/embedded_popup.h"
#include "design_system/table/table_style.h"
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
#include <QPointer>
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
    void sqlForeignKeyActionOpensIndependentFilteredObjectTab() {
        choscordb::MainWindow window;
        window.show();
        auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
        workspace->connectSqlite(":memory:");
        QTRY_VERIFY(window.findChild<QAction*>("newQuery")->isEnabled());
        window.findChild<QAction*>("newQuery")->trigger();
        auto* tabs = window.findChild<QTabWidget*>("editorTabs");
        auto* editor = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
        auto* run = window.findChild<QAction*>("runStatement");
        auto* messages = window.findChild<QPlainTextEdit*>("queryMessages");
        const auto execute = [&](const QString& statement) {
            QTRY_VERIFY(run->isEnabled());
            messages->clear();
            editor->setText(statement);
            run->trigger();
            QTRY_VERIFY(messages->toPlainText().contains("Completed"));
        };
        execute("CREATE TABLE parent(id INTEGER PRIMARY KEY, name TEXT)");
        execute("CREATE TABLE child(parent_id INTEGER REFERENCES parent(id))");
        execute("WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE x<1005) "
                "INSERT INTO parent SELECT x, 'row ' || x FROM n");
        execute("INSERT INTO child VALUES (1004)");
        execute("SELECT parent_id FROM child");
        auto* source = window.findChild<QTableView*>("queryResults");
        QTRY_COMPARE(source->model()->rowCount(), 1);
        const auto linked = source->model()->index(0, 0);
        QTRY_VERIFY(!linked.data(choscordb::design::ForeignKeyLinkLabelRole).toString().isEmpty());
        source->setCurrentIndex(linked);
        auto* open = source->findChild<QAction*>("resultCellOpenReference");
        QVERIFY(open);
        QTRY_VERIFY(open->isEnabled());
        const int before = tabs->count();
        open->trigger();
        QTRY_COMPARE(tabs->count(), before + 1);
        auto* target = qobject_cast<choscordb::ObjectExplorer*>(tabs->currentWidget());
        QVERIFY(target);
        QCOMPARE(target->paneIndex(), 4);
        auto* filter = target->findChild<QLineEdit*>("resultFilterSql");
        auto* targetGrid = target->findChild<QTableView*>("objectDataResults");
        QVERIFY(filter && targetGrid);
        QTRY_COMPARE(filter->text(), QString("\"id\" = 1004"));
        QTRY_COMPARE(targetGrid->model()->rowCount(), 1);
        QCOMPARE(targetGrid->model()->index(0, 0).data().toString(), QString("1004"));
        QCOMPARE(source->model()->index(0, 0).data().toString(), QString("1004"));
    }
    void stagedForeignKeyOpensEmptyFilteredTabWithoutChangingSource() {
        choscordb::MainWindow window;
        window.show();
        auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
        workspace->connectSqlite(":memory:");
        QTRY_VERIFY(window.findChild<QAction*>("newQuery")->isEnabled());
        window.findChild<QAction*>("newQuery")->trigger();
        auto* tabs = window.findChild<QTabWidget*>("editorTabs");
        auto* editor = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
        auto* run = window.findChild<QAction*>("runStatement");
        auto* messages = window.findChild<QPlainTextEdit*>("queryMessages");
        const auto execute = [&](const QString& statement) {
            QTRY_VERIFY(run->isEnabled());
            messages->clear();
            editor->setText(statement);
            run->trigger();
            QTRY_VERIFY(messages->toPlainText().contains("Completed"));
        };
        execute("CREATE TABLE parent(id INTEGER PRIMARY KEY)");
        execute(
            "CREATE TABLE child(id INTEGER PRIMARY KEY, parent_id INTEGER REFERENCES parent(id))");
        execute("INSERT INTO parent VALUES (1)");
        execute("INSERT INTO child VALUES (7, 1)");
        const auto connection =
            window.findChild<QComboBox*>("connectionSelector")->currentData().toULongLong();
        window.objectContextSelected(connection, R"(["main","child"])", "child", "table");
        auto* child = qobject_cast<choscordb::ObjectExplorer*>(tabs->currentWidget());
        QVERIFY(child);
        auto* grid = child->findChild<QTableView*>("objectDataResults");
        auto* model = qobject_cast<choscordb::ResultTableModel*>(grid->model());
        QTRY_COMPARE(model->rowCount(), 1);
        const auto linked = model->index(0, 1);
        QTRY_VERIFY(linked.flags() & Qt::ItemIsEditable);
        QTRY_VERIFY(!linked.data(choscordb::design::ForeignKeyLinkLabelRole).toString().isEmpty());
        QVERIFY(model->setData(linked, QString("9999")));
        QVERIFY(model->hasPendingEdits());
        grid->setCurrentIndex(linked);
        auto* open = grid->findChild<QAction*>("resultCellOpenReference");
        QVERIFY(open && open->isEnabled());
        const int before = tabs->count();
        open->trigger();
        QTRY_COMPARE(tabs->count(), before + 1);
        auto* parent = qobject_cast<choscordb::ObjectExplorer*>(tabs->currentWidget());
        QVERIFY(parent && parent != child);
        auto* filter = parent->findChild<QLineEdit*>("resultFilterSql");
        auto* parentGrid = parent->findChild<QTableView*>("objectDataResults");
        QVERIFY(filter && parentGrid);
        QTRY_COMPARE(filter->text(), QString("\"id\" = 9999"));
        QTRY_COMPARE(parent->findChild<QLabel*>("objectDataSummary")->property("state").toString(),
                     QString("completed"));
        QCOMPARE(parentGrid->model()->rowCount(), 0);
        QCOMPARE(linked.data().toString(), QString("9999"));
        QVERIFY(model->hasPendingEdits());
    }
    void foreignKeyFilterQuotesIdentifierAndTextValueExactly() {
        choscordb::MainWindow window;
        window.show();
        QTimer confirm;
        connect(&confirm, &QTimer::timeout, &window, [] {
            auto* box =
                qobject_cast<QMessageBox*>(choscordb::design::DialogPresentation::activeDialog());
            if (box && box->windowTitle() == "Confirm SQL execution")
                if (auto* yes = box->button(QMessageBox::Yes))
                    yes->click();
        });
        confirm.start(10);
        auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
        workspace->connectSqlite(":memory:");
        QTRY_VERIFY(window.findChild<QAction*>("newQuery")->isEnabled());
        window.findChild<QAction*>("newQuery")->trigger();
        auto* tabs = window.findChild<QTabWidget*>("editorTabs");
        auto* editor = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
        auto* run = window.findChild<QAction*>("runStatement");
        auto* messages = window.findChild<QPlainTextEdit*>("queryMessages");
        const auto execute = [&](const QString& statement) {
            QTRY_VERIFY(run->isEnabled());
            messages->clear();
            editor->setText(statement);
            run->trigger();
            QTRY_VERIFY(messages->toPlainText().contains("Completed"));
        };
        execute("CREATE TABLE parent(\"key\"\"name\" TEXT PRIMARY KEY)");
        execute("CREATE TABLE child(ref TEXT REFERENCES parent(\"key\"\"name\"))");
        execute("INSERT INTO parent VALUES ('O''Reilly')");
        execute("INSERT INTO child VALUES ('O''Reilly')");
        execute("SELECT ref FROM child");
        auto* source = window.findChild<QTableView*>("queryResults");
        QTRY_COMPARE(source->model()->rowCount(), 1);
        const auto linked = source->model()->index(0, 0);
        QTRY_VERIFY(!linked.data(choscordb::design::ForeignKeyLinkLabelRole).toString().isEmpty());
        source->setCurrentIndex(linked);
        auto* open = source->findChild<QAction*>("resultCellOpenReference");
        QVERIFY(open && open->isEnabled());
        open->trigger();
        auto* target = qobject_cast<choscordb::ObjectExplorer*>(tabs->currentWidget());
        QVERIFY(target);
        auto* filter = target->findChild<QLineEdit*>("resultFilterSql");
        auto* grid = target->findChild<QTableView*>("objectDataResults");
        QVERIFY(filter && grid);
        QTRY_COMPARE(filter->text(), QString("\"key\"\"name\" = 'O''Reilly'"));
        QTRY_COMPARE(grid->model()->rowCount(), 1);
        QCOMPARE(grid->model()->index(0, 0).data().toString(), QString("O'Reilly"));
    }
    void failedReferenceFilterKeepsTargetEmptyAndPagingDisabled() {
        choscordb::MainWindow window;
        window.show();
        QTimer confirm;
        connect(&confirm, &QTimer::timeout, &window, [] {
            auto* box =
                qobject_cast<QMessageBox*>(choscordb::design::DialogPresentation::activeDialog());
            if (box && box->windowTitle() == "Confirm SQL execution")
                if (auto* yes = box->button(QMessageBox::Yes))
                    yes->click();
        });
        confirm.start(10);
        auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
        workspace->connectSqlite(":memory:");
        QTRY_VERIFY(window.findChild<QAction*>("newQuery")->isEnabled());
        window.findChild<QAction*>("newQuery")->trigger();
        auto* tabs = window.findChild<QTabWidget*>("editorTabs");
        auto* editor = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
        auto* run = window.findChild<QAction*>("runStatement");
        auto* messages = window.findChild<QPlainTextEdit*>("queryMessages");
        const auto execute = [&](const QString& statement) {
            QTRY_VERIFY(run->isEnabled());
            messages->clear();
            editor->setText(statement);
            run->trigger();
            QTRY_VERIFY(messages->toPlainText().contains("Completed"));
        };
        execute("CREATE TABLE parent(id TEXT PRIMARY KEY)");
        execute("CREATE TABLE child(ref TEXT REFERENCES parent(id))");
        execute("WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE x<1005) "
                "INSERT INTO parent SELECT CAST(x AS TEXT) FROM n");
        execute("INSERT INTO parent VALUES (char(0) || 'x')");
        execute("INSERT INTO child VALUES (char(0) || 'x')");
        execute("SELECT ref FROM child");
        auto* source = window.findChild<QTableView*>("queryResults");
        auto* model = qobject_cast<choscordb::ResultTableModel*>(source->model());
        QTRY_COMPARE(model->rowCount(), 1);
        QTRY_VERIFY(!model->index(0, 0)
                         .data(choscordb::design::ForeignKeyLinkLabelRole)
                         .toString()
                         .isEmpty());
        source->setCurrentIndex(model->index(0, 0));
        auto* open = source->findChild<QAction*>("resultCellOpenReference");
        QVERIFY(open && open->isEnabled());
        open->trigger();
        auto* target = qobject_cast<choscordb::ObjectExplorer*>(tabs->currentWidget());
        QVERIFY(target);
        auto* targetGrid = target->findChild<QTableView*>("objectDataResults");
        auto* next = target->findChild<QPushButton*>("objectDataNext");
        auto* filter = target->findChild<QLineEdit*>("resultFilterSql");
        auto* error = target->findChild<QLabel*>("resultFilterError");
        QVERIFY(targetGrid && next && filter && error);
        QTRY_COMPARE(target->findChild<QLabel*>("objectDataSummary")->property("state").toString(),
                     QString("failed"));
        QCOMPARE(targetGrid->model()->rowCount(), 0);
        QVERIFY(!next->isEnabled());
        QVERIFY(filter->text().startsWith("\"id\" = '"));
        QVERIFY(filter->text().contains(QChar(0)));
        QVERIFY(error->isVisible());
    }
    void decimalReferenceUsesNumericEqualityOnlyWhenExact() {
        choscordb::MainWindow window;
        window.show();
        auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
        workspace->connectSqlite(":memory:");
        QTRY_VERIFY(window.findChild<QAction*>("newQuery")->isEnabled());
        window.findChild<QAction*>("newQuery")->trigger();
        auto* tabs = window.findChild<QTabWidget*>("editorTabs");
        auto* editor = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
        auto* run = window.findChild<QAction*>("runStatement");
        auto* messages = window.findChild<QPlainTextEdit*>("queryMessages");
        const auto execute = [&](const QString& statement) {
            QTRY_VERIFY(run->isEnabled());
            messages->clear();
            editor->setText(statement);
            run->trigger();
            QTRY_VERIFY(messages->toPlainText().contains("Completed"));
        };
        execute("CREATE TABLE parent(id NUMERIC PRIMARY KEY)");
        execute("CREATE TABLE child(ref NUMERIC REFERENCES parent(id))");
        execute("INSERT INTO parent VALUES (2.5)");
        execute("INSERT INTO child VALUES (2.5)");
        execute("SELECT ref FROM child");
        auto* source = window.findChild<QTableView*>("queryResults");
        auto* model = qobject_cast<choscordb::ResultTableModel*>(source->model());
        QTRY_COMPARE(model->rowCount(), 1);
        choscordb::ResultColumn column{};
        column.name = "ref";
        column.databaseType = "NUMERIC";
        choscordb::ResultCellMetadata metadata;
        metadata.sourceColumn = "ref";
        metadata.sourceObject = R"(["main","child"])";
        metadata.targetObject = R"(["main","parent"])";
        metadata.targetQualifiedName = "parent";
        metadata.targetColumn = "id";
        QVERIFY(model->setPage({column}, {{choscordb::DecimalValue{"2.50"}}}, 0));
        QVERIFY(model->setCellMetadata({metadata}));
        const auto linked = model->index(0, 0);
        QVERIFY(!linked.data(choscordb::design::ForeignKeyLinkLabelRole).toString().isEmpty());
        source->setCurrentIndex(linked);
        auto* open = source->findChild<QAction*>("resultCellOpenReference");
        QVERIFY(open && open->isEnabled());
        open->trigger();
        auto* target = qobject_cast<choscordb::ObjectExplorer*>(tabs->currentWidget());
        QVERIFY(target);
        auto* filter = target->findChild<QLineEdit*>("resultFilterSql");
        auto* grid = target->findChild<QTableView*>("objectDataResults");
        QVERIFY(filter && grid);
        QTRY_COMPARE(filter->text(), QString("\"id\" = 2.50"));
        QTRY_COMPARE(grid->model()->rowCount(), 1);
        QCOMPARE(grid->model()->index(0, 0).data().toString(), QString("2.5"));
        QVERIFY(model->setPage({column}, {{choscordb::DecimalValue{"0.1"}}}, 0));
        QVERIFY(model->setCellMetadata({metadata}));
        QVERIFY(model->index(0, 0).data(choscordb::design::ForeignKeyLinkLabelRole).isValid());
        QVERIFY(model->setPage({column}, {{choscordb::DecimalValue{"0.10000000000000001"}}}, 0));
        QVERIFY(model->setCellMetadata({metadata}));
        QVERIFY(!model->index(0, 0).data(choscordb::design::ForeignKeyLinkLabelRole).isValid());
    }
    void actualResultGridsUseTypedChoiceEditors() {
        choscordb::MainWindow window;
        window.show();
        auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
        workspace->connectSqlite(":memory:");
        QTRY_VERIFY(window.findChild<QAction*>("newQuery")->isEnabled());
        window.findChild<QAction*>("newQuery")->trigger();
        auto* sqlGrid = window.findChild<QTableView*>("queryResults");
        auto* sqlModel = qobject_cast<choscordb::ResultTableModel*>(sqlGrid->model());
        auto* editor = qobject_cast<choscordb::SqlEditor*>(
            window.findChild<QTabWidget*>("editorTabs")->currentWidget());
        auto* run = window.findChild<QAction*>("runStatement");
        QTRY_VERIFY(run->isEnabled());
        editor->setText("SELECT 1");
        run->trigger();
        QTRY_COMPARE(sqlModel->rowCount(), 1);
        QTRY_VERIFY(sqlGrid->isVisible());
        choscordb::ResultColumn flag{};
        flag.name = "enabled";
        flag.databaseType = "boolean";
        QVERIFY(sqlModel->setPage({flag}, {{true}}, 0));
        sqlModel->setEditableColumns({true}, false, false);
        choscordb::ResultCellMetadata boolean;
        boolean.boolean = true;
        boolean.nullable = false;
        QVERIFY(sqlModel->setCellMetadata({boolean}));
        sqlGrid->setCurrentIndex(sqlModel->index(0, 0));
        sqlGrid->edit(sqlModel->index(0, 0));
        auto* boolEditor = sqlGrid->findChild<QComboBox*>("resultCellChoiceEditor");
        QTRY_VERIFY(boolEditor && boolEditor->isVisible());
        QVERIFY(!boolEditor->isEditable());
        QCOMPARE(boolEditor->count(), 2);
        QCOMPARE(boolEditor->itemText(0), QString("true"));
        QCOMPARE(boolEditor->itemText(1), QString("false"));
        QTest::keyClick(boolEditor, Qt::Key_Down);
        QCOMPARE(boolEditor->currentIndex(), 1);
        QTest::keyClick(boolEditor, Qt::Key_Return);
        QTRY_COMPARE(std::get<bool>(sqlModel->rows()[0][0]), false);
        QVERIFY(sqlModel->hasPendingEdits());
        sqlModel->discardEdits();
        QCOMPARE(std::get<bool>(sqlModel->rows()[0][0]), true);

        choscordb::ObjectDataWorkspace objectData(workspace);
        objectData.resize(700, 400);
        objectData.show();
        auto* objectGrid = objectData.findChild<QTableView*>("objectDataResults");
        auto* objectModel = qobject_cast<choscordb::ResultTableModel*>(objectGrid->model());
        choscordb::ResultColumn state{};
        state.name = "state";
        state.databaseType = "status_enum";
        QVERIFY(objectModel->setPage({state}, {{QString("NULL")}}, 0));
        objectModel->setEditableColumns({true}, false, false);
        choscordb::ResultCellMetadata enumeration;
        enumeration.enumChoices = {"ready", "NULL"};
        enumeration.nullable = true;
        QVERIFY(objectModel->setCellMetadata({enumeration}));
        const auto cell = objectModel->index(0, 0);
        objectGrid->setCurrentIndex(cell);
        objectGrid->edit(cell);
        auto* enumEditor = objectGrid->findChild<QComboBox*>("resultCellChoiceEditor");
        QTRY_VERIFY(enumEditor && enumEditor->isVisible());
        QCOMPARE(enumEditor->count(), 3);
        QCOMPARE(enumEditor->currentIndex(), 1);
        QPointer<QComboBox> previousEditor = enumEditor;
        QTest::keyClick(enumEditor, Qt::Key_Escape);
        QTRY_VERIFY(previousEditor.isNull());
        QCOMPARE(std::get<QString>(objectModel->rows()[0][0]), QString("NULL"));
        QVERIFY(!objectModel->hasPendingEdits());
        objectGrid->edit(cell);
        enumEditor = objectGrid->findChild<QComboBox*>("resultCellChoiceEditor");
        QTRY_VERIFY(enumEditor && enumEditor->isVisible());
        QTest::keyClick(enumEditor, Qt::Key_Down);
        QCOMPARE(enumEditor->currentIndex(), 2);
        QTest::keyClick(enumEditor, Qt::Key_Return);
        QTRY_VERIFY(std::holds_alternative<std::monostate>(objectModel->rows()[0][0]));
        QVERIFY(objectModel->hasPendingEdits());
    }
};
QTEST_MAIN(ResultViewWorkspaceTest)
#include "result_view_workspace_test.moc"
