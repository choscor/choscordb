#include "app/appearance_controller.h"
#include "app/main_window.h"
#include "app/main_window_widgets.h"
#include "app/navigator_controller.h"
#include "app/object_explorer.h"
#include "app/query_workspace.h"
#include "app/workspace_recovery.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/button/button.h"
#include "design_system/dialog_presentation/dialog_presentation.h"
#include "design_system/history_row/history_row.h"
#include "design_system/json_text_view/json_text_view.h"
#include "design_system/menu/embedded_popup.h"
#include "design_system/menu/menu.h"
#include "design_system/theme.h"
#include "design_system/theme_manager.h"
#include "design_system/toast_region/toast_region.h"
#include "models/navigator_model.h"
#include "models/result_table_model.h"
#include "navigator_sql_workspace_test.h"
#include "widgets/history_dock/history_dock.h"
#include "widgets/profile_dialog/profile_dialog.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAction>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QTabBar>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QTimer>
#include <QToolBar>
#include <QTreeView>
#include <QTreeWidget>
#include <QtTest>

void NavigatorSqlWorkspaceTest::sqlCellJsonUsesClickedCellAndValidatesEligibility() {
    QTemporaryDir storage;
    choscordb::MainWindow window(nullptr, storage.filePath("settings.sqlite"));
    window.show();
    QTRY_VERIFY(window.findChild<choscordb::WorkspaceRecoveryController*>()->isReady());
    window.findChild<QAction*>("newQuery")->trigger();
    auto* grid = window.findChild<QTableView*>("queryResults");
    auto* model = qobject_cast<choscordb::ResultTableModel*>(grid->model());
    QVERIFY(model);
    const auto column = [](QString name, QString type) {
        choscordb::ResultColumn result{};
        result.name = std::move(name);
        result.databaseType = std::move(type);
        return result;
    };
    QVERIFY(model->setPage({column("payload", "jsonb")},
                           {{QString("{\"wrong\":0}")}, {QString("{\"items\":[1,true,null]}")}},
                           0));
    grid->setCurrentIndex(model->index(0, 0));
    const auto selected = grid->currentIndex();
    const auto inspect = [&](int row, bool trigger) {
        bool found = false, enabled = false;
        QTimer::singleShot(0, grid, [&] {
            auto* menu = qobject_cast<QMenu*>(choscordb::design::detail::activeEmbeddedPopup());
            if (!menu) {
                if (auto* popup = QApplication::activePopupWidget())
                    popup->close();
                return;
            }
            auto* action = menu->findChild<QAction*>("viewCellJson");
            found = action != nullptr;
            if (trigger && action)
                QTRY_VERIFY(action->isEnabled());
            enabled = action && action->isEnabled();
            menu->close();
            if (trigger && enabled)
                action->trigger();
        });
        grid->customContextMenuRequested(grid->visualRect(model->index(row, 0)).center());
        return found && enabled;
    };
    QVERIFY(inspect(1, true));
    auto* sheet = window.findChild<QDialog*>("rowJsonSheet");
    QVERIFY(sheet);
    auto* copy = sheet->findChild<QPushButton*>("rowJsonCopy");
    QVERIFY(copy);
    QVERIFY(!copy->isEnabled()); // Rendering completes after the UI event returns.
    QTRY_VERIFY(sheet->isVisible());
    QTRY_COMPARE(sheet->findChild<QLabel*>("rowJsonStatus")->text(), QString());
    QVERIFY(!sheet->findChild<QLabel*>("rowJsonStatus")->isVisible());
    auto* text = sheet->findChild<QPlainTextEdit*>("rowJsonText");
    QVERIFY(text);
    QTRY_VERIFY(copy->isEnabled());
    QCOMPARE(QJsonDocument::fromJson(text->toPlainText().toUtf8())
                 .object()
                 .value("items")
                 .toArray()
                 .at(1)
                 .toBool(),
             true);
    QCOMPARE(grid->currentIndex(), selected);
    copy->click();
    QCOMPARE(QApplication::clipboard()->text(), text->toPlainText());
    sheet->reject();

    model->setEditableColumns({true}, false, false);
    QVERIFY(model->setData(model->index(1, 0), QString("{broken")));
    QVERIFY(inspect(1, true));
    QTRY_VERIFY(sheet->isVisible());
    QVERIFY(text->toPlainText().isEmpty());
    QVERIFY(!copy->isEnabled());
    QTRY_VERIFY(sheet->findChild<QLabel*>("rowJsonStatus")
                    ->text()
                    .contains("invalid", Qt::CaseInsensitive));
    sheet->reject();

    QVERIFY(model->setPage({column("value", "text")}, {{QString("42")}}, 0));
    QVERIFY(inspect(0, true));
    QTRY_VERIFY(sheet->isVisible());
    QTRY_COMPARE(text->toPlainText().trimmed(), QString("42"));
    sheet->reject();
    QVERIFY(model->setPage({column("value", "text")}, {{QString("{broken")}}, 0));
    QVERIFY(!inspect(0, false));
    QVERIFY(model->setPage({column("value", "integer")}, {{qint64(42)}}, 0));
    QVERIFY(!inspect(0, false));
    QVERIFY(model->setPage({column("value", "json")}, {{std::monostate{}}}, 0));
    QVERIFY(!inspect(0, false));
}

