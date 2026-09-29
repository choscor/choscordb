#include "app/main_window.h"
#include "app/query_settings.h"
#include "app/query_workspace.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/menu/embedded_popup.h"
#include "models/result_table_model.h"
#include "widgets/sql_editor/sql_editor.h"
#include "widgets/value_detail_dialog/value_detail_dialog.h"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
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
        grid.resize(640, 480);
        grid.show();
        grid.selectionModel()->select(model->index(1, 0), QItemSelectionModel::Select);
        // Initial preference loading is unrelated to the context actions under test.
        auto* settings = workspace.findChild<choscordb::QuerySettingsController*>();
        QVERIFY(settings);
        QTRY_VERIFY(settings->isReady());
        QStringList queryEvents;
        connect(workspace.adapter(), &choscordb::EngineAdapter::eventReady, &window,
                [&](const choscordb::BridgeEvent& event) {
                    const auto kind =
                        QString::fromUtf8(event.kind.data(), qsizetype(event.kind.size()));
                    if (kind.startsWith("query_") || kind == "schema" || kind == "stored_page" ||
                        kind == "connected")
                        queryEvents.append(kind);
                });
        QSignalSpy failures(workspace.adapter(), &choscordb::EngineAdapter::commandFailed);
        QApplication::clipboard()->setText("unchanged");
        bool menuSeen = false;
        QTimer::singleShot(0, &window, [&] {
            auto* menu = qobject_cast<QMenu*>(choscordb::design::detail::activeEmbeddedPopup());
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
            auto* menu = qobject_cast<QMenu*>(choscordb::design::detail::activeEmbeddedPopup());
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
            auto* menu = qobject_cast<QMenu*>(choscordb::design::detail::activeEmbeddedPopup());
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
            auto* menu = qobject_cast<QMenu*>(choscordb::design::detail::activeEmbeddedPopup());
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
        QVERIFY(messages.toPlainText().contains("no longer available"));
        QVERIFY(model->setPage({column("unreadable")},
                               {{choscordb::UnavailableValue{"odd_type", "text output failed"}}},
                               0));
        QTimer::singleShot(0, &window, [&] {
            auto* menu = qobject_cast<QMenu*>(choscordb::design::detail::activeEmbeddedPopup());
            QVERIFY(menu);
            auto* action = menu->findChild<QAction*>("copyCurrentPage");
            QVERIFY(action);
            action->trigger();
            menu->close();
        });
        emit grid.customContextMenuRequested(QPoint());
        QCOMPARE(QApplication::clipboard()->text(), QString("keep on stale selection"));
        QVERIFY(messages.toPlainText().contains("odd_type"));
        QVERIFY2(queryEvents.isEmpty(), qPrintable(queryEvents.join(", ")));
        QCOMPARE(failures.count(), 0);
    }
    void sqlResultsCopyCompleteDeferredValue() {
        choscordb::MainWindow window;
        window.show();
        auto* sql = window.findChild<choscordb::QueryWorkspace*>();
        QVERIFY(sql);
        sql->connectSqlite(":memory:");
        QTRY_VERIFY(window.findChild<QAction*>("newQuery")->isEnabled());
        window.findChild<QAction*>("newQuery")->trigger();
        auto* run = window.findChild<QAction*>("runStatement");
        auto* editor = qobject_cast<choscordb::SqlEditor*>(
            window.findChild<QTabWidget*>("editorTabs")->currentWidget());
        auto* grid = window.findChild<QTableView*>("queryResults");
        QVERIFY(run && editor && grid);
        QTRY_VERIFY(run->isEnabled());
        editor->setText("SELECT zeroblob(70000) AS payload, 7 AS id");
        run->trigger();
        QTRY_COMPARE(grid->model()->rowCount(), 1);
        const auto index = grid->model()->index(0, 0);
        QVERIFY(grid->model()->data(index).toString().contains("open to load"));
        grid->setCurrentIndex(index);
        grid->setFocus();
        QApplication::clipboard()->setText("original clipboard");
        QTest::keySequence(grid, QKeySequence::Copy);
        QTRY_COMPARE(QApplication::clipboard()->text(),
                     QStringLiteral("0x") + QString(140000, QChar('0')));
        grid->setCurrentIndex(index);
        QApplication::clipboard()->setText("preserve on selection change");
        QTest::keySequence(grid, QKeySequence::Copy);
        grid->setCurrentIndex(grid->model()->index(0, 1));
        QTest::qWait(200);
        QCOMPARE(QApplication::clipboard()->text(), QString("preserve on selection change"));
    }
    void sqlResultsCopyCompleteDeferredFallbackText() {
        choscordb::MainWindow window;
        window.show();
        auto* sql = window.findChild<choscordb::QueryWorkspace*>();
        QVERIFY(sql);
        sql->connectSqlite(":memory:");
        QTRY_VERIFY(window.findChild<QAction*>("newQuery")->isEnabled());
        window.findChild<QAction*>("newQuery")->trigger();
        auto* run = window.findChild<QAction*>("runStatement");
        auto* editor = qobject_cast<choscordb::SqlEditor*>(
            window.findChild<QTabWidget*>("editorTabs")->currentWidget());
        auto* grid = window.findChild<QTableView*>("queryResults");
        QVERIFY(run && editor && grid);
        QTRY_VERIFY(run->isEnabled());
        editor->setText("SELECT printf('%070000d', 0) AS unknown_value");
        run->trigger();
        QTRY_COMPARE(grid->model()->rowCount(), 1);
        auto* model = qobject_cast<choscordb::ResultTableModel*>(grid->model());
        QVERIFY(model);
        const auto deferred = model->deferredValue(model->index(0, 0));
        QVERIFY(deferred);
        QCOMPARE(deferred->bytes, quint64(70000));
        choscordb::ResultColumn column{};
        column.name = "unknown_value";
        column.databaseType = "custom_type";
        QVERIFY(model->setPage(
            {column},
            {{choscordb::DeferredValue{deferred->handle, deferred->bytes, "custom_type", true}}},
            0));
        const auto index = model->index(0, 0);
        QCOMPARE(index.data(choscordb::ResultTableModel::ResultValueKindRole).toString(),
                 QString("deferred_fallback"));
        grid->setCurrentIndex(index);
        grid->setFocus();
        QApplication::clipboard()->setText("old clipboard");
        QTest::keySequence(grid, QKeySequence::Copy);
        QTRY_COMPARE(QApplication::clipboard()->text(), QString(70000, QChar('0')));
        QTimer::singleShot(0, &window, [&] {
            auto* menu = qobject_cast<QMenu*>(choscordb::design::detail::activeEmbeddedPopup());
            QVERIFY(menu);
            auto* action = menu->findChild<QAction*>("viewRowJson");
            QVERIFY(action && action->isEnabled());
            action->trigger();
            menu->close();
        });
        emit grid->customContextMenuRequested(grid->visualRect(index).center());
        auto* sheet = window.findChild<QDialog*>("rowJsonSheet");
        QVERIFY(sheet);
        QTRY_VERIFY(sheet->isVisible());
        auto* jsonText = sheet->findChild<QPlainTextEdit*>("rowJsonText");
        QVERIFY(jsonText);
        QTRY_VERIFY(!jsonText->toPlainText().isEmpty());
        const auto fallback = QJsonDocument::fromJson(jsonText->toPlainText().toUtf8())
                                  .object()
                                  .value("unknown_value")
                                  .toObject();
        QCOMPARE(fallback.value("fallback_text").toString(), QString(70000, QChar('0')));
        QCOMPARE(fallback.value("database_type").toString(), QString("custom_type"));
        sheet->reject();
    }
    void inlineFallbackOpensCompleteReadOnlyDetail() {
        QWidget window;
        window.show();
        QComboBox connections, mode;
        QAction run, cancel, commit, rollback, newConnection;
        QPushButton next;
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
                                             nullptr,
                                             nullptr,
                                             {}},
                                            &window);
        auto* model = qobject_cast<choscordb::ResultTableModel*>(grid.model());
        QVERIFY(model);
        auto resultColumn = column("unfamiliar");
        resultColumn.databaseType = "custom_type";
        const QString complete = QString(180, QChar('x')) + QStringLiteral("\tend");
        QVERIFY(model->setPage({resultColumn}, {{choscordb::FallbackText{complete, "custom_type"}}},
                               0));
        const auto index = model->index(0, 0);
        QVERIFY(index.data().toString().size() < complete.size());
        grid.resize(640, 480);
        grid.show();
        emit grid.doubleClicked(index);
        auto* detail = window.findChild<choscordb::ValueDetailDialog*>("valueDetail");
        QVERIFY(detail);
        QVERIFY(detail->isVisible());
        QVERIFY(detail->windowTitle().contains("custom_type"));
        QVERIFY(detail->windowTitle().contains("fallback", Qt::CaseInsensitive));
        auto* preview = detail->findChild<QTableView*>("valuePreview");
        QVERIFY(preview);
        QVERIFY(preview->editTriggers() == QAbstractItemView::NoEditTriggers);
        QString shown;
        for (int row = 0; row < preview->model()->rowCount(); ++row)
            shown += preview->model()->index(row, 1).data().toString();
        QCOMPARE(shown, QString(180, QChar('x')) + QStringLiteral("\\tend"));
        auto binaryColumn = column("payload");
        binaryColumn.databaseType = "BLOB";
        QVERIFY(model->setPage({binaryColumn}, {{QByteArray::fromHex("00ff7f")}}, 0));
        emit grid.doubleClicked(model->index(0, 0));
        QVERIFY(detail->isVisible());
        QVERIFY(detail->windowTitle().contains("BLOB"));
        QCOMPARE(preview->model()->index(0, 1).data().toString(), QString("00 ff 7f"));
        const QString large = QString(65535, QChar('y')) + QStringLiteral("🙂tail");
        QVERIFY(
            model->setPage({resultColumn}, {{choscordb::FallbackText{large, "custom_type"}}}, 0));
        emit grid.doubleClicked(model->index(0, 0));
        auto* nextChunk = detail->findChild<QPushButton*>("valueNext");
        auto* previousChunk = detail->findChild<QPushButton*>("valuePrevious");
        auto* status = detail->findChild<QLabel*>("valueStatus");
        QVERIFY(nextChunk && previousChunk && status);
        QVERIFY(nextChunk->isEnabled());
        nextChunk->click();
        QCOMPARE(preview->model()->index(0, 1).data().toString(), QStringLiteral("🙂tail"));
        QVERIFY(previousChunk->isEnabled());
        previousChunk->click();
        QVERIFY(status->text().contains("Bytes 0"));
        QVERIFY(preview->model()->index(0, 1).data().toString().startsWith('y'));
    }
};
QTEST_MAIN(ResultCopyWorkspaceTest)
#include "result_copy_workspace_test.moc"
