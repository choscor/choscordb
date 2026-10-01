#include "app/main_window.h"
#include "app/object_explorer.h"
#include "app/query_workspace.h"
#include "design_system/dialog_presentation/dialog_presentation.h"
#include "design_system/menu/embedded_popup.h"
#include "design_system/theme_manager.h"
#include "models/result_table_model.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTableView>
#include <QTimer>
#include <QtTest>
#include <memory>

class QuickFilterWorkspaceTest : public QObject {
    Q_OBJECT
    std::unique_ptr<choscordb::MainWindow> window_;
    choscordb::ObjectExplorer* object_ = nullptr;
    QTableView* grid_ = nullptr;
    QLineEdit* filter_ = nullptr;
    static QMenu* quickMenu(QMenu* menu) {
        for (auto* action : menu->actions())
            if (action->menu() && action->menu()->objectName() == "resultQuickFilter")
                return action->menu();
        return nullptr;
    }
    void menuAt(const QPoint& point, const std::function<void(QMenu*)>& inspect) {
        QTimer::singleShot(0, grid_, [inspect] {
            auto* menu = qobject_cast<QMenu*>(choscordb::design::detail::activeEmbeddedPopup());
            QVERIFY(menu);
            inspect(menu);
            menu->close();
        });
        grid_->customContextMenuRequested(point);
    }
    void choose(int row, int column, int operation) {
        menuAt(grid_->visualRect(grid_->model()->index(row, column)).center(),
               [operation](QMenu* menu) {
                   auto* quick = quickMenu(menu);
                   QVERIFY(quick);
                   QVERIFY(quick->menuAction()->isEnabled());
                   QVERIFY(quick->actions().at(operation)->isEnabled());
                   quick->actions().at(operation)->trigger();
               });
    }
  private slots:
    void init() {
        window_ = std::make_unique<choscordb::MainWindow>();
        window_->show();
        auto* workspace = window_->findChild<choscordb::QueryWorkspace*>();
        workspace->connectSqlite(":memory:");
        auto* newQuery = window_->findChild<QAction*>("newQuery");
        QTRY_VERIFY(newQuery->isEnabled());
        newQuery->trigger();
        auto* tabs = window_->findChild<QTabWidget*>("editorTabs");
        auto* editor = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
        auto* run = window_->findChild<QAction*>("runStatement");
        auto* messages = window_->findChild<QPlainTextEdit*>("queryMessages");
        for (const auto& sql :
             {"CREATE TABLE sample(id INTEGER PRIMARY KEY, name TEXT, \"a&b\" "
              "TEXT DEFAULT 'marker', "
              "pattern TEXT DEFAULT '50%_\\O''Reilly, Việt')",
              "INSERT INTO sample(id,name,pattern) VALUES (1,'Alice','50%_\\O''Reilly, Việt'),"
              "(2,'Bob','prefix 50%_\\O''Reilly, Việt suffix'),(3,NULL,'50xx\\O''Reilly, Việt')",
              "SELECT * FROM sample"}) {
            QTRY_VERIFY(run->isEnabled());
            messages->clear();
            editor->setText(sql);
            run->trigger();
            QTRY_VERIFY(messages->toPlainText().contains("Completed"));
        }
        const auto connection =
            window_->findChild<QComboBox*>("connectionSelector")->currentData().toULongLong();
        window_->objectContextSelected(connection, R"(["main","sample"])", "sample", "table");
        object_ = qobject_cast<choscordb::ObjectExplorer*>(tabs->currentWidget());
        QVERIFY(object_);
        grid_ = object_->findChild<QTableView*>("objectDataResults");
        filter_ = object_->findChild<QLineEdit*>("resultFilterSql");
        QVERIFY(grid_ && filter_);
        QTRY_COMPARE(grid_->model()->rowCount(), 3);
        QTRY_VERIFY(filter_->isEnabled());
    }
    void cleanup() { window_.reset(); }
    void clickedCellUsesVisibleDraftAndAppliesImmediately() {
        grid_->setCurrentIndex(grid_->model()->index(0, 0));
        grid_->selectionModel()->select(grid_->model()->index(2, 1), QItemSelectionModel::Select);
        const auto selected = grid_->selectionModel()->selectedIndexes();
        filter_->setText("id = 1 OR id = 2");
        menuAt(grid_->visualRect(grid_->model()->index(1, 1)).center(),
               [this, selected](QMenu* menu) {
                   auto* quick = quickMenu(menu);
                   QVERIFY(quick);
                   QCOMPARE(quick->title(), QString("Quick Filter"));
                   QStringList labels;
                   for (auto* action : quick->actions())
                       labels << action->text();
                   QCOMPARE(labels, QStringList({"name = value", "name != value", "name < value",
                                                 "name <= value", "name > value", "name >= value",
                                                 "name IN (value)", "name LIKE value"}));
                   QCOMPARE(grid_->selectionModel()->selectedIndexes(), selected);
                   quick->actions().first()->trigger();
               });
        QTRY_COMPARE(filter_->text(), QString("(id = 1 OR id = 2) AND \"name\" = 'Bob'"));
        QTRY_COMPARE(grid_->model()->rowCount(), 1);
        QCOMPARE(grid_->model()->index(0, 0).data().toString(), QString("2"));
    }
    void ampersandColumnNamesAreLiteralMenuLabels() {
        menuAt(grid_->visualRect(grid_->model()->index(0, 2)).center(), [](QMenu* menu) {
            auto* quick = quickMenu(menu);
            QVERIFY(quick);
            QCOMPARE(quick->actions().first()->text(), QString("a&&b = value"));
        });
    }
    void literalLikeAndSingletonInActivateThroughTheMenu_data() {
        QTest::addColumn<bool>("like");
        QTest::newRow("contains-like") << true;
        QTest::newRow("singleton-in") << false;
    }
    void literalLikeAndSingletonInActivateThroughTheMenu() {
        QFETCH(bool, like);
        choose(0, 3, like ? 7 : 6);
        QTRY_VERIFY(filter_->text().startsWith("\"pattern\""));
        QTRY_VERIFY(filter_->isEnabled());
        QCOMPARE(filter_->text(),
                 like ? QString("\"pattern\" LIKE '%50\\%\\_\\\\O''Reilly, Việt%' ESCAPE '\\'")
                      : QString("\"pattern\" IN ('50%_\\O''Reilly, Việt')"));
        QCOMPARE(grid_->model()->rowCount(), like ? 2 : 1);
        QCOMPARE(grid_->model()->index(0, 0).data().toString(), QString("1"));
        if (like)
            QCOMPARE(grid_->model()->index(1, 0).data().toString(), QString("2"));
    }
    void nullAndDisabledOperatorsAndUnavailableTargets() {
        menuAt(grid_->visualRect(grid_->model()->index(2, 1)).center(), [](QMenu* menu) {
            auto* quick = quickMenu(menu);
            QVERIFY(quick);
            QCOMPARE(quick->actions().size(), 2);
            QCOMPARE(quick->actions().at(0)->text(), QString("name IS NULL"));
            QCOMPARE(quick->actions().at(1)->text(), QString("name IS NOT NULL"));
        });
        menuAt(grid_->visualRect(grid_->model()->index(0, 0)).center(), [](QMenu* menu) {
            auto* quick = quickMenu(menu);
            QVERIFY(quick && quick->actions().size() == 8);
            QVERIFY(!quick->actions().at(7)->isEnabled());
            QVERIFY(!quick->actions().at(7)->toolTip().isEmpty());
        });
        menuAt(QPoint(grid_->viewport()->width() - 5, grid_->viewport()->height() - 5),
               [](QMenu* menu) {
                   auto* quick = quickMenu(menu);
                   QVERIFY(quick && !quick->menuAction()->isEnabled());
                   QVERIFY(!quick->menuAction()->toolTip().isEmpty());
               });
        auto* workspace = object_->findChild<choscordb::QueryWorkspace*>();
        QVERIFY(workspace);
        workspace->setExternalWork(true);
        menuAt(grid_->visualRect(grid_->model()->index(0, 0)).center(), [](QMenu* menu) {
            auto* quick = quickMenu(menu);
            QVERIFY(quick && !quick->menuAction()->isEnabled());
        });
        workspace->setExternalWork(false);
        auto* sqlGrid = window_->findChild<QTableView*>("queryResults");
        auto* saved = grid_;
        grid_ = sqlGrid;
        menuAt(QPoint(10, 10), [](QMenu* menu) { QVERIFY(!quickMenu(menu)); });
        grid_ = saved;
        choose(2, 1, 0);
        QTRY_COMPARE(filter_->text(), QString("\"name\" IS NULL"));
        QTRY_COMPARE(grid_->model()->rowCount(), 1);
        QCOMPARE(grid_->model()->index(0, 0).data().toString(), QString("3"));
    }
    void invalidDraftRetainsTheAcceptedView() {
        choose(1, 0, 0);
        QTRY_COMPARE(grid_->model()->rowCount(), 1);
        QTRY_VERIFY(filter_->isEnabled());
        filter_->setText("id = (");
        choose(0, 0, 0);
        auto* error = object_->findChild<QLabel*>("resultFilterError");
        QTRY_VERIFY(error->isVisible());
        QVERIFY(filter_->text().contains("id = ("));
        QVERIFY(filter_->text().contains("AND \"id\" = 2"));
        QCOMPARE(grid_->model()->rowCount(), 1);
        QCOMPARE(grid_->model()->index(0, 0).data().toString(), QString("2"));
    }
    void staleActivationAndBusyActivationDoNotChangeDraft() {
        auto* model = qobject_cast<choscordb::ResultTableModel*>(grid_->model());
        QVERIFY(model);
        filter_->setText("id > 0");
        menuAt(grid_->visualRect(model->index(0, 0)).center(), [this, model](QMenu* menu) {
            auto* quick = quickMenu(menu);
            QVERIFY(quick && quick->actions().first()->isEnabled());
            const auto columns = model->columns();
            const auto rows = model->rows();
            QVERIFY(model->setPage(columns, rows, 0));
            quick->actions().first()->trigger();
            QCOMPARE(filter_->text(), QString("id > 0"));
            QVERIFY(filter_->isEnabled());
        });
        auto* workspace = object_->findChild<choscordb::QueryWorkspace*>();
        menuAt(grid_->visualRect(model->index(0, 0)).center(), [this, workspace](QMenu* menu) {
            auto* quick = quickMenu(menu);
            QVERIFY(quick && quick->actions().first()->isEnabled());
            workspace->setExternalWork(true);
            quick->actions().first()->trigger();
            QCOMPARE(filter_->text(), QString("id > 0"));
            workspace->setExternalWork(false);
        });
    }
    void pendingEditCancelRestoresAcceptedAndDiscardUsesCapturedValue_data() {
        QTest::addColumn<bool>("discard");
        QTest::newRow("cancel") << false;
        QTest::newRow("discard") << true;
    }
    void pendingEditCancelRestoresAcceptedAndDiscardUsesCapturedValue() {
        QFETCH(bool, discard);
        auto* model = qobject_cast<choscordb::ResultTableModel*>(grid_->model());
        const auto cell = model->index(0, 1);
        QTRY_VERIFY(cell.flags() & Qt::ItemIsEditable);
        QVERIFY(model->setData(cell, QString("Bob")));
        filter_->setText("id > 0");
        QTimer review;
        bool shown = false;
        connect(&review, &QTimer::timeout, this, [discard, &shown] {
            auto* box =
                qobject_cast<QMessageBox*>(choscordb::design::DialogPresentation::activeDialog());
            if (!box || box->windowTitle() != "Pending grid changes")
                return;
            shown = true;
            if (!discard) {
                box->reject();
                return;
            }
            for (auto* button : box->findChildren<QPushButton*>())
                if (button->text() == "Discard") {
                    button->click();
                    return;
                }
            QFAIL("Missing Discard button");
        });
        review.start(10);
        choose(0, 1, 0);
        QTRY_VERIFY(shown);
        QTRY_VERIFY(filter_->isEnabled());
        if (discard) {
            QTRY_COMPARE(grid_->model()->rowCount(), 1);
            QCOMPARE(filter_->text(), QString("id > 0 AND \"name\" = 'Bob'"));
            QCOMPARE(model->index(0, 0).data().toString(), QString("2"));
            QVERIFY(!model->hasPendingEdits());
        } else {
            QVERIFY(filter_->text().isEmpty());
            QCOMPARE(model->rowCount(), 3);
            QVERIFY(model->hasPendingEdits());
            QCOMPARE(model->index(0, 1).data().toString(), QString("Bob"));
        }
    }
    void generationFailurePreservesVisibleDraftAndStagedValue() {
        auto* model = qobject_cast<choscordb::ResultTableModel*>(grid_->model());
        const auto cell = model->index(0, 1);
        QTRY_VERIFY(cell.flags() & Qt::ItemIsEditable);
        filter_->setText("id > 0");
        const auto unsafeText = QString("x") + QChar(0) + QString("y");
        menuAt(grid_->visualRect(cell).center(), [model, cell, unsafeText](QMenu* menu) {
            auto* quick = quickMenu(menu);
            QVERIFY(quick && quick->actions().first()->isEnabled());
            QVERIFY(model->setData(cell, unsafeText));
            quick->actions().first()->trigger();
        });
        QTRY_VERIFY(object_->findChild<QLabel*>("resultFilterError")->isVisible());
        QVERIFY(filter_->isEnabled());
        QCOMPARE(filter_->text(), QString("id > 0"));
        QCOMPARE(model->rowCount(), 3);
        QCOMPARE(model->index(0, 1).data().toString(), unsafeText);
        QVERIFY(model->hasPendingEdits());
    }
    void invalidationDuringCompositionPreventsApplication() {
        auto* workspace = object_->findChild<choscordb::QueryWorkspace*>();
        choose(0, 1, 0);
        workspace->invalidateResult();
        QTRY_COMPARE(grid_->model()->rowCount(), 0);
        QTRY_COMPARE(object_->findChild<QLabel*>("objectDataSummary")->property("state").toString(),
                     QString("disconnected"));
        QVERIFY(filter_->text().isEmpty());
        QVERIFY(!object_->findChild<QPushButton*>("resultFilterApply")->isEnabled());
    }
    void replacingResultBeforeActivationKeepsNewResultUnfiltered() {
        auto* workspace = object_->findChild<choscordb::QueryWorkspace*>();
        const auto connection =
            window_->findChild<QComboBox*>("connectionSelector")->currentData().toULongLong();
        filter_->setText("id > 0");
        menuAt(grid_->visualRect(grid_->model()->index(0, 1)).center(),
               [workspace, connection](QMenu* menu) {
                   auto* quick = quickMenu(menu);
                   QVERIFY(quick && quick->actions().first()->isEnabled());
                   workspace->openObjectData(connection, R"(["main","sample"])", "sample",
                                             workspace->queryPreferences());
                   quick->actions().first()->trigger();
               });
        QTRY_COMPARE(grid_->model()->rowCount(), 3);
        QTRY_VERIFY(filter_->isEnabled());
        QVERIFY(filter_->text().isEmpty());
        QCOMPARE(grid_->model()->index(0, 1).data().toString(), QString("Alice"));
    }
    void externalWorkDuringCompositionDoesNotLeaveTheFilterDisabled() {
        filter_->setText("id > 0");
        auto* workspace = object_->findChild<choscordb::QueryWorkspace*>();
        choose(0, 1, 0);
        auto* watcher = workspace->findChild<QFutureWatcherBase*>("resultQuickFilterPreparation");
        QVERIFY(watcher);
        QSignalSpy finished(watcher, &QFutureWatcherBase::finished);
        workspace->setExternalWork(true);
        QTRY_COMPARE(finished.count(), 1);
        workspace->setExternalWork(false);
        QVERIFY(filter_->isEnabled());
        QCOMPARE(filter_->text(), QString("id > 0"));
        QCOMPARE(grid_->model()->rowCount(), 3);
    }
    void pendingEditDialogSurvivesWorkspaceDestruction() {
        auto* model = qobject_cast<choscordb::ResultTableModel*>(grid_->model());
        const auto cell = model->index(0, 1);
        QTRY_VERIFY(cell.flags() & Qt::ItemIsEditable);
        QVERIFY(model->setData(cell, QString("Bob")));
        QPointer<choscordb::QueryWorkspace> workspace =
            object_->findChild<choscordb::QueryWorkspace*>();
        QTimer review;
        bool removed = false;
        connect(&review, &QTimer::timeout, this, [&] {
            auto* box =
                qobject_cast<QMessageBox*>(choscordb::design::DialogPresentation::activeDialog());
            if (!box || box->windowTitle() != "Pending grid changes")
                return;
            delete workspace.data();
            removed = true;
            for (auto* button : box->findChildren<QPushButton*>())
                if (button->text() == "Discard") {
                    button->click();
                    return;
                }
            QFAIL("Missing Discard button");
        });
        review.start(10);
        choose(0, 1, 0);
        QTRY_VERIFY(removed);
        QVERIFY(workspace.isNull());
        QVERIFY(grid_->model() == nullptr);
        delete object_;
        object_ = nullptr;
    }
    void pendingEditApplyWaitsForRefreshAndFiltersCapturedValue() {
        auto* model = qobject_cast<choscordb::ResultTableModel*>(grid_->model());
        const auto cell = model->index(0, 1);
        QTRY_VERIFY(cell.flags() & Qt::ItemIsEditable);
        QVERIFY(model->setData(cell, QString("Bob")));
        QTimer review;
        bool pendingShown = false, reviewShown = false;
        connect(&review, &QTimer::timeout, this, [&] {
            auto* dialog = choscordb::design::DialogPresentation::activeDialog();
            if (auto* box = qobject_cast<QMessageBox*>(dialog);
                box && box->windowTitle() == "Pending grid changes") {
                pendingShown = true;
                for (auto* button : box->findChildren<QPushButton*>())
                    if (button->text() == "Apply…") {
                        button->click();
                        return;
                    }
            } else if (auto* reviewDialog = qobject_cast<QDialog*>(dialog);
                       reviewDialog && reviewDialog->findChild<QPlainTextEdit*>("gridEditReview")) {
                reviewShown = true;
                reviewDialog->accept();
            }
        });
        review.start(10);
        choose(0, 1, 0);
        QTRY_VERIFY(pendingShown && reviewShown);
        QTRY_COMPARE(grid_->model()->rowCount(), 2);
        QTRY_VERIFY(filter_->isEnabled());
        QCOMPARE(filter_->text(), QString("\"name\" = 'Bob'"));
        QVERIFY(!model->hasPendingEdits());
        QCOMPARE(model->index(0, 0).data().toString(), QString("1"));
        QCOMPARE(model->index(1, 0).data().toString(), QString("2"));
        object_->findChild<QPushButton*>("resultFilterClear")->click();
        QTRY_COMPARE(model->rowCount(), 3);
        QCOMPARE(model->index(0, 1).data().toString(), QString("Bob"));
    }
    void fullResultFilterPreservesSortAndResetsPaging() {
        auto* tabs = window_->findChild<QTabWidget*>("editorTabs");
        auto* editor = window_->findChild<choscordb::SqlEditor*>();
        tabs->setCurrentWidget(editor);
        auto* run = window_->findChild<QAction*>("runStatement");
        auto* messages = window_->findChild<QPlainTextEdit*>("queryMessages");
        QTRY_VERIFY(run->isEnabled());
        messages->clear();
        editor->setText(
            "WITH RECURSIVE n(x) AS (SELECT 4 UNION ALL SELECT x+1 FROM n WHERE x<1005) "
            "INSERT INTO sample(id,name) SELECT x, CASE WHEN x=1004 THEN 'Bob' ELSE 'Other' END "
            "FROM n");
        run->trigger();
        QTRY_VERIFY(messages->toPlainText().contains("Completed"));
        tabs->setCurrentWidget(object_);
        auto* workspace = object_->findChild<choscordb::QueryWorkspace*>();
        const auto connection =
            window_->findChild<QComboBox*>("connectionSelector")->currentData().toULongLong();
        workspace->openObjectData(
            connection, R"(["main","sample"])", "sample",
            window_->findChild<choscordb::QueryWorkspace*>()->queryPreferences());
        QTRY_COMPARE(grid_->model()->rowCount(), 1000);
        QTRY_VERIFY(filter_->isEnabled());
        QTest::mouseClick(grid_->horizontalHeader()->viewport(), Qt::LeftButton, {}, QPoint(10, 5));
        QTRY_VERIFY(grid_->horizontalHeader()->isSortIndicatorShown());
        QTRY_VERIFY(filter_->isEnabled());
        auto* next = object_->findChild<QPushButton*>("objectDataNext");
        QTRY_VERIFY(next->isEnabled());
        next->click();
        QTRY_COMPARE(grid_->model()->rowCount(), 5);
        QTRY_VERIFY(filter_->isEnabled());
        choose(3, 1, 0);
        QTRY_COMPARE(grid_->model()->rowCount(), 2);
        QTRY_VERIFY(filter_->isEnabled());
        QCOMPARE(filter_->text(), QString("\"name\" = 'Bob'"));
        QCOMPARE(grid_->model()->index(0, 0).data().toString(), QString("2"));
        QCOMPARE(grid_->model()->index(1, 0).data().toString(), QString("1004"));
        QCOMPARE(grid_->horizontalHeader()->sortIndicatorSection(), 0);
        QCOMPARE(grid_->horizontalHeader()->sortIndicatorOrder(), Qt::AscendingOrder);
        QVERIFY(!object_->findChild<QPushButton*>("objectDataPrevious")->isEnabled());
        object_->findChild<QPushButton*>("resultFilterClear")->click();
        QTRY_COMPARE(grid_->model()->rowCount(), 1000);
        QTRY_VERIFY(filter_->isEnabled());
        QVERIFY(filter_->text().isEmpty());
        QCOMPARE(grid_->model()->index(0, 0).data().toString(), QString("1"));
    }
    void menuHoverAndKeyboardNavigation_data() {
        QTest::addColumn<bool>("dark");
        QTest::addColumn<bool>("keyboard");
        QTest::newRow("light-hover") << false << false;
        QTest::newRow("dark-hover") << true << false;
        QTest::newRow("light-keyboard") << false << true;
        QTest::newRow("dark-keyboard") << true << true;
    }
    void menuHoverAndKeyboardNavigation() {
        QFETCH(bool, dark);
        QFETCH(bool, keyboard);
        choscordb::design::ThemeManager theme;
        theme.setMode(dark ? choscordb::design::ThemeMode::Dark
                           : choscordb::design::ThemeMode::Light);
        theme.applyTo(*window_);
        menuAt(grid_->visualRect(grid_->model()->index(0, 1)).center(), [keyboard](QMenu* menu) {
            auto* quick = quickMenu(menu);
            QVERIFY(quick);
            if (keyboard) {
                menu->setActiveAction(quick->menuAction());
                QTest::keyClick(menu, Qt::Key_Right);
            } else {
                QTest::mouseMove(menu, menu->actionGeometry(menu->actions().last()).center());
                QTest::mouseMove(menu, menu->actionGeometry(quick->menuAction()).center());
            }
            QTRY_VERIFY(quick->isVisible());
            quick->setActiveAction(quick->actions().first());
            QTest::keyClick(quick, Qt::Key_Down);
            QCOMPARE(quick->activeAction(), quick->actions().at(1));
            QTest::keyClick(quick, Qt::Key_Escape);
        });
    }
};
QTEST_MAIN(QuickFilterWorkspaceTest)
#include "quick_filter_workspace_test.moc"