void NavigatorSqlWorkspaceTest::sqlTableJsonIncludesLoadedPageFromBlankSpace() {
    QTemporaryDir storage;
    choscordb::MainWindow window(nullptr, storage.filePath("settings.sqlite"));
    window.show();
    QTRY_VERIFY(window.findChild<choscordb::WorkspaceRecoveryController*>()->isReady());
    window.findChild<QAction*>("newQuery")->trigger();
    auto* grid = window.findChild<QTableView*>("queryResults");
    auto* model = qobject_cast<choscordb::ResultTableModel*>(grid->model());
    QVERIFY(model);
    choscordb::ResultColumn id{}, payload{}, total{};
    id.name = "id";
    id.databaseType = "integer";
    payload.name = "payload";
    payload.databaseType = "jsonb";
    total.name = "total";
    total.databaseType = "numeric";
    QVERIFY(model->setPage(
        {id, payload, total},
        {{qint64(7), QString("{\"nested\":true}"), choscordb::DecimalValue{"377.00"}},
         {qint64(8), QString("[1,2]"), choscordb::DecimalValue{"12345678901234567890.123456789"}}},
        200));
    model->setEditableColumns({true, true, true}, true, true);
    QVERIFY(model->setData(model->index(1, 0), QString("9")));
    grid->selectRow(0);
    const auto selected = grid->selectionModel()->selectedIndexes();
    const QPoint blank(grid->viewport()->width() - 2, grid->viewport()->height() - 2);
    QVERIFY(!grid->indexAt(blank).isValid());
    QTimer::singleShot(0, grid, [&] {
        auto* menu = qobject_cast<QMenu*>(choscordb::design::detail::activeEmbeddedPopup());
        QVERIFY(menu);
        auto* row = menu->findChild<QAction*>("viewRowJson");
        auto* table = menu->findChild<QAction*>("viewTableJson");
        if (!row || !table) {
            menu->close();
            QFAIL("JSON row or table action is missing");
        }
        QVERIFY(!row->isEnabled());
        QVERIFY(table->isEnabled());
        QCOMPARE(menu->actions().indexOf(table), menu->actions().indexOf(row) + 1);
        menu->close();
        table->trigger();
    });
    grid->customContextMenuRequested(blank);
    auto* sheet = window.findChild<QDialog*>("rowJsonSheet");
    QVERIFY(sheet);
    QTRY_VERIFY(sheet->isVisible());
    auto* text = sheet->findChild<QPlainTextEdit*>("rowJsonText");
    auto* copy = sheet->findChild<QPushButton*>("rowJsonCopy");
    QVERIFY(text && copy);
    QTRY_VERIFY(copy->isEnabled());
    const auto parsed = QJsonDocument::fromJson(text->toPlainText().toUtf8());
    QVERIFY(parsed.isArray());
    QCOMPARE(parsed.array().size(), 2);
    QCOMPARE(parsed.array().at(0).toObject().value("id").toInt(), 7);
    QCOMPARE(parsed.array().at(0).toObject().value("payload").toObject().value("nested").toBool(),
             true);
    QCOMPARE(parsed.array().at(1).toObject().value("id").toInt(), 9);
    QCOMPARE(parsed.array().at(1).toObject().value("payload").toArray().at(1).toInt(), 2);
    QCOMPARE(parsed.array().at(0).toObject().value("total").toString(), QString("377.00"));
    QCOMPARE(parsed.array().at(1).toObject().value("total").toString(),
             QString("12345678901234567890.123456789"));
    QVERIFY(!sheet->findChild<QLabel*>("rowJsonPageNote"));
    for (auto* label : sheet->findChildren<QLabel*>())
        QVERIFY(label->accessibleName() != QString("Omitted field explanation"));
    QVERIFY(!sheet->findChild<QLabel*>("rowJsonStatus")->isVisible());
    QCOMPARE(grid->selectionModel()->selectedIndexes(), selected);
    QVERIFY(model->hasPendingEdits());
    copy->click();
    QCOMPARE(QApplication::clipboard()->text(), text->toPlainText());
    sheet->reject();
}

