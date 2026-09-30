#include "app/main_window.h"
#include "app/object_data_workspace.h"
#include "app/object_explorer.h"
#include "app/query_workspace.h"
#include "app/result_filter_bar.h"
#include "bridge/engine_adapter.h"
#include "design_system/dialog_presentation/dialog_presentation.h"
#include "design_system/menu/embedded_popup.h"
#include "design_system/toast_region/toast_region.h"
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
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QtTest>
class ObjectDataWorkspaceTest : public QObject {
    Q_OBJECT
    static void triggerTableAction(QTableView* grid, const QString& label) {
        grid->window()->show();
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
    void objectRowJsonOpensForReadOnlyRowsWithoutChangingSelection() {
        choscordb::MainWindow window;
        auto* sql = window.findChild<choscordb::QueryWorkspace*>();
        choscordb::ObjectDataWorkspace data(sql);
        data.resize(700, 400);
        data.show();
        auto* grid = data.findChild<QTableView*>("objectDataResults");
        auto* model = qobject_cast<choscordb::ResultTableModel*>(grid->model());
        QVERIFY(model);
        choscordb::ResultColumn column{};
        column.name = "value";
        QVERIFY(model->setPage({column}, {{QString("first")}, {QString("second")}}, 0));
        grid->setCurrentIndex(model->index(0, 0));
        const auto selected = grid->currentIndex();
        const auto point = grid->visualRect(model->index(1, 0)).center();
        QTimer::singleShot(0, grid, [&] {
            auto* menu = qobject_cast<QMenu*>(choscordb::design::detail::activeEmbeddedPopup());
            if (!menu) {
                if (auto* popup = QApplication::activePopupWidget())
                    popup->close();
                QFAIL("Row context menu did not open");
            }
            auto* action = menu->findChild<QAction*>("viewRowJson");
            const bool enabled =
                action && QTest::qWaitFor([action] { return action->isEnabled(); });
            menu->close();
            QVERIFY(enabled);
            action->trigger();
        });
        grid->customContextMenuRequested(point);
        auto* sheet = data.findChild<QDialog*>("rowJsonSheet");
        QVERIFY(sheet);
        QTRY_VERIFY(sheet->isVisible());
        auto* text = sheet->findChild<QPlainTextEdit*>("rowJsonText");
        QVERIFY(text && text->isReadOnly());
        QTRY_COMPARE(QJsonDocument::fromJson(text->toPlainText().toUtf8())
                         .object()
                         .value("value")
                         .toString(),
                     QString("second"));
        QCOMPARE(grid->currentIndex(), selected);
        sheet->reject();
        QCOMPARE(grid->currentIndex(), selected);
        QCOMPARE(model->rowCount(), 2);
    }

    void objectCellAndTableJsonUseSharedGridActions() {
        choscordb::MainWindow window;
        auto* sql = window.findChild<choscordb::QueryWorkspace*>();
        choscordb::ObjectDataWorkspace data(sql);
        data.resize(700, 400);
        data.show();
        auto* grid = data.findChild<QTableView*>("objectDataResults");
        auto* model = qobject_cast<choscordb::ResultTableModel*>(grid->model());
        QVERIFY(model);
        choscordb::ResultColumn payload{}, plain{};
        payload.name = "payload";
        payload.databaseType = "jsonb";
        plain.name = "plain";
        plain.databaseType = "text";
        QVERIFY(model->setPage(
            {payload, plain},
            {{QString("{\"id\":1}"), QString("[1,2]")}, {QString("{\"id\":2}"), QString("[3,4]")}},
            0));
        grid->setCurrentIndex(model->index(0, 0));
        const auto selected = grid->currentIndex();
        const auto trigger = [&](const char* actionName, const QPoint& point) {
            bool enabled = false;
            QTimer::singleShot(0, grid, [&] {
                auto* menu = qobject_cast<QMenu*>(choscordb::design::detail::activeEmbeddedPopup());
                if (!menu) {
                    if (auto* popup = QApplication::activePopupWidget())
                        popup->close();
                    return;
                }
                auto* action = menu->findChild<QAction*>(actionName);
                enabled = action && QTest::qWaitFor([action] { return action->isEnabled(); });
                menu->close();
                if (enabled)
                    action->trigger();
            });
            grid->customContextMenuRequested(point);
            return enabled;
        };
        QVERIFY(trigger("viewCellJson", grid->visualRect(model->index(1, 0)).center()));
        auto* sheet = data.findChild<QDialog*>("rowJsonSheet");
        QVERIFY(sheet);
        QTRY_VERIFY(sheet->isVisible());
        auto* text = sheet->findChild<QPlainTextEdit*>("rowJsonText");
        QVERIFY(text);
        QTRY_COMPARE(
            QJsonDocument::fromJson(text->toPlainText().toUtf8()).object().value("id").toInt(), 2);
        QCOMPARE(grid->currentIndex(), selected);
        sheet->reject();
        const QPoint blank(grid->viewport()->width() - 2, grid->viewport()->height() - 2);
        QVERIFY(!grid->indexAt(blank).isValid());
        QVERIFY(trigger("viewTableJson", blank));
        QTRY_VERIFY(sheet->isVisible());
        QTRY_VERIFY(QJsonDocument::fromJson(text->toPlainText().toUtf8()).isArray());
        const auto table = QJsonDocument::fromJson(text->toPlainText().toUtf8());
        QVERIFY(table.isArray());
        QCOMPARE(table.array().size(), 2);
        QCOMPARE(table.array().at(0).toObject().value("payload").toObject().value("id").toInt(), 1);
        QCOMPARE(table.array().at(1).toObject().value("plain").toString(), QString("[3,4]"));
        QCOMPARE(grid->currentIndex(), selected);
        sheet->reject();
    }

    void valueLoadFailureUsesWindowToast() {
        choscordb::EngineAdapter adapter;
        QWidget host;
        host.resize(900, 600);
        host.show();
        choscordb::ValueDetailDialog detail(&adapter, &host);
        detail.openValue(777, 888);
        QTRY_VERIFY(
            host.findChild<choscordb::ToastRegion*>("toastRegion", Qt::FindDirectChildrenOnly));
        auto* toast =
            host.findChild<choscordb::ToastRegion*>("toastRegion", Qt::FindDirectChildrenOnly);
        QTRY_VERIFY2(toast->text().contains("Unable to load value"), qPrintable(toast->text()));
        QCOMPARE(toast->property("variant").toString(), QString("danger"));
        QVERIFY(toast->x() >= host.width() / 2);
        QVERIFY(toast->y() >= host.height() / 2);
        QVERIFY(detail.findChild<QLabel*>("valueStatus")->text().isEmpty());
    }

    void resultGridUsesCompactHeaderAndAlternatingRows() {
        choscordb::MainWindow window;
        auto* sql = window.findChild<choscordb::QueryWorkspace*>();
        choscordb::ObjectDataWorkspace data(sql);
        auto* grid = data.findChild<QTableView*>("objectDataResults");
        QVERIFY(grid);
        QCOMPARE(grid->horizontalHeader()->height(), 34);
        auto* model = qobject_cast<choscordb::ResultTableModel*>(grid->model());
        QVERIFY(model);
        choscordb::ResultColumn first{}, second{};
        first.name = "name";
        second.name = "value";
        QVERIFY(model->setPage({first, second}, {{QString("Active"), QString("true")}}, 0));
        QCOMPARE(grid->horizontalHeader()->sectionResizeMode(0), QHeaderView::Interactive);
        QVERIFY(!grid->horizontalHeader()->stretchLastSection());
        QVERIFY(grid->alternatingRowColors());
        QCOMPARE(data.property("designSurface").toString(), QString("panel"));
        auto* queryGrid = window.findChild<QTableView*>("queryResults");
        QVERIFY(queryGrid);
        QCOMPARE(queryGrid->horizontalHeader()->height(), 34);
        QVERIFY(queryGrid->alternatingRowColors());
    }
    void hoveringCellPreservesTheRowBackgroundAndText() {
        choscordb::MainWindow window;
        auto* sql = window.findChild<choscordb::QueryWorkspace*>();
        choscordb::ObjectDataWorkspace data(sql);
        data.resize(500, 240);
        data.show();
        auto* grid = data.findChild<QTableView*>("objectDataResults");
        auto* model = qobject_cast<choscordb::ResultTableModel*>(grid->model());
        choscordb::ResultColumn first{}, second{};
        first.name = "sku";
        second.name = "name";
        QVERIFY(
            model->setPage({first, second}, {{QString("MOU-WLS"), QString("Wireless mouse")}}, 0));
        QCoreApplication::processEvents();

        const auto firstCell = grid->visualRect(model->index(0, 0));
        const auto secondCell = grid->visualRect(model->index(0, 1));
        const auto textLeft = [&] {
            const auto image = grid->viewport()->grab().toImage();
            for (int x = firstCell.left() + 2; x < firstCell.right() - 2; ++x)
                for (int y = firstCell.top() + 2; y < firstCell.bottom() - 2; ++y)
                    if (image.pixelColor(x, y).lightness() < 100)
                        return x;
            return -1;
        };
        const int normalTextLeft = textLeft();
        const auto normal = grid->viewport()->grab().toImage();

        QMouseEvent hover(QEvent::MouseMove, firstCell.center(),
                          grid->viewport()->mapToGlobal(firstCell.center()), Qt::NoButton,
                          Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(grid->viewport(), &hover);
        QCoreApplication::processEvents();
        const auto hovered = grid->viewport()->grab().toImage();
        QCOMPARE(hovered.pixelColor(secondCell.right() - 4, secondCell.center().y()),
                 normal.pixelColor(secondCell.right() - 4, secondCell.center().y()));
        QCOMPARE(textLeft(), normalTextLeft);
    }

    void setNullAppliesToSelectedCells_data() {
        QTest::addColumn<bool>("objectTable");
        QTest::newRow("object") << true;
        QTest::newRow("sql-result") << false;
    }
    void setNullAppliesToSelectedCells() {
        QFETCH(bool, objectTable);
        choscordb::MainWindow window;
        auto* sql = window.findChild<choscordb::QueryWorkspace*>();
        choscordb::ObjectDataWorkspace data(sql);
        auto* grid = objectTable ? data.findChild<QTableView*>("objectDataResults")
                                 : window.findChild<QTableView*>("queryResults");
        QVERIFY(grid);
        auto* model = qobject_cast<choscordb::ResultTableModel*>(grid->model());
        QVERIFY(model);
        choscordb::ResultColumn column{};
        column.name = "value";
        QVERIFY(model->setPage({column, column},
                               {{QString("a"), QString("keep-a")},
                                {QString("b"), QString("keep-b")},
                                {QString("c"), QString("keep-c")}},
                               0));
        model->setEditableColumns({true, false}, true, true);
        grid->setCurrentIndex(model->index(0, 0));
        grid->selectionModel()->select(model->index(2, 0), QItemSelectionModel::Select);
        triggerTableAction(grid, "Set NULL");
        QCOMPARE(model->index(0, 0).data().toString(), QString("NULL"));
        QCOMPARE(model->index(2, 0).data().toString(), QString("NULL"));
        QCOMPARE(model->index(1, 0).data().toString(), QString("b"));
        QCOMPARE(model->index(0, 1).data().toString(), QString("keep-a"));
        QVERIFY(model->hasPendingEdits());
        grid->setCurrentIndex(model->index(0, 1));
        grid->selectionModel()->select(model->index(1, 0), QItemSelectionModel::ClearAndSelect);
        triggerTableAction(grid, "Set NULL");
        QCOMPARE(model->index(1, 0).data().toString(), QString("NULL"));
    }
    void newRowsCanBeRemovedImmediately_data() {
        QTest::addColumn<bool>("canDelete");
        QTest::newRow("insert-only") << false;
        QTest::newRow("editable") << true;
    }
    void newRowsCanBeRemovedImmediately() {
        QFETCH(bool, canDelete);
        choscordb::MainWindow window;
        auto* sql = window.findChild<choscordb::QueryWorkspace*>();
        choscordb::ObjectDataWorkspace data(sql);
        auto* grid = data.findChild<QTableView*>("objectDataResults");
        auto* model = qobject_cast<choscordb::ResultTableModel*>(grid->model());
        choscordb::ResultColumn column{};
        column.name = "value";
        QVERIFY(model->setPage({column}, {{QString("existing")}}, 0));
        model->setEditableColumns({false}, true, canDelete);
        QVERIFY(model->addRow());
        grid->setCurrentIndex(model->index(1, 0));
        QVERIFY(data.findChild<QPushButton*>("objectDataDeleteRows")->isEnabled());
        QVERIFY(!data.findChild<QPushButton*>("objectDataRestoreRows")->isEnabled());
        triggerTableAction(grid, "Delete selected");
        QCOMPARE(model->rowCount(), 1);
        QCOMPARE(model->index(0, 0).data().toString(), QString("existing"));
        QVERIFY(!model->hasPendingEdits());
    }
    void actionsStayAboveResultsAndFooterOnlyContainsPagination() {
        choscordb::MainWindow window;
        auto* sql = window.findChild<choscordb::QueryWorkspace*>();
        choscordb::ObjectDataWorkspace data(sql);
        data.resize(1000, 600);
        data.show();
        QVERIFY(!data.findChild<QPushButton*>("objectDataDiscard"));
        QVERIFY(!data.findChild<QPushButton*>("objectDataRefresh"));
        auto* footer = data.footerWidget();
        QCOMPARE(footer->findChildren<QPushButton*>().size(), 2);
        auto* grid = data.findChild<QTableView*>("objectDataResults");
        for (const char* name : {"objectDataAddRow", "objectDataDeleteRows",
                                 "objectDataRestoreRows", "objectDataSetNull"})
            QVERIFY(!data.findChild<QPushButton*>(name)->isVisible());
        QTimer::singleShot(0, &data, [] {
            auto* menu = qobject_cast<QMenu*>(choscordb::design::detail::activeEmbeddedPopup());
            QVERIFY(menu);
            const auto actions = menu->actions();
            menu->close();
            QStringList labels;
            for (auto* action : actions)
                if (!action->isSeparator())
                    labels << action->text();
            QCOMPARE(labels,
                     QStringList({"Edit cell…", "View cell as JSON", "View row as JSON",
                                  "View table as JSON", "Copy selected cells", "Copy selected rows",
                                  "Copy current page", "Duplicate row", "Add row",
                                  "Delete selected", "Restore selected", "Set NULL", "Cancel"}));
            for (auto* action : actions)
                if (!action->isSeparator())
                    QVERIFY(!action->isEnabled());
        });
        grid->customContextMenuRequested(QPoint(10, 10));
        for (const char* name : {"objectDataExport", "objectDataApply"}) {
            auto* button = data.findChild<QPushButton*>(name);
            QVERIFY(button);
            QVERIFY(!footer->isAncestorOf(button));
            QVERIFY(button->text().isEmpty());
            QVERIFY(!button->icon().isNull());
            QVERIFY(!button->accessibleName().isEmpty());
            QVERIFY(!button->toolTip().isEmpty());
            QVERIFY(button->isVisible());
            QVERIFY(button->mapTo(&data, QPoint()).y() < grid->mapTo(&data, QPoint()).y());
        }
    }
    void explorerKeepsDataActionsVisibleAndDisablesThemOnOtherPanes() {
        choscordb::MainWindow window;
        auto* sql = window.findChild<choscordb::QueryWorkspace*>();
        choscordb::ObjectExplorer explorer(sql->adapter());
        explorer.resize(1000, 600);
        auto* data = new choscordb::ObjectDataWorkspace(sql);
        explorer.installDataWidget(data);
        explorer.restoreObject(1, R"(["main","records"])", "records");
        explorer.selectPane(5);
        explorer.show();
        QCoreApplication::processEvents();
        auto* header = explorer.findChild<QWidget*>("objectHeader");
        auto* footer = explorer.findChild<QWidget*>("objectFooter");
        QCOMPARE(QString::fromLatin1(footer->metaObject()->className()),
                 QString("choscordb::design::StatusLine"));
        auto* sqlSummary = window.findChild<QLabel*>("executionSummary");
        auto* dataSummary = explorer.findChild<QLabel*>("objectDataSummary");
        auto* objectStatus = explorer.findChild<QLabel*>("objectStatus");
        QVERIFY(sqlSummary && dataSummary && objectStatus);
        QCOMPARE(sqlSummary->font().pixelSize(), 13);
        QCOMPARE(footer->font().pixelSize(), sqlSummary->font().pixelSize());
        QCOMPARE(dataSummary->font().pixelSize(), sqlSummary->font().pixelSize());
        QCOMPARE(objectStatus->font().pixelSize(), sqlSummary->font().pixelSize());
        QCOMPARE(footer->findChildren<QPushButton*>().size(), 2);
        for (const char* name : {"objectOpenQuery", "objectGenerateSql", "objectDataCancel"}) {
            auto* control = explorer.findChild<QWidget*>(name);
            QVERIFY2(!control || control->isHidden(), name);
        }
        int right = explorer.findChild<QPushButton*>("objectRefresh")->geometry().right();
        for (const char* name : {"objectDataExport", "objectDataApply"}) {
            auto* action = explorer.findChild<QPushButton*>(name);
            QVERIFY(header->isAncestorOf(action));
            QVERIFY(action->isVisible());
            QVERIFY(action->mapTo(header, QPoint()).x() > right);
            QVERIFY(action->mapTo(header, QPoint()).x() - right < 30);
            right = action->mapTo(header, QPoint()).x() + action->width() - 1;
        }
        QVERIFY(explorer.findChild<QPushButton*>("objectDataCancel")->isHidden());
        QVERIFY(!explorer.findChild<QPushButton*>("objectDataCancel")->isEnabled());
        auto* refresh = explorer.findChild<QPushButton*>("objectRefresh");
        QVERIFY(refresh->isEnabled());
        QSignalSpy requested(&explorer, &choscordb::ObjectExplorer::dataRequested);
        refresh->click();
        QCOMPARE(requested.size(), 1);
        QCOMPARE(requested.at(0).at(1).toString(), QString(R"(["main","records"])"));
        QVERIFY(refresh->isEnabled());
        explorer.setOperationBusy(true);
        QVERIFY(!refresh->isEnabled());
        explorer.setOperationBusy(false);
        QVERIFY(refresh->isEnabled());
        explorer.selectPane(0);
        QVERIFY(!explorer.findChild<QPushButton*>("objectDataAddRow")->isVisible());
        QVERIFY(!footer->findChild<QPushButton*>("objectDataNext")->isVisible());
        QVERIFY(explorer.findChild<QPushButton*>("objectRefresh")->isVisible());
    }
    void binaryOriginalAllowsInsertButPreventsUnsafeRowChanges() {
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
        QTRY_VERIFY(run->isEnabled());
        editor->setText(
            "CREATE TABLE binary_rows(id INTEGER PRIMARY KEY, name TEXT, payload BLOB);");
        run->trigger();
        QTRY_VERIFY(messages->toPlainText().contains("Completed"));
        QTRY_VERIFY(run->isEnabled());
        messages->clear();
        editor->setText("INSERT INTO binary_rows VALUES(1, 'before', x'00ff');");
        run->trigger();
        QTRY_VERIFY(messages->toPlainText().contains("Completed"));
        const auto connection =
            window.findChild<QComboBox*>("connectionSelector")->currentData().toULongLong();
        choscordb::ObjectDataWorkspace data(sql);
        data.show();
        data.openObject(connection, R"(["main","binary_rows"])", "binary_rows");
        auto* grid = data.findChild<QTableView*>("objectDataResults");
        QTRY_COMPARE(grid->model()->rowCount(), 1);
        auto* add = data.findChild<QPushButton*>("objectDataAddRow");
        auto* remove = data.findChild<QPushButton*>("objectDataDeleteRows");
        QTRY_VERIFY(add->isEnabled());
        QVERIFY(!remove->isEnabled());
        QVERIFY(remove->toolTip().contains("Binary"));
        QVERIFY(!(grid->model()->flags(grid->model()->index(0, 1)) & Qt::ItemIsEditable));
    }
    void omittedInsertUsesDefaultAndSetNullStaysDistinct() {
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
        QTRY_VERIFY(run->isEnabled());
        editor->setText(
            "CREATE TABLE default_rows(id INTEGER PRIMARY KEY, name TEXT DEFAULT 'db-default');");
        run->trigger();
        QTRY_VERIFY(messages->toPlainText().contains("Completed"));
        const auto connection =
            window.findChild<QComboBox*>("connectionSelector")->currentData().toULongLong();
        choscordb::ObjectDataWorkspace data(sql);
        data.show();
        data.openObject(connection, R"(["main","default_rows"])", "default_rows");
        auto* grid = data.findChild<QTableView*>("objectDataResults");
        auto* add = data.findChild<QPushButton*>("objectDataAddRow");
        QTRY_VERIFY(add->isEnabled());
        triggerTableAction(grid, "Add row");
        QVERIFY(grid->model()->setData(grid->model()->index(0, 0), QString("3")));
        QCOMPARE(grid->model()->index(0, 1).data().toString(), QString());
        triggerTableAction(grid, "Add row");
        QVERIFY(grid->model()->setData(grid->model()->index(1, 0), QString("4")));
        grid->setCurrentIndex(grid->model()->index(1, 1));
        auto* setNull = data.findChild<QPushButton*>("objectDataSetNull");
        QTRY_VERIFY(setNull->isEnabled());
        triggerTableAction(grid, "Set NULL");
        QVERIFY(grid->model()->index(1, 1).data(Qt::UserRole).toBool());
        QTimer reviewPoll;
        bool reviewed = false;
        connect(&reviewPoll, &QTimer::timeout, &data, [&] {
            auto* dialog =
                qobject_cast<QDialog*>(choscordb::design::DialogPresentation::activeDialog());
            auto* preview = dialog ? dialog->findChild<QPlainTextEdit*>("gridEditReview") : nullptr;
            if (!preview)
                return;
            reviewed = true;
            const auto review = preview->toPlainText();
            QVERIFY(review.contains("DEFAULT") == false);
            QVERIFY(review.contains("NULL"));
            dialog->accept();
        });
        reviewPoll.start(10);
        data.findChild<QPushButton*>("objectDataApply")->click();
        reviewPoll.stop();
        QVERIFY(reviewed);
        editor->setText("SELECT id, name FROM default_rows ORDER BY id;");
        editor->SendScintilla(QsciScintilla::SCI_GOTOPOS, 0);
        QTRY_VERIFY(run->isEnabled());
        run->trigger();
        auto* results = window.findChild<QTableView*>("queryResults");
        QTRY_COMPARE(results->model()->rowCount(), 2);
        QCOMPARE(results->model()->index(0, 1).data().toString(), QString("db-default"));
        QCOMPARE(results->model()->index(1, 1).data(Qt::DisplayRole).toString(), QString("NULL"));
    }
    void duplicateRowOmitsKeyAndRetainsStagingAfterConstraintFailure() {
        choscordb::MainWindow window;
        window.show();
        auto* sql = window.findChild<choscordb::QueryWorkspace*>();
        sql->connectSqlite(":memory:");
        QTRY_VERIFY(window.findChild<QAction*>("newQuery")->isEnabled());
        window.findChild<QAction*>("newQuery")->trigger();
        auto* editor = qobject_cast<choscordb::SqlEditor*>(
            window.findChild<QTabWidget*>("editorTabs")->currentWidget());
        auto* run = window.findChild<QAction*>("runStatement");
        auto* sqlMessages = window.findChild<QPlainTextEdit*>("queryMessages");
        QTRY_VERIFY(run->isEnabled());
        editor->setText("CREATE TABLE duplicate_rows(id INTEGER PRIMARY KEY, name TEXT UNIQUE);");
        run->trigger();
        QTRY_VERIFY(sqlMessages->toPlainText().contains("Completed"));
        QTRY_VERIFY(run->isEnabled());
        sqlMessages->clear();
        editor->setText("INSERT INTO duplicate_rows(name) VALUES('original');");
        run->trigger();
        QTRY_VERIFY(sqlMessages->toPlainText().contains("Completed"));
        const auto connection =
            window.findChild<QComboBox*>("connectionSelector")->currentData().toULongLong();
        choscordb::ObjectDataWorkspace data(sql);
        data.resize(800, 500);
        data.show();
        data.openObject(connection, R"(["main","duplicate_rows"])", "duplicate_rows");
        auto* grid = data.findChild<QTableView*>("objectDataResults");
        auto* messages = data.findChild<QPlainTextEdit*>("objectDataMessages");
        auto* model = qobject_cast<choscordb::ResultTableModel*>(grid->model());
        QTRY_COMPARE(model->rowCount(), 1);
        QTRY_VERIFY(data.findChild<QPushButton*>("objectDataAddRow")->isEnabled());
        auto* filterBar = data.findChild<QWidget*>("resultFilterBar");
        filterBar->findChild<QLineEdit*>("resultFilterSql")->setText("id > 0");
        filterBar->findChild<QPushButton*>("resultFilterApply")->click();
        QVERIFY(!filterBar->findChild<QListWidget*>("resultFilterConditions"));
        QTRY_COMPARE(data.findChild<QLabel*>("objectDataSummary")->property("state").toString(),
                     QString("completed"));
        QTRY_VERIFY(data.findChild<QPushButton*>("objectDataAddRow")->isEnabled());
        QCoreApplication::processEvents();
        triggerTableAction(grid, "Duplicate row");
        QCOMPARE(model->rowCount(), 2);
        QCOMPARE(model->index(1, 0).data().toString(), QString());
        QCOMPARE(model->index(1, 1).data().toString(), QString("original"));
        QTimer::singleShot(0, &data, [] {
            auto* dialog =
                qobject_cast<QMessageBox*>(choscordb::design::DialogPresentation::activeDialog());
            QVERIFY(dialog);
            dialog->reject();
        });
        QTest::mouseClick(grid->horizontalHeader()->viewport(), Qt::LeftButton, {}, QPoint(10, 5));
        QVERIFY(model->hasPendingEdits());
        QVERIFY(!grid->horizontalHeader()->isSortIndicatorShown());
        QTimer::singleShot(0, &data, [] {
            auto* dialog =
                qobject_cast<QMessageBox*>(choscordb::design::DialogPresentation::activeDialog());
            QVERIFY(dialog);
            for (auto* button : dialog->findChildren<QPushButton*>())
                if (button->text() == "Discard") {
                    button->click();
                    return;
                }
            QFAIL("Missing Discard button");
        });
        QTest::mouseClick(grid->horizontalHeader()->viewport(), Qt::LeftButton, {}, QPoint(10, 5));
        QTRY_VERIFY(!model->hasPendingEdits());
        QTRY_VERIFY(grid->horizontalHeader()->isSortIndicatorShown());
        QTRY_COMPARE(grid->horizontalHeader()->sortIndicatorOrder(), Qt::AscendingOrder);
        QTRY_COMPARE(data.findChild<QLabel*>("objectDataSummary")->property("state").toString(),
                     QString("completed"));
        triggerTableAction(grid, "Duplicate row");
        QCOMPARE(model->rowCount(), 2);
        const auto acceptReview = [&data] {
            QTimer reviewPoll;
            bool reviewed = false;
            connect(&reviewPoll, &QTimer::timeout, &data, [&] {
                auto* dialog =
                    qobject_cast<QDialog*>(choscordb::design::DialogPresentation::activeDialog());
                if (!dialog || !dialog->findChild<QPlainTextEdit*>("gridEditReview"))
                    return;
                reviewed = true;
                dialog->accept();
            });
            reviewPoll.start(10);
            data.findChild<QPushButton*>("objectDataApply")->click();
            reviewPoll.stop();
            QVERIFY(reviewed);
        };
        acceptReview();
        QTRY_VERIFY(messages->toPlainText().contains("not applied"));
        QCOMPARE(model->rowCount(), 2);
        QVERIFY(model->hasPendingEdits());
        QVERIFY(model->setData(model->index(1, 1), QString("copy")));
        messages->clear();
        QTimer reviewPoll;
        bool reviewed = false;
        connect(&reviewPoll, &QTimer::timeout, &data, [&] {
            auto* review =
                qobject_cast<QDialog*>(choscordb::design::DialogPresentation::activeDialog());
            if (!review || !review->findChild<QPlainTextEdit*>("gridEditReview"))
                return;
            reviewed = true;
            review->accept();
        });
        reviewPoll.start(10);
        QTimer::singleShot(0, &data, [] {
            auto* pending =
                qobject_cast<QMessageBox*>(choscordb::design::DialogPresentation::activeDialog());
            QVERIFY(pending);
            for (auto* button : pending->findChildren<QPushButton*>())
                if (button->text().startsWith("Apply")) {
                    button->click();
                    return;
                }
            QFAIL("Missing Apply button");
        });
        QTest::mouseClick(grid->horizontalHeader()->viewport(), Qt::LeftButton, {}, QPoint(10, 5));
        reviewPoll.stop();
        QVERIFY(reviewed);
        QTRY_VERIFY(!model->hasPendingEdits());
        QTRY_COMPARE(model->rowCount(), 2);
        QTRY_VERIFY(grid->horizontalHeader()->isSortIndicatorShown());
        QTRY_COMPARE(grid->horizontalHeader()->sortIndicatorOrder(), Qt::DescendingOrder);
        QTRY_COMPARE(data.findChild<QLabel*>("objectDataSummary")->property("state").toString(),
                     QString("completed"));
        QTRY_COMPARE(model->index(0, 0).data().toString(), QString("2"));
        QCOMPARE(model->index(0, 1).data().toString(), QString("copy"));
    }
    void manualTransactionDisablesGridApply() {
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
        QTRY_VERIFY(run->isEnabled());
        editor->setText("CREATE TABLE tx_rows(id INTEGER PRIMARY KEY, name TEXT);");
        run->trigger();
        QTRY_VERIFY(messages->toPlainText().contains("Completed"));
        const auto connection =
            window.findChild<QComboBox*>("connectionSelector")->currentData().toULongLong();
        choscordb::ObjectDataWorkspace data(sql);
        data.show();
        data.openObject(connection, R"(["main","tx_rows"])", "tx_rows");
        auto* add = data.findChild<QPushButton*>("objectDataAddRow");
        QTRY_VERIFY(add->isEnabled());
        add->click();
        auto* apply = data.findChild<QPushButton*>("objectDataApply");
        QTRY_VERIFY(apply->isEnabled());
        window.findChild<QComboBox*>("transactionMode")->setCurrentIndex(1);
        editor->setText("SELECT 1;");
        editor->SendScintilla(QsciScintilla::SCI_GOTOPOS, 0);
        QTRY_VERIFY(run->isEnabled());
        run->trigger();
        QTRY_VERIFY(!apply->isEnabled());
        QVERIFY(apply->toolTip().contains("Commit or roll back"));
    }
    void sqlResultUsesVerifiedSourceColumns() {
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
        auto* grid = window.findChild<QTableView*>("queryResults");
        QTRY_VERIFY(run->isEnabled());
        editor->setText("CREATE TABLE source_rows (id INTEGER PRIMARY KEY, name TEXT);");
        run->trigger();
        QTRY_VERIFY(messages->toPlainText().contains("Completed"));
        QTRY_VERIFY(run->isEnabled());
        messages->clear();
        editor->setText("INSERT INTO source_rows VALUES (1, 'before');");
        run->trigger();
        QTRY_VERIFY(messages->toPlainText().contains("Completed"));
        QTRY_VERIFY(run->isEnabled());
        editor->setText("SELECT id, name AS shown, upper(name) AS computed FROM source_rows;");
        editor->SendScintilla(QsciScintilla::SCI_GOTOPOS, 0);
        run->trigger();
        QTRY_COMPARE(grid->model()->rowCount(), 1);
        QTRY_VERIFY(grid->model()->flags(grid->model()->index(0, 1)) & Qt::ItemIsEditable);
        QVERIFY(!(grid->model()->flags(grid->model()->index(0, 0)) & Qt::ItemIsEditable));
        QVERIFY(!(grid->model()->flags(grid->model()->index(0, 2)) & Qt::ItemIsEditable));
        QVERIFY(grid->model()->setData(grid->model()->index(0, 1), QString("after")));
        auto* apply = window.findChild<QPushButton*>("queryResultApplyEdits");
        QTRY_VERIFY(apply->isEnabled());
        QTimer confirmEdits;
        bool reviewed = false;
        connect(&confirmEdits, &QTimer::timeout, &window, [&] {
            auto* dialog =
                qobject_cast<QDialog*>(choscordb::design::DialogPresentation::activeDialog());
            if (!dialog)
                return;
            auto* review = dialog->findChild<QPlainTextEdit*>("gridEditReview");
            if (!review)
                return;
            QVERIFY(!review->toPlainText().contains("shown"));
            reviewed = true;
            dialog->accept();
        });
        confirmEdits.start(10);
        apply->click();
        QTRY_VERIFY(reviewed);
        confirmEdits.stop();
        QTRY_VERIFY(!apply->isEnabled());
        QTRY_COMPARE(grid->model()->index(0, 1).data().toString(), QString("after"));
        editor->setText("SELECT name FROM source_rows;");
        editor->SendScintilla(QsciScintilla::SCI_GOTOPOS, 0);
        QTRY_VERIFY(run->isEnabled());
        run->trigger();
        QTRY_COMPARE(grid->model()->rowCount(), 1);
        QTRY_VERIFY(!(grid->model()->flags(grid->model()->index(0, 0)) & Qt::ItemIsEditable));
        QTRY_VERIFY(!grid->toolTip().isEmpty());
    }
    void shutdownConfirmsActiveObjectWorkWithoutDiscardingIt() {
        choscordb::MainWindow window;
        window.show();
        auto* sql = window.findChild<choscordb::QueryWorkspace*>();
        sql->connectSqlite(":memory:");
        QTRY_VERIFY(window.findChild<QAction*>("newQuery")->isEnabled());
        window.findChild<QAction*>("newQuery")->trigger();
        auto* run = window.findChild<QAction*>("runStatement");
        auto* editor = qobject_cast<choscordb::SqlEditor*>(
            window.findChild<QTabWidget*>("editorTabs")->currentWidget());
        QTRY_VERIFY(run->isEnabled());
        editor->setText("CREATE VIEW slow_object AS WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL "
                        "SELECT x+1 FROM n WHERE x<1000000000) SELECT sum(x) FROM n;");
        run->trigger();
        QTRY_VERIFY(window.findChild<QPlainTextEdit*>("queryMessages")
                        ->toPlainText()
                        .contains("Completed"));
        QTRY_VERIFY(run->isEnabled());
        const auto connection =
            window.findChild<QComboBox*>("connectionSelector")->currentData().toULongLong();
        choscordb::ObjectDataWorkspace data(sql);
        data.show();
        data.openObject(connection, R"(["main","slow_object"])", "slow_object");
        QVERIFY(!sql->navigationAllowed());
        bool prompted = false;
        QTimer::singleShot(0, &window, [&] {
            auto* dialog =
                qobject_cast<QMessageBox*>(choscordb::design::DialogPresentation::activeDialog());
            if (!dialog)
                return;
            prompted = true;
            dialog->button(QMessageBox::Cancel)->click();
        });
        QVERIFY(!sql->confirmShutdown());
        QVERIFY(prompted);
        QVERIFY(!sql->navigationAllowed());
        auto* cancel = data.findChild<QPushButton*>("objectDataCancel");
        QTRY_VERIFY(cancel->isEnabled());
        cancel->click();
        QTRY_VERIFY(sql->navigationAllowed());
        QVERIFY(sql->confirmShutdown());
    }
    void copyExportAndValueDetailsUseObjectSource() {
        QTemporaryDir directory;
        choscordb::MainWindow window;
        window.show();
        auto* sql = window.findChild<choscordb::QueryWorkspace*>();
        sql->connectSqlite(":memory:");
        QTRY_VERIFY(window.findChild<QAction*>("newQuery")->isEnabled());
        window.findChild<QAction*>("newQuery")->trigger();
        auto* run = window.findChild<QAction*>("runStatement");
        auto* editor = qobject_cast<choscordb::SqlEditor*>(
            window.findChild<QTabWidget*>("editorTabs")->currentWidget());
        auto* sqlTable = window.findChild<QTableView*>("queryResults");
        QTRY_VERIFY(run->isEnabled());
        editor->setText(
            "CREATE TABLE records AS SELECT 7 AS id, 'object' AS txt, zeroblob(70000) AS payload;");
        run->trigger();
        QTRY_VERIFY(window.findChild<QPlainTextEdit*>("queryMessages")
                        ->toPlainText()
                        .contains("Completed"));
        QTRY_VERIFY(run->isEnabled());
        editor->setText("SELECT 99 AS sql_source;");
        editor->SendScintilla(QsciScintilla::SCI_GOTOPOS, 0);
        run->trigger();
        QTRY_COMPARE(sqlTable->model()->rowCount(), 1);
        QCOMPARE(sqlTable->model()->index(0, 0).data().toString(), QString("99"));
        QTRY_VERIFY(run->isEnabled());
        const auto connection =
            window.findChild<QComboBox*>("connectionSelector")->currentData().toULongLong();
        choscordb::ObjectDataWorkspace data(sql);
        data.resize(900, 600);
        data.show();
        data.openObject(connection, R"(["main","records"])", "records");
        auto* table = data.findChild<QTableView*>("objectDataResults");
        QTRY_COMPARE(table->model()->rowCount(), 1);
        QCOMPARE(table->verticalHeader()->sectionSize(0), 35);
        QCOMPARE(table->horizontalHeader()->height(), 34);
        QVERIFY(!table->showGrid());
        QVERIFY(!table->wordWrap());
        table->setCurrentIndex(table->model()->index(0, 0));
        table->setFocus();
        QTest::keySequence(table, QKeySequence::Copy);
        QTRY_COMPARE(QApplication::clipboard()->text(), QString("7"));
        const auto payload = table->model()->index(0, 2);
        table->setCurrentIndex(payload);
        QTest::mouseClick(table->viewport(), Qt::LeftButton, Qt::NoModifier,
                          table->visualRect(payload).center());
        QTest::mouseDClick(table->viewport(), Qt::LeftButton, Qt::NoModifier,
                           table->visualRect(payload).center());
        auto* detail = data.findChild<choscordb::ValueDetailDialog*>();
        QVERIFY(detail);
        QTRY_VERIFY(detail->isVisible());
        QVERIFY(!detail->isModal());
        QTRY_VERIFY(detail->findChild<QLabel*>("valueStatus")->text().contains("70000"));
        auto* preview = detail->findChild<QTableView*>("valuePreview");
        QTRY_VERIFY(preview->model()->rowCount() > 0);
        detail->close();
        auto* exportButton = data.findChild<QPushButton*>("objectDataExport");
        QTRY_VERIFY(exportButton->isEnabled());
        exportButton->click();
        auto* exportDialog = data.findChild<choscordb::ExportDialog*>();
        QVERIFY(exportDialog);
        QCOMPARE(choscordb::design::DialogPresentation::activeDialog(), exportDialog);
        const auto destination = directory.filePath("objects.csv");
        exportDialog->findChild<QLineEdit*>("exportDestination")->setText(destination);
        exportDialog->findChild<QPushButton*>("exportStart")->click();
        QTRY_VERIFY(QFile::exists(destination));
        QTRY_VERIFY(!exportDialog->isRunning());
        QFile file(destination);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto bytes = file.readAll();
        QVERIFY(bytes.startsWith("\"id\",\"txt\",\"payload\"\r\n\"7\",\"object\","));
        exportDialog->close();
        QCOMPARE(sqlTable->model()->index(0, 0).data().toString(), QString("99"));
        data.invalidate();
        QCOMPARE(table->model()->rowCount(), 0);
        QVERIFY(!detail->isVisible());
    }
    void objectPagesKeepTheSqlResultAndItsCursor() {
        choscordb::MainWindow window;
        window.show();
        auto* sql = window.findChild<choscordb::QueryWorkspace*>();
        sql->connectSqlite(":memory:");
        QTRY_VERIFY(window.findChild<QAction*>("newQuery")->isEnabled());
        window.findChild<QAction*>("newQuery")->trigger();
        auto* run = window.findChild<QAction*>("runStatement");
        auto* editor = qobject_cast<choscordb::SqlEditor*>(
            window.findChild<QTabWidget*>("editorTabs")->currentWidget());
        auto* sqlTable = window.findChild<QTableView*>("queryResults");
        QTRY_VERIFY(run->isEnabled());
        editor->setText("CREATE TABLE records AS WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT "
                        "x+1 FROM n WHERE x<1001) SELECT x FROM n;");
        run->trigger();
        QTRY_VERIFY(window.findChild<QPlainTextEdit*>("queryMessages")
                        ->toPlainText()
                        .contains("Completed"));
        QTRY_VERIFY(run->isEnabled());
        editor->setText("WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE "
                        "x<1001) SELECT x+10000 FROM n;");
        editor->SendScintilla(QsciScintilla::SCI_GOTOPOS, 0);
        run->trigger();
        QTRY_VERIFY2(sqlTable->model()->rowCount() == 1000,
                     qPrintable(window.findChild<QPlainTextEdit*>("queryMessages")->toPlainText()));
        QCOMPARE(sqlTable->model()->index(0, 0).data().toString(), QString("10001"));
        QTRY_VERIFY(run->isEnabled());
        const auto draft = editor->text();
        const auto connection =
            window.findChild<QComboBox*>("connectionSelector")->currentData().toULongLong();
        choscordb::ObjectDataWorkspace data(sql);
        data.show();
        data.openObject(connection, R"(["main","records"])", "records");
        auto* objectTable = data.findChild<QTableView*>("objectDataResults");
        QVERIFY(objectTable);
        QTRY_COMPARE(objectTable->model()->rowCount(), 1000);
        QCOMPARE(objectTable->model()->index(0, 0).data().toString(), QString("1"));
        QVERIFY(
            !(objectTable->model()->flags(objectTable->model()->index(0, 0)) & Qt::ItemIsEditable));
        QCOMPARE(sqlTable->model()->index(0, 0).data().toString(), QString("10001"));
        QCOMPARE(editor->text(), draft);
        auto* next = data.findChild<QPushButton*>("objectDataNext");
        QVERIFY(next);
        QTRY_VERIFY(next->isEnabled());
        next->click();
        QTRY_COMPARE(objectTable->model()->rowCount(), 1);
        QCOMPARE(objectTable->model()->index(0, 0).data().toString(), QString("1001"));
        auto* sqlNext = window.findChild<QPushButton*>("nextPage");
        QTRY_VERIFY(sqlNext->isEnabled());
        sqlNext->click();
        QTRY_VERIFY2(sqlTable->model()->rowCount() == 1,
                     qPrintable(window.findChild<QPlainTextEdit*>("queryMessages")->toPlainText()));
        QCOMPARE(sqlTable->model()->index(0, 0).data().toString(), QString("11001"));
        data.invalidate();
        QCOMPARE(objectTable->model()->rowCount(), 0);
        QCOMPARE(sqlTable->model()->index(0, 0).data().toString(), QString("11001"));
    }
};
QTEST_MAIN(ObjectDataWorkspaceTest)
#include "object_data_workspace_test.moc"
