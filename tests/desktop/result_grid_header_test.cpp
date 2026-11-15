#include "app/main_window.h"
#include "app/query_workspace.h"
#include "design_system/menu/embedded_popup.h"
#include "models/result_table_model.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAction>
#include <QHeaderView>
#include <QMenu>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QTableView>
#include <QtTest>
#include <memory>

class ResultGridHeaderTest : public QObject {
    Q_OBJECT
    static QTableView* runQuery(choscordb::MainWindow& window, const QString& sql) {
        window.show();
        window.findChild<choscordb::QueryWorkspace*>()->connectSqlite(":memory:");
        if (!QTest::qWaitFor([&] { return window.findChild<QAction*>("newQuery")->isEnabled(); }))
            return nullptr;
        window.findChild<QAction*>("newQuery")->trigger();
        auto* editor = qobject_cast<choscordb::SqlEditor*>(
            window.findChild<QTabWidget*>("editorTabs")->currentWidget());
        auto* run = window.findChild<QAction*>("runStatement");
        if (!editor || !QTest::qWaitFor([run] { return run->isEnabled(); }))
            return nullptr;
        editor->setText(sql);
        run->trigger();
        return window.findChild<QTableView*>("queryResults");
    }
    static QStringList rows(QTableView* grid) {
        QStringList output;
        for (int row = 0; row < grid->model()->rowCount(); ++row)
            output << grid->model()->index(row, 0).data().toString() + ":" +
                          grid->model()->index(row, 1).data().toString();
        return output;
    }
    static void click(QHeaderView* header, int section, Qt::KeyboardModifiers modifiers = {}) {
        QTest::mouseClick(header->viewport(), Qt::LeftButton, modifiers,
                          QPoint(header->sectionViewportPosition(section) + 10, 5));
    }
    static void chooseColumn(QTableView* grid, const QString& name) {
        QTimer pick;
        connect(&pick, &QTimer::timeout, grid, [&] {
            auto* menu = qobject_cast<QMenu*>(choscordb::design::detail::activeEmbeddedPopup());
            if (!menu || menu->objectName() != "resultColumnChooser")
                return;
            for (auto* action : menu->actions())
                if (action->text() == name)
                    action->trigger();
            menu->close();
        });
        pick.start(10);
        grid->findChild<QAction*>("chooseColumns")->trigger();
    }
    static int rank(QTableView* grid, int section) {
        return grid->model()
            ->headerData(section, Qt::Horizontal, choscordb::ResultTableModel::HeaderSortRole)
            .toInt();
    }

  private slots:
    void shiftClickAddsASecondSortKey() {
        choscordb::MainWindow window;
        auto* grid = runQuery(window, "SELECT 2 AS a, 'x' AS b UNION ALL SELECT 1, 'y' UNION "
                                      "ALL SELECT 2, 'z' UNION ALL SELECT 1, 'w'");
        QVERIFY(grid);
        QTRY_COMPARE(grid->model()->rowCount(), 4);
        auto busy = std::make_shared<bool>(false);
        connect(window.findChild<choscordb::QueryWorkspace*>(),
                &choscordb::QueryWorkspace::activityChanged, this,
                [busy](bool active) { *busy = active; });
        QTRY_VERIFY(!*busy);
        auto* header = grid->horizontalHeader();
        click(header, 0);
        QTRY_COMPARE(rank(grid, 0), 1);
        QTRY_VERIFY(!*busy);
        click(header, 1, Qt::ShiftModifier);
        QTRY_COMPARE(rank(grid, 1), 2);
        QTRY_COMPARE(rows(grid), (QStringList{"1:w", "1:y", "2:x", "2:z"}));
        QTRY_VERIFY(!*busy);
        click(header, 1, Qt::ShiftModifier);
        QTRY_COMPARE(rank(grid, 1), -2);
        QTRY_COMPARE(rows(grid), (QStringList{"1:y", "1:w", "2:z", "2:x"}));
        QCOMPARE(rank(grid, 0), 1);
        QTRY_VERIFY(!*busy);
        click(header, 0);
        QTRY_COMPARE(rank(grid, 0), -1);
        QCOMPARE(rank(grid, 1), 0);
        QTRY_COMPARE(rows(grid).first().left(2), QString("2:"));
    }
    void columnChooserHidesColumnsButKeepsOneVisible() {
        choscordb::MainWindow window;
        auto* grid = runQuery(window, "SELECT 1 AS a, 2 AS b");
        QVERIFY(grid);
        QTRY_COMPARE(grid->model()->rowCount(), 1);
        auto* chooser = grid->findChild<QAction*>("chooseColumns");
        QVERIFY(chooser);
        QCOMPARE(chooser->shortcut(), QKeySequence("Ctrl+Alt+F"));
        QTRY_VERIFY(chooser->isEnabled());
        auto* header = grid->horizontalHeader();
        chooseColumn(grid, "b");
        QVERIFY(header->isSectionHidden(1));
        QVERIFY(!header->isSectionHidden(0));
        chooseColumn(grid, "a");
        QVERIFY(!header->isSectionHidden(0));
        QVERIFY(window.findChild<QPlainTextEdit*>("queryMessages")
                    ->toPlainText()
                    .contains("The result must show at least one column"));
        chooseColumn(grid, "Show all columns");
        QVERIFY(!header->isSectionHidden(1));
    }
};

QTEST_MAIN(ResultGridHeaderTest)
#include "result_grid_header_test.moc"