void NavigatorSqlWorkspaceTest::sqlCellAndTableJsonLoadFullDeferredText() {
    QTemporaryDir storage;
    choscordb::MainWindow window(nullptr, storage.filePath("settings.sqlite"));
    window.show();
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    workspace->connectSqlite(":memory:");
    QTRY_VERIFY(window.findChild<QAction*>("newQuery")->isEnabled());
    window.findChild<QAction*>("newQuery")->trigger();
    auto* editor = qobject_cast<choscordb::SqlEditor*>(
        window.findChild<QTabWidget*>("editorTabs")->currentWidget());
    auto* run = window.findChild<QAction*>("runStatement");
    auto* grid = window.findChild<QTableView*>("queryResults");
    auto* model = qobject_cast<choscordb::ResultTableModel*>(grid->model());
    QVERIFY(editor && run && grid && model);
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
            if (action)
                QTRY_VERIFY(action->isEnabled());
            enabled = action && action->isEnabled();
            menu->close();
            if (enabled)
                action->trigger();
        });
        grid->customContextMenuRequested(point);
        return enabled;
    };
    QTRY_VERIFY(run->isEnabled());
    editor->setText("SELECT '\"' || substr(replace(hex(zeroblob(32768)), '0', 'a'),1,65535) "
                    "|| '\"' AS payload;");
    run->trigger();
    QTRY_VERIFY(model->rowCount() == 1 && model->deferredValue(model->index(0, 0)));
    QCOMPARE(model->deferredValue(model->index(0, 0))->bytes, quint64(65537));
    const QString source =
        QStringLiteral("\"") + QString(65535, QLatin1Char('a')) + QStringLiteral("\"");
    QVERIFY(trigger("viewCellJson", grid->visualRect(model->index(0, 0)).center()));
    auto* sheet = window.findChild<QDialog*>("rowJsonSheet");
    QVERIFY(sheet);
    auto* text = sheet->findChild<QPlainTextEdit*>("rowJsonText");
    auto* copy = sheet->findChild<QPushButton*>("rowJsonCopy");
    QVERIFY(text && copy);
    QVERIFY(!copy->isEnabled());
    QTRY_VERIFY(copy->isEnabled());
    QCOMPARE(text->toPlainText(), source);
    copy->click();
    QCOMPARE(QApplication::clipboard()->text(), source);
    sheet->reject();

    const QPoint blank(grid->viewport()->width() - 2, grid->viewport()->height() - 2);
    QVERIFY(!grid->indexAt(blank).isValid());
    QVERIFY(trigger("viewTableJson", blank));
    QVERIFY(!copy->isEnabled());
    QTRY_VERIFY(copy->isEnabled());
    const auto parsed = QJsonDocument::fromJson(text->toPlainText().toUtf8());
    QVERIFY(parsed.isArray());
    QCOMPARE(parsed.array().size(), 1);
    QCOMPARE(parsed.array().at(0).toObject().value("payload").toString(), source);
    sheet->reject();

    QTRY_VERIFY(run->isEnabled());
    editor->setText("SELECT '{' || substr(replace(hex(zeroblob(32768)), '0', 'a'),1,65535) "
                    "AS payload;");
    run->trigger();
    QTRY_VERIFY(model->rowCount() == 1 && model->deferredValue(model->index(0, 0)));
    QVERIFY(trigger("viewCellJson", grid->visualRect(model->index(0, 0)).center()));
    QTRY_VERIFY(sheet->findChild<QLabel*>("rowJsonStatus")
                    ->text()
                    .contains("Unable to show complete JSON"));
    QVERIFY(text->toPlainText().isEmpty());
    QVERIFY(!copy->isEnabled());
    sheet->reject();
    QTRY_VERIFY(workspace->adapter()->memoryUsage().used < 2 * 1024 * 1024);
}

