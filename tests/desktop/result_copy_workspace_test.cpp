#include "app/query_workspace.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "models/result_table_model.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableView>
#include <QTimer>
#include <QtTest>
namespace {
choscordb::ResultColumn column(const QString& name) {
    choscordb::ResultColumn value{};
    value.name = name;
    return value;
}
} // namespace
class ResultCopyWorkspaceTest : public QObject {
    Q_OBJECT
  private slots:
    void stagedCellsAndRowsAreReversible() {
        choscordb::ResultTableModel model;
        QVERIFY(model.setPage({column("id"), column("name")},
                              {{qint64(1), QString("first")}, {qint64(2), QString("second")}}, 0));
        model.setEditableColumns({false, true}, true, true);
        QVERIFY(!(model.flags(model.index(0, 0)) & Qt::ItemIsEditable));
        QVERIFY(model.flags(model.index(0, 1)) & Qt::ItemIsEditable);
        QVERIFY(model.setData(model.index(0, 1), QString("changed")));
        QCOMPARE(model.index(0, 1).data().toString(), QString("changed"));
        QVERIFY(model.setNull(model.index(1, 1)));
        QCOMPARE(model.index(1, 1).data(Qt::UserRole).toBool(), true);
        QVERIFY(model.addRow());
        QCOMPARE(model.rowCount(), 3);
        QCOMPARE(model.index(2, 1).data(Qt::DisplayRole).toString(), QString(""));
        model.markDeleted({model.index(0, 0), model.index(1, 1)}, true);
        QVERIFY(model.hasPendingEdits());
        QVERIFY(model.deleted()[0] && model.deleted()[1]);
        model.markDeleted({model.index(0, 0), model.index(1, 0)}, false);
        QVERIFY(!model.deleted()[0] && !model.deleted()[1]);
        model.discardEdits();
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.index(0, 1).data().toString(), QString("first"));
        QVERIFY(!model.hasPendingEdits());
    }
    void repeatedEditReusesStagingBudget() {
        choscordb::ResultTableModel model;
        QVERIFY(model.setPage({column("value")}, {{QString("original")}}, 0));
        model.setEditableColumns({true}, false, false);
        QVERIFY(model.setByteBudget(model.residentBytes() + 200));
        const auto cell = model.index(0, 0);
        QVERIFY(model.setData(cell, QString(30, 'a')));
        QVERIFY(model.setData(cell, QString(30, 'b')));
        QCOMPARE(cell.data().toString(), QString(30, 'b'));
        QVERIFY(!model.setByteBudget(model.residentBytes()));
    }
    void rowAndPageScopesPreserveValuesAndBounds() {
        choscordb::ResultTableModel model;
        QVERIFY(model.setPage({column("a"), column("b")},
                              {{std::monostate{}, QString{}},
                               {QString("a\tb"), QByteArray::fromHex("00ff")},
                               {qint64(3), QString("\"x\"")}},
                              0));
        QString error;
        QCOMPARE(model.copyRows({model.index(2, 1), model.index(0, 0), model.index(2, 1)}, &error),
                 QString("NULL\t\n3\t\"\"\"x\"\"\""));
        QVERIFY(error.isEmpty());
        QCOMPARE(model.copyPage(&error), QString("NULL\t\n\"a\tb\"\t0x00ff\n3\t\"\"\"x\"\"\""));
        QVERIFY(model.setPage({column("a")}, {{choscordb::DeferredValue{1, 90000, "text"}}}, 0));
        QVERIFY(model.copyPage(&error).isEmpty());
        QVERIFY(error.contains("Export"));
        QVERIFY(model.setPage({column("a")}, {{QString(1024, QChar('"'))}}, 0));
        QVERIFY(model.setByteBudget(model.residentBytes()));
        QVERIFY(model.copyRows({model.index(0, 0)}, &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY(model.copyPage(&error).isEmpty());
        QVERIFY(!error.isEmpty());
    }
    void contextActionsUseCurrentSelectionAndNeverSubmitDatabaseWork() {
        QWidget window;
        QComboBox connections, mode;
        QAction run, cancel, commit, rollback, newConnection;
        QPushButton next, previous, exportResult;
        QLabel summary;
        QPlainTextEdit messages;
        QTableView grid;
        choscordb::SqlEditor editor;
        choscordb::QueryWorkspace workspace({&connections,
                                             &mode,
                                             &run,
                                             &cancel,
                                             &commit,
                                             &rollback,
                                             &newConnection,
                                             &next,
                                             &summary,
                                             &messages,
                                             &grid,
                                             [&] { return &editor; },
                                             &window,
                                             &previous,
                                             &exportResult,
                                             {}},
                                            &window);
        auto* model = qobject_cast<choscordb::ResultTableModel*>(grid.model());
        QVERIFY(model);
        QVERIFY(model->setPage({column("a"), column("b")},
                               {{qint64(1), QString("first")}, {qint64(2), QString("second")}}, 0));
        grid.selectionModel()->select(model->index(1, 0), QItemSelectionModel::Select);
        int queryEvents = 0;
        connect(workspace.adapter(), &choscordb::EngineAdapter::eventReady, &window,
                [&](const choscordb::BridgeEvent& event) {
                    const auto kind =
                        QString::fromUtf8(event.kind.data(), qsizetype(event.kind.size()));
                    if (kind.startsWith("query_") || kind == "schema" || kind == "stored_page" ||
                        kind == "connected")
                        ++queryEvents;
                });
        QSignalSpy failures(workspace.adapter(), &choscordb::EngineAdapter::commandFailed);
        QApplication::clipboard()->setText("unchanged");
        bool menuSeen = false;
        QTimer::singleShot(0, &window, [&] {
            auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
            QVERIFY(menu);
            menuSeen = true;
            auto* action = menu->findChild<QAction*>("copySelectedRows");
            QVERIFY(action);
            action->trigger();
            menu->close();
        });
        emit grid.customContextMenuRequested(QPoint(0, 0));
        QVERIFY(menuSeen);
        QCOMPARE(QApplication::clipboard()->text(), QString("2\tsecond"));
        QTimer::singleShot(0, &window, [&] {
            auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
            QVERIFY(menu);
            auto* action = menu->findChild<QAction*>("copyCurrentPage");
            if (!action) {
                menu->close();
                QFAIL("missing page action");
            }
            action->trigger();
            menu->close();
        });
        emit grid.customContextMenuRequested(QPoint());
        QCOMPARE(QApplication::clipboard()->text(), QString("1\tfirst\n2\tsecond"));
        QApplication::clipboard()->setText("keep on stale selection");
        QTimer::singleShot(0, &window, [&] {
            auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
            QVERIFY(menu);
            auto* action = menu->findChild<QAction*>("copySelectedCells");
            if (!action) {
                menu->close();
                QFAIL("missing cells action");
            }
            QVERIFY(model->setPage({column("replacement")}, {{qint64(99)}}, 0));
            action->trigger();
            menu->close();
        });
        emit grid.customContextMenuRequested(QPoint());
        QCOMPARE(QApplication::clipboard()->text(), QString("keep on stale selection"));
        QVERIFY(
            model->setPage({column("large")}, {{choscordb::DeferredValue{8, 100000, "text"}}}, 0));
        QTimer::singleShot(0, &window, [&] {
            auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
            QVERIFY(menu);
            auto* action = menu->findChild<QAction*>("copyCurrentPage");
            if (!action) {
                menu->close();
                QFAIL("missing page action");
            }
            action->trigger();
            menu->close();
        });
        emit grid.customContextMenuRequested(QPoint());
        QCOMPARE(QApplication::clipboard()->text(), QString("keep on stale selection"));
        QVERIFY(messages.toPlainText().contains("Export"));
        QCOMPARE(queryEvents, 0);
        QCOMPARE(failures.count(), 0);
    }
};
QTEST_MAIN(ResultCopyWorkspaceTest)
#include "result_copy_workspace_test.moc"
