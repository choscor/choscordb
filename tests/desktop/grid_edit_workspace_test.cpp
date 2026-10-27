#include "app/main_window.h"
#include "app/object_data_workspace.h"
#include "app/object_explorer.h"
#include "app/query_workspace.h"
#include "app/result_filter_bar.h"
#include "bridge/engine_adapter.h"
#include "design_system/dialog_presentation/dialog_presentation.h"
#include "design_system/menu/embedded_popup.h"
#include "design_system/table/table_style.h"
#include "design_system/toast_region/toast_region.h"
#include "dialog_sections_checks.h"
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
#include <QSignalSpy>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QtTest>
#include <functional>
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

    // An editable page with a read-only key column and an editable name column.
    static choscordb::ResultTableModel* editablePage(QTableView* grid) {
        auto* model = qobject_cast<choscordb::ResultTableModel*>(grid->model());
        choscordb::ResultColumn id{}, name{};
        id.name = "id";
        name.name = "name";
        if (!model->setPage({id, name}, {{qint64(1), QString("a")}, {qint64(2), QString("b")}}, 0))
            return nullptr;
        model->setEditableColumns({false, true}, true, true, {false, true});
        // Staging and discarding one value runs the production enablement refresh.
        model->setData(model->index(0, 1), QString("x"));
        model->discardEdits();
        return model;
    }
    static void focusGrid(QTableView* grid) {
        grid->window()->show();
        grid->window()->activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(grid->window()));
        grid->setFocus();
        QTRY_VERIFY(grid->hasFocus());
    }
    // Ctrl is Cmd on macOS through Qt's modifier mapping, as in the default key sequences.
    static void checkRowKeys(QTableView* grid) {
        auto* model = editablePage(grid);
        QVERIFY(model);
        focusGrid(grid);
        grid->setCurrentIndex(model->index(0, 1));
        QTest::keyClick(grid, Qt::Key_N, Qt::ControlModifier);
        QCOMPARE(model->rowCount(), 3);
        QVERIFY(model->hasPendingEdits());
        grid->setCurrentIndex(model->index(1, 1));
        QTest::keyClick(grid, Qt::Key_D, Qt::ControlModifier);
        QCOMPARE(model->rowCount(), 4);
        QCOMPARE(model->index(3, 1).data().toString(), QString("b"));
        QCOMPARE(model->index(3, 0).data().toString(), QString());
        grid->clearSelection();
        grid->selectionModel()->select(model->index(0, 0),
                                       QItemSelectionModel::Select | QItemSelectionModel::Rows);
        QTest::keyClick(grid, Qt::Key_Minus, Qt::ControlModifier | Qt::AltModifier);
        QVERIFY(model->hasDeletedRows());
        QCOMPARE(model->index(0, 1).data(choscordb::design::CellChangeRole).toInt(),
                 int(choscordb::design::CellChange::Deleted));
        QTest::keyClick(grid, Qt::Key_Equal, Qt::ControlModifier | Qt::AltModifier);
        QVERIFY(!model->hasDeletedRows());
        QCOMPARE(model->index(0, 1).data(choscordb::design::CellChangeRole).toInt(),
                 int(choscordb::design::CellChange::None));
        grid->clearSelection();
        grid->selectionModel()->select(model->index(0, 0), QItemSelectionModel::Select);
        grid->selectionModel()->select(model->index(0, 1), QItemSelectionModel::Select);
        QTest::keyClick(grid, Qt::Key_Delete);
        QVERIFY(model->index(0, 1).data(choscordb::design::CellNullRole).toBool());
        QVERIFY(!model->index(0, 0).data(choscordb::design::CellNullRole).toBool());
        QCOMPARE(model->index(0, 0).data().toString(), QString("1"));
        grid->clearSelection();
        grid->selectionModel()->select(model->index(1, 1), QItemSelectionModel::Select);
        QTest::keyClick(grid, Qt::Key_Backspace);
        QVERIFY(model->index(1, 1).data(choscordb::design::CellNullRole).toBool());
    }

    // Presses a key and answers the first dialog that it opens. Returns the review object
    // name or the message text of that dialog, or an empty string when no dialog opened.
    static QString answerDialog(const std::function<void()>& press, const QString& choice = {}) {
        QString seen;
        QTimer poll;
        QObject::connect(&poll, &QTimer::timeout, [&] {
            auto* dialog =
                qobject_cast<QDialog*>(choscordb::design::DialogPresentation::activeDialog());
            if (!dialog || !seen.isEmpty())
                return;
            if (auto* review = dialog->findChild<QPlainTextEdit*>("gridEditReview")) {
                seen = review->objectName();
                dialog->reject();
                return;
            }
            auto* box = qobject_cast<QMessageBox*>(dialog);
            seen = box ? box->text() : dialog->objectName();
            if (box)
                for (auto* button : box->buttons())
                    if (button->text() == choice) {
                        button->click();
                        return;
                    }
            dialog->reject();
        });
        poll.start(10);
        press();
        if (!QTest::qWaitFor([&] { return !seen.isEmpty(); }, 1000))
            return {};
        return seen;
    }
    static void checkEditAndDiscardKeys(QTableView* grid) {
        auto* model = editablePage(grid);
        QVERIFY(model);
        focusGrid(grid);
        const auto cell = model->index(0, 1);
        grid->setCurrentIndex(cell);
        QTest::keyClick(grid, Qt::Key_Return, Qt::ShiftModifier);
        auto* sheet = grid->window()->findChild<QDialog*>("cellEditSheet");
        QVERIFY(sheet);
        QTRY_VERIFY(sheet->isVisible());
        sheet->reject();
        QTRY_VERIFY(!sheet->isVisible());
        focusGrid(grid);
        grid->setCurrentIndex(cell);
        grid->edit(cell);
        auto* editor = qobject_cast<QLineEdit*>(grid->indexWidget(cell));
        QVERIFY(editor);
        editor->setFocus();
        QTest::keyClick(editor, Qt::Key_End);
        QTest::keyClicks(editor, "zz");
        QTest::keyClick(editor, Qt::Key_Backspace);
        QTest::keyClick(editor, Qt::Key_Delete);
        QTest::keyClick(editor, Qt::Key_N, Qt::ControlModifier);
        QCOMPARE(editor->text(), QString("az"));
        QCOMPARE(model->rowCount(), 2);
        QVERIFY(!model->index(0, 1).data(choscordb::design::CellNullRole).toBool());
        QTest::keyClick(editor, Qt::Key_Escape);
        QTRY_VERIFY(!grid->indexWidget(cell));
        QVERIFY(model->setData(cell, QString("changed")));
        focusGrid(grid);
        const auto discard = [grid] {
            QTest::keyClick(grid, Qt::Key_Backspace, Qt::ControlModifier | Qt::ShiftModifier);
        };
        QCOMPARE(answerDialog(discard, "Cancel"), QString("Discard all staged grid changes?"));
        QVERIFY(model->hasPendingEdits());
        QCOMPARE(answerDialog(discard, "Discard"), QString("Discard all staged grid changes?"));
        QVERIFY(!model->hasPendingEdits());
        QCOMPARE(cell.data().toString(), QString("a"));
    }

  private slots:
    void gridKeysStageRowEditsInBothGrids() {
        choscordb::MainWindow window;
        window.findChild<QAction*>("newQuery")->trigger();
        auto* tabs = window.findChild<QTabWidget*>("editorTabs");
        const auto tabCount = tabs->count();
        checkRowKeys(window.findChild<QTableView*>("queryResults"));
        QCOMPARE(tabs->count(), tabCount);
        choscordb::ObjectDataWorkspace data(window.findChild<choscordb::QueryWorkspace*>());
        data.resize(800, 500);
        checkRowKeys(data.findChild<QTableView*>("objectDataResults"));
        QCOMPARE(tabs->count(), tabCount);
    }
    void gridEditAndDiscardKeysWorkInBothGrids() {
        choscordb::MainWindow window;
        window.findChild<QAction*>("newQuery")->trigger();
        checkEditAndDiscardKeys(window.findChild<QTableView*>("queryResults"));
        choscordb::ObjectDataWorkspace data(window.findChild<choscordb::QueryWorkspace*>());
        data.resize(800, 500);
        QVERIFY(data.findChild<QPushButton*>("objectDataDiscardEdits"));
        checkEditAndDiscardKeys(data.findChild<QTableView*>("objectDataResults"));
    }
    void applyKeyReviewsFromGridAndInlineEditorButSavesFromSqlEditor() {
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
        for (const auto* statement :
             {"CREATE TABLE keyed_rows (id INTEGER PRIMARY KEY, name TEXT);",
              "INSERT INTO keyed_rows VALUES (1, 'before');"}) {
            QTRY_VERIFY(run->isEnabled());
            messages->clear();
            editor->setText(statement);
            run->trigger();
            QTRY_VERIFY(messages->toPlainText().contains("Completed"));
        }
        QTRY_VERIFY(run->isEnabled());
        auto* save = window.findChild<QAction*>("command_save_sql_file");
        QVERIFY(save);
        QSignalSpy saved(save, &QAction::triggered);
        editor->setFocus();
        QTRY_VERIFY(editor->hasFocus());
        const auto saveKey = [editor] { QTest::keyClick(editor, Qt::Key_S, Qt::ControlModifier); };
        QVERIFY(answerDialog(saveKey) != QString("gridEditReview"));
        QCOMPARE(saved.count(), 1);
        choscordb::ObjectExplorer explorer(sql->adapter());
        auto& data = *new choscordb::ObjectDataWorkspace(sql);
        explorer.installDataWidget(&data);
        connect(&explorer, &choscordb::ObjectExplorer::dataRequested, &data,
                &choscordb::ObjectDataWorkspace::openObject);
        explorer.restoreObject(editor->connectionTarget().value_or(0), R"(["main","keyed_rows"])",
                               "keyed_rows", "table");
        explorer.selectPane(5);
        explorer.show();
        explorer.activateRestoredObject();
        auto* grid = data.findChild<QTableView*>("objectDataResults");
        auto* model = grid->model();
        QTRY_COMPARE(model->rowCount(), 1);
        const auto cell = model->index(0, 1);
        QTRY_VERIFY(model->flags(cell) & Qt::ItemIsEditable);
        focusGrid(grid);
        const auto applyKey = [grid] { QTest::keyClick(grid, Qt::Key_S, Qt::ControlModifier); };
        QCOMPARE(answerDialog(applyKey), QString());
        grid->setCurrentIndex(cell);
        grid->edit(cell);
        auto* inline_ = qobject_cast<QLineEdit*>(grid->indexWidget(cell));
        QVERIFY(inline_);
        inline_->setFocus();
        QTest::keyClick(inline_, Qt::Key_End);
        QTest::keyClicks(inline_, "!");
        const auto commitKey = [inline_] {
            QTest::keyClick(inline_, Qt::Key_S, Qt::ControlModifier);
        };
        QCOMPARE(answerDialog(commitKey), QString("gridEditReview"));
        QCOMPARE(cell.data().toString(), QString("before!"));
        QTRY_VERIFY(!grid->indexWidget(cell));
        focusGrid(grid);
        QCOMPARE(answerDialog(applyKey), QString("gridEditReview"));
        QCOMPARE(saved.count(), 1);
        // An open SQL transaction blocks Apply: the key shows the reason, not the review.
        QTRY_VERIFY(run->isEnabled());
        editor->setText("BEGIN;");
        run->trigger();
        auto* applyAction = grid->findChild<QAction*>("objectDataApply");
        QVERIFY(applyAction);
        QTRY_VERIFY(!applyAction->isEnabled());
        const QString guard(
            "Commit or roll back the open transaction before applying grid changes.");
        QCOMPARE(applyAction->toolTip(), guard);
        auto* dataMessages = data.findChild<QPlainTextEdit*>("objectDataMessages");
        dataMessages->clear();
        focusGrid(grid);
        QCOMPARE(answerDialog(applyKey), QString());
        QVERIFY(dataMessages->toPlainText().contains(guard));
        QVERIFY(model->index(0, 1).data(choscordb::design::CellChangeRole).toInt() !=
                int(choscordb::design::CellChange::None));
    }
    void menuShowsGridKeysAndApplyTooltipShowsItsKey() {
        choscordb::MainWindow window;
        choscordb::ObjectDataWorkspace data(window.findChild<choscordb::QueryWorkspace*>());
        data.resize(800, 500);
        data.show();
        auto* grid = data.findChild<QTableView*>("objectDataResults");
        auto* model = editablePage(grid);
        QVERIFY(model);
        QStringList keyed;
        QTimer::singleShot(0, grid, [&keyed] {
            auto* menu = qobject_cast<QMenu*>(choscordb::design::detail::activeEmbeddedPopup());
            QVERIFY(menu);
            const auto actions = menu->actions();
            menu->close();
            for (auto* action : actions)
                if (!action->shortcut().isEmpty())
                    keyed << action->objectName();
        });
        grid->customContextMenuRequested(grid->visualRect(model->index(0, 1)).center());
        QCOMPARE(keyed,
                 QStringList({"editCell", "duplicateRow", "objectDataAddRow",
                              "objectDataDeleteRows", "objectDataRestoreRows", "objectDataSetNull",
                              "objectDataApply", "objectDataDiscardEdits"}));
        const auto key = QKeySequence(QKeySequence::Save).toString(QKeySequence::NativeText);
        QVERIFY(model->setData(model->index(0, 1), QString("changed")));
        QCOMPARE(data.findChild<QPushButton*>("objectDataApply")->toolTip(), "Apply… · " + key);
    }
    void sqlGridMenuHasApplyAndDiscardAndShowsBlockedReasons() {
        choscordb::MainWindow window;
        window.findChild<QAction*>("newQuery")->trigger();
        auto* grid = window.findChild<QTableView*>("queryResults");
        grid->window()->show();
        auto* model = editablePage(grid);
        QVERIFY(model);
        QVERIFY(model->setData(model->index(0, 1), QString("changed")));
        QHash<QString, QAction*> shown;
        const auto openMenu = [&] {
            shown.clear();
            QTimer::singleShot(0, grid, [&shown] {
                auto* menu = qobject_cast<QMenu*>(choscordb::design::detail::activeEmbeddedPopup());
                QVERIFY(menu);
                for (auto* action : menu->actions())
                    if (!action->objectName().isEmpty())
                        shown.insert(action->objectName(), action);
                menu->close();
            });
            grid->customContextMenuRequested(grid->visualRect(model->index(0, 0)).center());
        };
        openMenu();
        QVERIFY(shown.contains("queryResultApplyEdits"));
        QVERIFY(shown.contains("queryResultDiscardEdits"));
        QVERIFY(shown.value("queryResultDiscardEdits")->isEnabled());
        QVERIFY(!shown.value("editCell")->isEnabled());
        // A read-only page gives Add row its reason.
        choscordb::ResultColumn column{};
        column.name = "value";
        QVERIFY(model->setPage({column}, {{QString("a")}}, 0));
        openMenu();
        QCOMPARE(shown.value("queryResultAddRow")->toolTip(), QString("This result is read only."));
    }
    void editKeyOpensInlineEditorAndKeypadOrShiftedKeysRunCommands() {
        choscordb::MainWindow window;
        window.findChild<QAction*>("newQuery")->trigger();
        auto* grid = window.findChild<QTableView*>("queryResults");
        auto* model = editablePage(grid);
        QVERIFY(model);
        focusGrid(grid);
        const auto cell = model->index(0, 1);
        grid->setCurrentIndex(cell);
#ifdef Q_OS_MACOS
        QTest::keyClick(grid, Qt::Key_Return);
#else
        QTest::keyClick(grid, Qt::Key_F2);
#endif
        QTRY_VERIFY(qobject_cast<QLineEdit*>(grid->indexWidget(cell)));
        auto* editor = qobject_cast<QLineEdit*>(grid->indexWidget(cell));
        editor->setFocus();
        // Keypad Delete inside the editor deletes text and never sets the cell to NULL.
        QTest::keyClick(editor, Qt::Key_Home);
        QTest::keyClick(editor, Qt::Key_Delete, Qt::KeypadModifier);
        QCOMPARE(editor->text(), QString());
        QVERIFY(!cell.data(choscordb::design::CellNullRole).toBool());
        QTest::keyClick(editor, Qt::Key_Escape);
        QTRY_VERIFY(!grid->indexWidget(cell));
        focusGrid(grid);
        grid->clearSelection();
        grid->selectionModel()->select(cell, QItemSelectionModel::Select);
        QTest::keyClick(grid, Qt::Key_Delete, Qt::KeypadModifier);
        QVERIFY(cell.data(choscordb::design::CellNullRole).toBool());
        // A layout that needs Shift for "=" still restores rows with Ctrl+Alt+=.
        grid->selectionModel()->select(model->index(1, 0), QItemSelectionModel::ClearAndSelect |
                                                               QItemSelectionModel::Rows);
        QTest::keyClick(grid, Qt::Key_Minus, Qt::ControlModifier | Qt::AltModifier);
        QVERIFY(model->hasDeletedRows());
        QTest::keyClick(grid, Qt::Key_Equal,
                        Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier);
        QVERIFY(!model->hasDeletedRows());
    }
    void blockedGridKeyShowsReasonAndIdleKeyDoesNothing() {
        choscordb::MainWindow window;
        window.findChild<QAction*>("newQuery")->trigger();
        auto* tabs = window.findChild<QTabWidget*>("editorTabs");
        const auto tabCount = tabs->count();
        auto* grid = window.findChild<QTableView*>("queryResults");
        auto* messages = window.findChild<QPlainTextEdit*>("queryMessages");
        auto* model = qobject_cast<choscordb::ResultTableModel*>(grid->model());
        choscordb::ResultColumn column{};
        column.name = "value";
        QVERIFY(model->setPage({column}, {{QString("a")}}, 0));
        focusGrid(grid);
        grid->setCurrentIndex(model->index(0, 0));
        messages->clear();
        QTest::keyClick(grid, Qt::Key_N, Qt::ControlModifier);
        QCOMPARE(model->rowCount(), 1);
        QCOMPARE(tabs->count(), tabCount);
        QVERIFY(messages->toPlainText().contains("This result is read only."));
        messages->clear();
        QTest::keyClick(grid, Qt::Key_S, Qt::ControlModifier);
        QTest::keyClick(grid, Qt::Key_Equal, Qt::ControlModifier | Qt::AltModifier);
        QCoreApplication::processEvents();
        QVERIFY(!choscordb::design::DialogPresentation::activeDialog());
        QVERIFY(messages->toPlainText().isEmpty());
        QVERIFY(!model->hasPendingEdits());
    }
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
        const auto connection = editor->connectionTarget().value_or(0);
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
        QCOMPARE(statement.paramKinds[0], choscordb::CellKind::Date);
        QCOMPARE(statement.paramKinds[1], choscordb::CellKind::Time);
        QCOMPARE(statement.paramKinds[2], choscordb::CellKind::Json);
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
        const auto connection = editor->connectionTarget().value_or(0);
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
        triggerTableAction(grid, "Delete rows");
        QVERIFY(grid->model()->index(0, 0).data(Qt::ToolTipRole).toString().contains("deletion"));
        QVERIFY(grid->model()->index(1, 0).data(Qt::ToolTipRole).toString().contains("deletion"));
        triggerTableAction(grid, "Restore rows");
        QVERIFY(!grid->model()->index(0, 0).data(Qt::ToolTipRole).toString().contains("deletion"));
        QVERIFY(!grid->model()->index(1, 0).data(Qt::ToolTipRole).toString().contains("deletion"));
        QVERIFY(grid->model()->setData(grid->model()->index(0, 1), QString("after")));
        QVERIFY(explorer.findChild<QPushButton*>("objectDataApply")->isEnabled());
        QTimer reviewPoll;
        bool firstReview = false;
        bool reviewSectioned = false;
        const auto firstReviewConnection = connect(&reviewPoll, &QTimer::timeout, &data, [&] {
            auto* dialog =
                qobject_cast<QDialog*>(choscordb::design::DialogPresentation::activeDialog());
            auto* review = dialog ? dialog->findChild<QPlainTextEdit*>("gridEditReview") : nullptr;
            if (!review)
                return;
            firstReview = true;
            QVERIFY(review->toPlainText().contains("UPDATE"));
            auto* sections = dialog->findChild<choscordb::design::DialogSections*>();
            if (!sections) {
                dialog->reject();
                return;
            }
            auto* body = sections->bodyLayout()->parentWidget();
            auto* footer = sections->footerLayout()->parentWidget();
            auto* description = body->findChild<QLabel*>();
            auto* apply = footer->findChild<QPushButton*>("gridEditApply");
            reviewSectioned =
                headerHeadline(*dialog) == QStringLiteral("Review grid changes") &&
                sectionedSpacing(*dialog) == compactSectionedSpacing() &&
                footerOverrides(*dialog).isEmpty() && body->isAncestorOf(review) && description &&
                description->text() == QStringLiteral("Statements and bound parameter values") &&
                footer->findChild<QPushButton*>("gridEditCancel") && apply && apply->isDefault();
            if (auto* dismiss = headerDismiss(*dialog, "gridEditDismiss"))
                dismiss->click();
            else
                dialog->reject();
        });
        reviewPoll.start(10);
        explorer.findChild<QPushButton*>("objectDataApply")->click();
        reviewPoll.stop();
        disconnect(firstReviewConnection);
        QVERIFY(firstReview);
        QVERIFY(reviewSectioned);
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
            dialog->findChild<QPushButton*>("gridEditApply")->click();
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