void NavigatorSqlWorkspaceTest::sqlMalformedRowJsonShowsErrorWithoutCopy() {
    QTemporaryDir storage;
    choscordb::MainWindow window(nullptr, storage.filePath("settings.sqlite"));
    window.show();
    QTRY_VERIFY(window.findChild<choscordb::WorkspaceRecoveryController*>()->isReady());
    window.findChild<QAction*>("newQuery")->trigger();
    auto* grid = window.findChild<QTableView*>("queryResults");
    auto* model = qobject_cast<choscordb::ResultTableModel*>(grid->model());
    QVERIFY(model);
    choscordb::ResultColumn column{};
    column.name = "payload";
    column.databaseType = "jsonb";
    QVERIFY(model->setPage({column}, {{QString("{}")}}, 0));
    model->setEditableColumns({true}, false, false);
    QVERIFY(model->setData(model->index(0, 0), QString("{bad")));
    bool enabled = false;
    QTimer::singleShot(0, grid, [&] {
        auto* menu = qobject_cast<QMenu*>(choscordb::design::detail::activeEmbeddedPopup());
        if (!menu) {
            if (auto* popup = QApplication::activePopupWidget())
                popup->close();
            return;
        }
        auto* action = menu->findChild<QAction*>("viewRowJson");
        if (action)
            QTRY_VERIFY(action->isEnabled());
        enabled = action && action->isEnabled();
        menu->close();
        if (enabled)
            action->trigger();
    });
    grid->customContextMenuRequested(grid->visualRect(model->index(0, 0)).center());
    QVERIFY(enabled);
    auto* sheet = window.findChild<QDialog*>("rowJsonSheet");
    QVERIFY(sheet);
    QTRY_VERIFY(sheet->isVisible());
    QTRY_VERIFY(sheet->findChild<QLabel*>("rowJsonStatus")
                    ->text()
                    .contains("invalid", Qt::CaseInsensitive));
    QVERIFY(sheet->findChild<QPlainTextEdit*>("rowJsonText")->toPlainText().isEmpty());
    QVERIFY(!sheet->findChild<QPushButton*>("rowJsonCopy")->isEnabled());
    QVERIFY(model->hasPendingEdits());
    sheet->reject();
}

void NavigatorSqlWorkspaceTest::sqlJsonViewsCloseAndDisableAfterDisconnect() {
    QTemporaryDir storage;
    choscordb::MainWindow window(nullptr, storage.filePath("settings.sqlite"));
    window.show();
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    QSignalSpy connected(workspace, &choscordb::QueryWorkspace::connectionReady);
    workspace->connectSqlite(":memory:");
    QTRY_VERIFY(!connected.isEmpty());
    const auto connection = connected.last().at(0).toULongLong();
    QTRY_VERIFY(window.findChild<QAction*>("newQuery")->isEnabled());
    window.findChild<QAction*>("newQuery")->trigger();
    auto* editor = qobject_cast<choscordb::SqlEditor*>(
        window.findChild<QTabWidget*>("editorTabs")->currentWidget());
    auto* run = window.findChild<QAction*>("runStatement");
    auto* grid = window.findChild<QTableView*>("queryResults");
    auto* model = qobject_cast<choscordb::ResultTableModel*>(grid->model());
    QVERIFY(editor && run && model);
    QTRY_VERIFY(run->isEnabled());
    editor->setText("SELECT '{\"x\":1}' AS payload;");
    run->trigger();
    QTRY_COMPARE(model->rowCount(), 1);
    model->setEditableColumns({true}, false, false);
    QVERIFY(model->setData(model->index(0, 0), QString("{\"x\":2}")));
    const auto point = grid->visualRect(model->index(0, 0)).center();
    QTimer::singleShot(0, grid, [&] {
        auto* menu = qobject_cast<QMenu*>(choscordb::design::detail::activeEmbeddedPopup());
        QVERIFY(menu);
        auto* action = menu->findChild<QAction*>("viewCellJson");
        if (action)
            QTRY_VERIFY(action->isEnabled());
        const bool enabled = action && action->isEnabled();
        menu->close();
        QVERIFY(enabled);
        action->trigger();
    });
    grid->customContextMenuRequested(point);
    auto* sheet = window.findChild<QDialog*>("rowJsonSheet");
    QVERIFY(sheet);
    QTRY_VERIFY(sheet->isVisible());
    QTRY_VERIFY(sheet->findChild<QPushButton*>("rowJsonCopy")->isEnabled());
    QVERIFY(workspace->adapter()->disconnectConnection(connection));
    QTRY_VERIFY(!sheet->isVisible());
    QCOMPARE(model->rowCount(), 1); // The existing grid still displays its last page.
    QTimer::singleShot(0, grid, [&] {
        auto* menu = qobject_cast<QMenu*>(choscordb::design::detail::activeEmbeddedPopup());
        QVERIFY(menu);
        for (const char* name : {"viewCellJson", "viewRowJson", "viewTableJson"}) {
            auto* action = menu->findChild<QAction*>(name);
            QVERIFY(action);
            QVERIFY(!action->isEnabled());
        }
        menu->close();
    });
    grid->customContextMenuRequested(point);
    model->discardEdits(); // A model reset must not make the disconnected page current again.
    bool staleActionEnabled = false, sawMenu = false;
    QTimer::singleShot(0, grid, [&] {
        auto* menu = qobject_cast<QMenu*>(choscordb::design::detail::activeEmbeddedPopup());
        if (menu) {
            sawMenu = true;
            for (const char* name : {"viewCellJson", "viewRowJson", "viewTableJson"}) {
                auto* action = menu->findChild<QAction*>(name);
                staleActionEnabled |= !action || action->isEnabled();
            }
            menu->close();
        }
    });
    grid->customContextMenuRequested(grid->visualRect(model->index(0, 0)).center());
    QVERIFY(sawMenu);
    QVERIFY(!staleActionEnabled);
}

