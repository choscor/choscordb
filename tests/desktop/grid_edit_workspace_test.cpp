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
#include <tuple>
class GridEditWorkspaceTest : public QObject {
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
    void editChangedDuringPlanningNeverReviewsStaleSnapshot() {
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
        execute("CREATE TABLE planning_rows (id INTEGER PRIMARY KEY, name TEXT)");
        execute("WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE x<500) "
                "INSERT INTO planning_rows SELECT x, 'before' FROM n");
        const auto connection =
            window.findChild<QComboBox*>("connectionSelector")->currentData().toULongLong();
        choscordb::ObjectDataWorkspace data(sql);
        data.show();
        data.openObject(connection, R"(["main","planning_rows"])", "planning_rows");
        auto* grid = data.findChild<QTableView*>("objectDataResults");
        auto* model = qobject_cast<choscordb::ResultTableModel*>(grid->model());
        auto* result = data.findChild<choscordb::QueryWorkspace*>();
        auto* apply = data.findChild<QPushButton*>("objectDataApply");
        auto* summary = data.findChild<QLabel*>("objectDataSummary");
        QAction* cancelAction = nullptr;
        for (auto* action : data.findChildren<QAction*>())
            if (action->text() == QStringLiteral("Cancel")) {
                cancelAction = action;
                break;
            }
        QVERIFY(model && result && apply && summary && cancelAction);
        QTRY_COMPARE(model->rowCount(), 500);
        QTRY_VERIFY(model->flags(model->index(0, 1)) & Qt::ItemIsEditable);
        QVERIFY(model->setData(model->index(0, 1), QString("first edit")));
        QTRY_VERIFY(apply->isEnabled());

        bool planningStarted = false;
        bool reviewShown = false;
        bool disconnectDialogShown = false;
        bool disconnectIgnored = false;
        bool cancelIgnored = false;
        bool changed = false;
        connect(result, &choscordb::QueryWorkspace::gridEditPlanningChanged, &data,
                [&](bool planning) {
                    if (!planning)
                        return;
                    planningStarted = true;
                    QTimer::singleShot(0, &data, [&] {
                        result->disconnectConnection(connection);
                        disconnectIgnored = result->selectedTargetAvailable();
                        cancelAction->setEnabled(true);
                        cancelAction->trigger();
                        cancelIgnored = summary->property("state") != QStringLiteral("cancelling");
                        changed = model->setData(model->index(0, 1), QString("second edit"));
                        if (auto* dialog = qobject_cast<QDialog*>(
                                choscordb::design::DialogPresentation::activeDialog());
                            dialog && dialog->findChild<QPlainTextEdit*>("gridEditReview")) {
                            reviewShown = true;
                            dialog->reject();
                        }
                    });
                });
        QTimer reviewGuard;
        connect(&reviewGuard, &QTimer::timeout, &data, [&] {
            if (auto* dialog =
                    qobject_cast<QDialog*>(choscordb::design::DialogPresentation::activeDialog())) {
                if (dialog->windowTitle() == QStringLiteral("Disconnect database session")) {
                    disconnectDialogShown = true;
                    dialog->reject();
                } else if (dialog->findChild<QPlainTextEdit*>("gridEditReview")) {
                    reviewShown = true;
                    dialog->reject();
                }
            }
        });
        reviewGuard.start(10);
        apply->click();
        reviewGuard.stop();
        QVERIFY(planningStarted);
        QVERIFY(changed);
        QVERIFY(disconnectIgnored);
        QVERIFY(cancelIgnored);
        QVERIFY(!disconnectDialogShown);
        QVERIFY(!reviewShown);
        QCOMPARE(model->index(0, 1).data().toString(), QString("second edit"));
        QVERIFY(model->hasPendingEdits());
    }
    void gridEditPlannerUsesTypedNullSafePredicatesAndRejectsUnsafeNames() {
        choscordb::GridEditRequest request;
        request.driver = "sqlite";
        request.qualifiedName = "\"main\".\"orders\"";
        request.parameterStyle = "?";
        choscordb::GridEditColumn id;
        id.name = id.resultName = "id";
        id.databaseType = "integer";
        id.key = true;
        choscordb::GridEditColumn label;
        label.name = label.resultName = "label";
        label.databaseType = "text";
        request.columns = {id, label};
        choscordb::GridEditRow row;
        row.current = {qint64(7), QString("after")};
        row.original = {qint64(7), QString("before")};
        row.touched = {false, true};
        request.rows = {row};

        const auto eligibility = choscordb::EngineAdapter::gridEditability(request);
        QCOMPARE(eligibility.editable, (std::vector<bool>{false, true}));
        QVERIFY(eligibility.canDelete);
        const auto plan = choscordb::EngineAdapter::planGridEdits(request);
        QVERIFY2(plan.error.isEmpty(), qPrintable(plan.error));
        QCOMPARE(plan.statements.size(), size_t(1));
        QCOMPARE(plan.statements[0].sql, QString("UPDATE \"main\".\"orders\" SET \"label\" = ? "
                                                 "WHERE \"id\" IS ? AND \"label\" IS ?"));
        QCOMPARE(plan.statements[0].params.size(), size_t(3));
        QCOMPARE(std::get<QString>(plan.statements[0].params[0]), QString("after"));
        QCOMPARE(plan.statements[0].expectedRows, std::optional<quint64>(1));

        request.driver = "postgres";
        request.qualifiedName = "\"public\".\"orders\"";
        request.parameterStyle = "$";
        const auto postgres = choscordb::EngineAdapter::planGridEdits(request);
        QVERIFY2(postgres.error.isEmpty(), qPrintable(postgres.error));
        QCOMPARE(
            postgres.statements[0].sql,
            QString("UPDATE \"public\".\"orders\" SET \"label\" = $1 WHERE \"id\" IS NOT DISTINCT "
                    "FROM $2 AND \"label\" IS NOT DISTINCT FROM $3"));

        request.driver = "mysql";
        request.qualifiedName = "`shop`.`orders`";
        request.parameterStyle = "?";
        const auto mysql = choscordb::EngineAdapter::planGridEdits(request);
        QVERIFY2(mysql.error.isEmpty(), qPrintable(mysql.error));
        QCOMPARE(
            mysql.statements[0].sql,
            QString("UPDATE `shop`.`orders` SET `label` = ? WHERE `id` <=> ? AND `label` <=> ?"));

        request.rows[0].current[1] = choscordb::DeferredValue{4, 100, "text"};
        const auto deferred = choscordb::EngineAdapter::gridEditability(request);
        QVERIFY(deferred.canInsert);
        QVERIFY(!deferred.canDelete);
        request.rows[0].current[1] = QString("after");
        request.rows[0].deleted = true;
        request.rows[0].original[1] = choscordb::FallbackText{"opaque", "text"};
        const auto opaque = choscordb::EngineAdapter::planGridEdits(request);
        QVERIFY(!opaque.error.isEmpty());
        QVERIFY(opaque.statements.empty());

        request.rows[0] = row;
        request.qualifiedName = "main.orders";
        const auto unsafe = choscordb::EngineAdapter::planGridEdits(request);
        QVERIFY(!unsafe.error.isEmpty());
        QVERIFY(unsafe.statements.empty());
    }