void NavigatorSqlWorkspaceTest::sqlTableJsonTracksRealPageControls() {
    QTemporaryDir storage;
    choscordb::MainWindow window(nullptr, storage.filePath("settings.sqlite"));
    window.show();
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    workspace->connectSqlite(":memory:");
    QTRY_VERIFY(window.findChild<QAction*>("newQuery")->isEnabled());
    window.findChild<QAction*>("newQuery")->trigger();
    auto* editor = qobject_cast<choscordb::SqlEditor*>(
        window.findChild<QTabWidget*>("editorTabs")->currentWidget());
    auto* run = window.findChild<QAction*>("runStatement");
    auto* grid = window.findChild<QTableView*>("queryResults");
    auto* next = window.findChild<QPushButton*>("nextPage");
    auto* model = qobject_cast<choscordb::ResultTableModel*>(grid->model());
    QVERIFY(editor && run && next && model);
    QTRY_VERIFY(run->isEnabled());
    editor->setText("WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE "
                    "x<1001) SELECT x AS id FROM n;");
    run->trigger();
    QTRY_COMPARE(model->rowCount(), 1000);
    QTRY_VERIFY(next->isEnabled());
    const auto viewPage = [&](const QPoint& point) {
        bool enabled = false;
        QTimer::singleShot(0, grid, [&] {
            auto* menu = qobject_cast<QMenu*>(choscordb::design::detail::activeEmbeddedPopup());
            if (!menu) {
                if (auto* popup = QApplication::activePopupWidget())
                    popup->close();
                return;
            }
            auto* action = menu->findChild<QAction*>("viewTableJson");
            enabled = action && action->isEnabled();
            menu->close();
            if (enabled)
                action->trigger();
        });
        grid->customContextMenuRequested(point);
        return enabled;
    };
    QVERIFY(viewPage(grid->visualRect(model->index(0, 0)).center()));
    auto* sheet = window.findChild<QDialog*>("rowJsonSheet");
    QVERIFY(sheet);
    QTRY_VERIFY(sheet->isVisible());
    auto* text = sheet->findChild<QPlainTextEdit*>("rowJsonText");
    QVERIFY(text);
    QTRY_VERIFY(sheet->findChild<QPushButton*>("rowJsonCopy")->isEnabled());
    auto page = QJsonDocument::fromJson(text->toPlainText().toUtf8());
    QVERIFY(page.isArray());
    QCOMPARE(page.array().size(), 1000);
    QCOMPARE(page.array().first().toObject().value("id").toInt(), 1);
    QCOMPARE(page.array().last().toObject().value("id").toInt(), 1000);
    sheet->reject();
    next->click();
    QTRY_COMPARE(model->rowCount(), 1);
    const QPoint blank(grid->viewport()->width() - 2, grid->viewport()->height() - 2);
    QVERIFY(!grid->indexAt(blank).isValid());
    QVERIFY(viewPage(blank));
    QTRY_VERIFY(sheet->isVisible());
    QTRY_VERIFY(sheet->findChild<QPushButton*>("rowJsonCopy")->isEnabled());
    page = QJsonDocument::fromJson(text->toPlainText().toUtf8());
    QVERIFY(page.isArray());
    QCOMPARE(page.array().size(), 1);
    QCOMPARE(page.array().first().toObject().value("id").toInt(), 1001);
    QVERIFY(!sheet->findChild<QLabel*>("rowJsonPageNote"));
    QVERIFY(!sheet->findChild<QLabel*>("rowJsonStatus")->isVisible());
    sheet->reject();
}

void NavigatorSqlWorkspaceTest::sqlJsonActionsColorAndCopyInBothThemes() {
    QTemporaryDir storage;
    choscordb::MainWindow window(nullptr, storage.filePath("settings.sqlite"));
    window.show();
    QTRY_VERIFY(window.findChild<choscordb::WorkspaceRecoveryController*>()->isReady());
    QTRY_VERIFY(window.findChild<choscordb::AppearanceController*>()->isReady());
    window.findChild<QAction*>("newQuery")->trigger();
    auto* theme = window.findChild<choscordb::design::ThemeManager*>();
    auto* grid = window.findChild<QTableView*>("queryResults");
    auto* model = qobject_cast<choscordb::ResultTableModel*>(grid->model());
    QVERIFY(theme && model);
    choscordb::ResultColumn payload{};
    payload.name = "payload";
    payload.databaseType = "jsonb";
    QVERIFY(model->setPage({payload},
                           {{QString("{\"key\":\"value\",\"count\":42,\"active\":true}")}}, 0));
    const auto point = grid->visualRect(model->index(0, 0)).center();
    QColor lightKey;
    for (const auto mode :
         {choscordb::design::ThemeMode::Light, choscordb::design::ThemeMode::Dark}) {
        theme->setMode(mode);
        QCoreApplication::processEvents();
        for (const char* actionName : {"viewCellJson", "viewRowJson", "viewTableJson"}) {
            bool enabled = false;
            QTimer::singleShot(0, grid, [&] {
                auto* menu = qobject_cast<QMenu*>(choscordb::design::detail::activeEmbeddedPopup());
                if (!menu) {
                    if (auto* popup = QApplication::activePopupWidget())
                        popup->close();
                    return;
                }
                auto* action = menu->findChild<QAction*>(actionName);
                if (action)
                    QTRY_VERIFY(action->isEnabled());
                enabled = action && action->isEnabled();
                menu->close();
                if (enabled)
                    action->trigger();
            });
            grid->customContextMenuRequested(point);
            QVERIFY(enabled);
            auto* sheet = window.findChild<QDialog*>("rowJsonSheet");
            QVERIFY(sheet);
            QTRY_VERIFY(sheet->isVisible());
            auto* text = sheet->findChild<choscordb::design::JsonTextView*>("rowJsonText");
            auto* copy = sheet->findChild<QPushButton*>("rowJsonCopy");
            QVERIFY(text && copy);
            QTRY_VERIFY(copy->isEnabled());
            const auto plain = text->toPlainText();
            const auto colorAt = [&](qsizetype position) {
                const auto block = text->document()->findBlock(position);
                if (!block.isValid() || !block.layout())
                    return QColor{};
                const auto local = position - block.position();
                for (const auto& range : block.layout()->formats())
                    if (local >= range.start && local < range.start + range.length)
                        return range.format.foreground().color();
                return QColor{};
            };
            const auto key = colorAt(plain.indexOf("\"key\"") + 1);
            const auto stringValue = colorAt(plain.indexOf("\"value\"") + 1);
            const auto number = colorAt(plain.indexOf("42"));
            const auto literal = colorAt(plain.indexOf("true"));
            QVERIFY(key.isValid() && stringValue.isValid() && number.isValid() &&
                    literal.isValid());
            QVERIFY(key != stringValue && key != number && key != literal &&
                    stringValue != number && stringValue != literal);
            if (mode == choscordb::design::ThemeMode::Light)
                lightKey = key;
            else
                QVERIFY(key != lightKey);
            copy->click();
            QCOMPARE(QApplication::clipboard()->text(), plain);
            sheet->reject();
        }
    }
}