    void gridPlanPreservesRustTypedDateTimeAndJsonParameters() {
        choscordb::GridEditRequest request;
        request.driver = "postgres";
        request.qualifiedName = "\"public\".\"events\"";
        request.parameterStyle = "$";
        for (const auto& [name, databaseType, key] :
             {std::tuple{QString("id"), QString("integer"), true},
              std::tuple{QString("event_date"), QString("date"), false},
              std::tuple{QString("event_time"), QString("time without time zone"), false},
              std::tuple{QString("payload"), QString("jsonb"), false}}) {
            choscordb::GridEditColumn column;
            column.name = column.resultName = name;
            column.databaseType = databaseType;
            column.key = key;
            request.columns.push_back(std::move(column));
        }
        choscordb::GridEditRow row;
        row.original = {qint64(1), QString("2026-09-28"), QString("12:00:00"),
                        QString("{\"old\":true}")};
        row.current = {qint64(1), QString("2026-09-29"), QString("13:00:00"),
                       QString("{\"new\":true}")};
        row.touched = {false, true, true, true};
        request.rows.push_back(std::move(row));
        const auto plan = choscordb::EngineAdapter::planGridEdits(request);
        QVERIFY2(plan.error.isEmpty(), qPrintable(plan.error));
        QCOMPARE(plan.statements.size(), size_t(1));
        const auto& statement = plan.statements[0];
        QCOMPARE(statement.paramKinds.size(), statement.params.size());
        QCOMPARE(statement.paramKinds[0], QString("date"));
        QCOMPARE(statement.paramKinds[1], QString("time"));
        QCOMPARE(statement.paramKinds[2], QString("json"));
        QCOMPARE(std::get<QString>(statement.params[0]), QString("2026-09-29"));
        QCOMPARE(std::get<QString>(statement.params[2]), QString("{\"new\":true}"));
    }
    void keyedTableEditsStageAndApplyWithReview() {
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
        editor->setText("CREATE TABLE editable_rows (id INTEGER PRIMARY KEY, name TEXT);");
        run->trigger();
        QTRY_VERIFY(window.findChild<QPlainTextEdit*>("queryMessages")
                        ->toPlainText()
                        .contains("Completed"));
        QTRY_VERIFY(run->isEnabled());
        window.findChild<QPlainTextEdit*>("queryMessages")->clear();
        editor->setText("INSERT INTO editable_rows VALUES (1, 'before'), (2, 'second');");
        run->trigger();
        QTRY_VERIFY(window.findChild<QPlainTextEdit*>("queryMessages")
                        ->toPlainText()
                        .contains("Completed"));
        QTRY_VERIFY(run->isEnabled());
        const auto connection =
            window.findChild<QComboBox*>("connectionSelector")->currentData().toULongLong();
        choscordb::ObjectExplorer explorer(sql->adapter());
        auto& data = *new choscordb::ObjectDataWorkspace(sql);
        explorer.installDataWidget(&data);
        connect(&explorer, &choscordb::ObjectExplorer::dataRequested, &data,
                &choscordb::ObjectDataWorkspace::openObject);
        connect(&data, &choscordb::ObjectDataWorkspace::busyChanged, &explorer,
                &choscordb::ObjectExplorer::setOperationBusy);
        explorer.restoreObject(connection, R"(["main","editable_rows"])", "editable_rows", "table");
        explorer.selectPane(5);
        explorer.show();
        explorer.activateRestoredObject();
        auto* grid = data.findChild<QTableView*>("objectDataResults");
        QTRY_COMPARE(grid->model()->rowCount(), 2);
        QTRY_VERIFY(grid->model()->flags(grid->model()->index(0, 1)) & Qt::ItemIsEditable);
        for (const char* name : {"objectDataAddRow", "objectDataDeleteRows", "objectDataApply"}) {
            auto* action = explorer.findChild<QPushButton*>(name);
            QVERIFY(!action->toolTip().isEmpty());
            QVERIFY(action->text().isEmpty());
        }
        QVERIFY(!(grid->model()->flags(grid->model()->index(0, 0)) & Qt::ItemIsEditable));
        grid->setSelectionMode(QAbstractItemView::MultiSelection);
        grid->selectRow(0);
        grid->selectRow(1);
        triggerTableAction(grid, "Delete selected");
        QVERIFY(grid->model()->index(0, 0).data(Qt::ToolTipRole).toString().contains("deletion"));
        QVERIFY(grid->model()->index(1, 0).data(Qt::ToolTipRole).toString().contains("deletion"));
        triggerTableAction(grid, "Restore selected");
        QVERIFY(!grid->model()->index(0, 0).data(Qt::ToolTipRole).toString().contains("deletion"));
        QVERIFY(!grid->model()->index(1, 0).data(Qt::ToolTipRole).toString().contains("deletion"));
        QVERIFY(grid->model()->setData(grid->model()->index(0, 1), QString("after")));
        QVERIFY(explorer.findChild<QPushButton*>("objectDataApply")->isEnabled());
        QTimer reviewPoll;
        bool firstReview = false;
        const auto firstReviewConnection = connect(&reviewPoll, &QTimer::timeout, &data, [&] {
            auto* dialog =
                qobject_cast<QDialog*>(choscordb::design::DialogPresentation::activeDialog());
            auto* review = dialog ? dialog->findChild<QPlainTextEdit*>("gridEditReview") : nullptr;
            if (!review)
                return;
            firstReview = true;
            QVERIFY(review->toPlainText().contains("UPDATE"));
            dialog->reject();
        });
        reviewPoll.start(10);
        explorer.findChild<QPushButton*>("objectDataApply")->click();
        reviewPoll.stop();
        disconnect(firstReviewConnection);
        QVERIFY(firstReview);
        QCOMPARE(grid->model()->index(0, 1).data().toString(), QString("after"));
        QTimer::singleShot(0, &data, [] {
            auto* dialog =
                qobject_cast<QMessageBox*>(choscordb::design::DialogPresentation::activeDialog());
            QVERIFY(dialog);
            dialog->button(QMessageBox::Cancel)->click();
        });
        auto* refresh = explorer.findChild<QPushButton*>("objectRefresh");
        QVERIFY(refresh->isEnabled());
        refresh->click();
        QVERIFY(refresh->isEnabled());
        QCOMPARE(grid->model()->index(0, 1).data().toString(), QString("after"));
        bool secondReview = false;
        connect(&reviewPoll, &QTimer::timeout, &data, [&] {
            auto* dialog =
                qobject_cast<QDialog*>(choscordb::design::DialogPresentation::activeDialog());
            if (!dialog || !dialog->findChild<QPlainTextEdit*>("gridEditReview"))
                return;
            secondReview = true;
            dialog->accept();
        });
        reviewPoll.start(10);
        explorer.findChild<QPushButton*>("objectDataApply")->click();
        reviewPoll.stop();
        QVERIFY(secondReview);
        QTRY_VERIFY(!explorer.findChild<QPushButton*>("objectDataApply")->isEnabled());
        editor->setText("SELECT name FROM editable_rows WHERE id = 1;");
        editor->SendScintilla(QsciScintilla::SCI_GOTOPOS, 0);
        QTRY_VERIFY(run->isEnabled());
        run->trigger();
        auto* sqlGrid = window.findChild<QTableView*>("queryResults");
        QTRY_COMPARE(sqlGrid->model()->rowCount(), 1);
        QTRY_COMPARE(sqlGrid->model()->index(0, 0).data().toString(), QString("after"));
        QTRY_VERIFY(run->isEnabled());
        window.findChild<QPlainTextEdit*>("queryMessages")->clear();
        editor->setText("UPDATE editable_rows SET name = 'fresh' WHERE id = 1;");
        run->trigger();
        QTRY_VERIFY(window.findChild<QPlainTextEdit*>("queryMessages")
                        ->toPlainText()
                        .contains("Completed"));
        QTRY_VERIFY(run->isEnabled());
        QTRY_VERIFY(grid->model()->rowCount() == 2);
        QVERIFY(grid->model()->setData(grid->model()->index(0, 1), QString("discard-me")));
        QTimer::singleShot(0, &data, [] {
            auto* dialog =
                qobject_cast<QMessageBox*>(choscordb::design::DialogPresentation::activeDialog());
            QVERIFY(dialog);
            for (auto* button : dialog->buttons())
                if (button->text() == "Discard") {
                    button->click();
                    return;
                }
            QFAIL("Discard button missing");
        });
        QTRY_VERIFY(refresh->isEnabled());
        refresh->click();
        QTRY_COMPARE(grid->model()->rowCount(), 2);
        QTRY_COMPARE(grid->model()->index(0, 1).data().toString(), QString("fresh"));
        QTRY_VERIFY(refresh->isEnabled());
    }
};
QTEST_MAIN(GridEditWorkspaceTest)
#include "grid_edit_workspace_test.moc"