void NavigatorSqlWorkspaceTest::sqlJsonActionsLeaveDatabaseAndSelectionIntact() {
    QTemporaryDir storage;
    choscordb::MainWindow window(nullptr, storage.filePath("settings.sqlite"));
    window.show();
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    workspace->connectSqlite(":memory:");
    QTRY_VERIFY(window.findChild<QAction*>("newQuery")->isEnabled());
    window.findChild<QAction*>("newQuery")->trigger();
    auto* editor = qobject_cast<choscordb::SqlEditor*>(
        window.findChild<QTabWidget*>("editorTabs")->currentWidget());
    auto* run = window.findChild<QAction*>("runStatement");
    auto* messages = window.findChild<QPlainTextEdit*>("queryMessages");
    auto* grid = window.findChild<QTableView*>("queryResults");
    auto* model = qobject_cast<choscordb::ResultTableModel*>(grid->model());
    QVERIFY(editor && run && messages && model);
    QTRY_VERIFY(run->isEnabled());
    editor->setText("CREATE TABLE records (payload TEXT);");
    run->trigger();
    QTRY_VERIFY(messages->toPlainText().contains("Completed"));
    messages->clear();
    QTRY_VERIFY(run->isEnabled());
    editor->setText("INSERT INTO records VALUES ('{\"n\":7}');");
    run->trigger();
    QTRY_VERIFY(messages->toPlainText().contains("Completed"));
    QTRY_VERIFY(run->isEnabled());
    editor->setText("SELECT payload FROM records;");
    run->trigger();
    QTRY_COMPARE(model->rowCount(), 1);
    QCOMPARE(model->index(0, 0).data().toString(), QString("{\"n\":7}"));
    grid->setCurrentIndex(model->index(0, 0));
    const auto selected = grid->currentIndex();
    const auto point = grid->visualRect(model->index(0, 0)).center();
    const char* actions[] = {"viewCellJson", "viewRowJson", "viewTableJson"};
    for (int i = 0; i < 3; ++i) {
        bool enabled = false;
        QTimer::singleShot(0, grid, [&] {
            auto* menu = qobject_cast<QMenu*>(choscordb::design::detail::activeEmbeddedPopup());
            if (!menu) {
                if (auto* popup = QApplication::activePopupWidget())
                    popup->close();
                return;
            }
            auto* action = menu->findChild<QAction*>(actions[i]);
            if (action)
                QTRY_VERIFY(action->isEnabled());
            enabled = action && action->isEnabled();
            menu->close();
            if (enabled)
                action->trigger();
        });
        grid->customContextMenuRequested(point);
        QVERIFY(enabled);
        auto* sheet = window.findChild<QDialog*>("rowJsonSheet");
        QVERIFY(sheet);
        QTRY_VERIFY(sheet->isVisible());
        auto* text = sheet->findChild<QPlainTextEdit*>("rowJsonText");
        auto* copy = sheet->findChild<QPushButton*>("rowJsonCopy");
        QVERIFY(text && copy);
        QTRY_VERIFY(copy->isEnabled());
        const auto shown = text->toPlainText();
        copy->click();
        QCOMPARE(QApplication::clipboard()->text(), shown);
        QCOMPARE(grid->currentIndex(), selected);
        if (i == 0) {
            sheet->findChild<QPushButton*>("rightSheetClose")->click();
        } else if (i == 1) {
            QTest::keyClick(sheet, Qt::Key_Escape);
        } else {
            QWidget* backdrop = nullptr;
            for (auto* candidate : window.findChildren<QWidget*>("modalBackdrop"))
                if (candidate->isVisible())
                    backdrop = candidate;
            QVERIFY(backdrop);
            QTest::mouseClick(backdrop, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
        }
        QTRY_VERIFY(!sheet->isVisible());
        QCOMPARE(grid->currentIndex(), selected);
    }
    QTRY_VERIFY(run->isEnabled());
    editor->setText("SELECT payload FROM records;");
    run->trigger();
    QTRY_COMPARE(model->rowCount(), 1);
    QCOMPARE(model->index(0, 0).data().toString(), QString("{\"n\":7}"));
}
