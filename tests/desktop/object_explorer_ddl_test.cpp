#include "app/object_explorer.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/theme_manager.h"
#include "design_system/toast_region/toast_region.h"
#include "screen_state_checks.h"
#include "widgets/object_erd_widget.h"
#include <QAction>
#include <QFrame>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTabBar>
#include <QTableView>
#include <QTest>
#include <QTextBlock>
#include <QTextLayout>
#include <QWheelEvent>
using namespace choscordb;
class ObjectExplorerDdlTest final : public QObject {
    Q_OBJECT
  private slots:
    void triggersPaneListsTriggersAndRequestsTheirSql() {
        EngineAdapter adapter;
        bool connected = false;
        int finished = 0;
        connect(&adapter, &EngineAdapter::eventReady, this, [&](const BridgeEvent& event) {
            if (event.kind == "connected")
                connected = true;
            if (event.kind == "query_finished")
                ++finished;
        });
        const auto connection = adapter.connectSqlite(":memory:");
        QVERIFY(connection);
        QTRY_VERIFY(connected);
        const QString definition = "CREATE TRIGGER audit_insert AFTER INSERT ON orders BEGIN "
                                   "INSERT INTO audit VALUES (NEW.id); END";
        int expected = 0;
        for (const auto& sql : {QString("CREATE TABLE orders(id INTEGER PRIMARY KEY, total REAL)"),
                                QString("CREATE TABLE audit(id INTEGER)"), definition}) {
            const auto query = adapter.execute(*connection, sql);
            QVERIFY(query);
            adapter.fetchPage(*query);
            ++expected;
            QTRY_COMPARE(finished, expected);
        }
        ObjectExplorer explorer(&adapter);
        explorer.show();
        QSignalSpy inspection(&adapter, &EngineAdapter::objectInspectionReady);
        QList<TriggerScriptRequest> requests;
        connect(&explorer, &ObjectExplorer::triggerScriptRequested, this,
                [&](quint64 target, const TriggerScriptRequest& request) {
                    QCOMPARE(target, *connection);
                    requests.append(request);
                });
        explorer.openObject(*connection, R"(["main","orders"])", "main.orders", "table");
        auto* tabs = explorer.findChild<QTabBar*>("objectTabs");
        auto* table = explorer.findChild<QTableView*>("objectMetadata");
        auto* button = explorer.findChild<QPushButton*>("objectTriggerSql");
        QVERIFY(button);
        QCOMPARE(tabs->tabText(6), QString("Triggers"));
        QVERIFY(tabs->isTabVisible(6));
        QTRY_COMPARE(inspection.count(), 1);
        QVERIFY(!button->isVisible());
        QTest::mouseClick(tabs, Qt::LeftButton, Qt::NoModifier, tabs->tabRect(6).center());
        QTRY_COMPARE(inspection.count(), 2);
        QVERIFY(button->isVisible());
        auto* model = table->model();
        QCOMPARE(model->rowCount(), 1);
        QCOMPARE(model->headerData(1, Qt::Horizontal).toString(), QString("Timing"));
        QCOMPARE(model->index(0, 0).data().toString(), QString("audit_insert"));
        QCOMPARE(model->index(0, 1).data().toString(), QString("AFTER"));
        QCOMPARE(model->index(0, 2).data().toString(), QString("INSERT"));
        auto* menu = button->menu();
        QVERIFY(menu);
        auto action = [&](const QString& code) {
            emit menu->aboutToShow();
            return menu->findChild<QAction*>("objectTrigger_" + code);
        };
        table->setCurrentIndex(QModelIndex());
        QVERIFY(action("create")->isEnabled());
        QVERIFY(!action("edit")->isEnabled());
        table->setCurrentIndex(model->index(0, 0));
        action("edit")->trigger();
        QCOMPARE(requests.size(), 1);
        auto request = requests.first();
        QCOMPARE(request.kind, QString("edit"));
        QCOMPARE(request.trigger, QString("audit_insert"));
        request.driver = "sqlite";
        const auto plan = EngineAdapter::triggerScript(request);
        QCOMPARE(plan.error, QString());
        QCOMPARE(plan.sql, QString("DROP TRIGGER \"main\".\"audit_insert\";\n\nCREATE TRIGGER "
                                   "\"main\".\"audit_insert\" AFTER INSERT ON orders BEGIN "
                                   "INSERT INTO audit VALUES (NEW.id); END;"));
        explorer.restoreObject(std::nullopt, R"(["main","ix"])", "main.ix", "index");
        QVERIFY(!tabs->isTabVisible(6));
    }
    void ddlTextHasTopAndLeftBreathingRoom() {
        EngineAdapter adapter;
        ObjectExplorer explorer(&adapter);
        auto* ddl = explorer.findChild<QPlainTextEdit*>("objectDdl");
        QVERIFY(ddl);
        explorer.findChild<QStackedWidget*>()->setCurrentWidget(ddl);
        explorer.resize(700, 450);
        explorer.show();
        ddl->setPlainText("CREATE VIEW sample AS SELECT 1;");
        QCoreApplication::processEvents();
        ddl->moveCursor(QTextCursor::Start);
        const QRect firstCharacter = ddl->cursorRect();
        QVERIFY2(firstCharacter.top() >= 8, "DDL text needs at least 8 px top inset");
        QVERIFY2(firstCharacter.left() >= 8, "DDL text needs at least 8 px left inset");
    }
    void ddlMatchesSqlEditorTypographyAndSyntaxPalette() {
        EngineAdapter adapter;
        ObjectExplorer explorer(&adapter);
        design::ThemeManager theme;
        theme.setMode(design::ThemeMode::Light);
        theme.applyTo(explorer);
        auto* ddl = explorer.findChild<QPlainTextEdit*>("objectDdl");
        QVERIFY(ddl);
        QCOMPARE(ddl->font().family(),
                 design::resolveTypography(design::TextStyle::Body, design::FontDesign::Monospaced)
                     .family());
        QCOMPARE(ddl->font().pixelSize(), 14);
        QCOMPARE(ddl->frameShape(), QFrame::NoFrame);
        ddl->setPlainText("SELECT 'x', 42; -- note");
        QCoreApplication::processEvents();
        const auto formats = ddl->document()->firstBlock().layout()->formats();
        const auto colorAt = [&](int offset) {
            for (const auto& range : formats)
                if (offset >= range.start && offset < range.start + range.length)
                    return range.format.foreground().color();
            return QColor{};
        };
        QCOMPARE(colorAt(0), QColor("#d73a49"));
        QCOMPARE(colorAt(7), QColor("#032f62"));
        QCOMPARE(colorAt(12), QColor("#005cc5"));
        QCOMPARE(colorAt(16), QColor("#6a737d"));
    }
    void ddlRecolorsWithThemeAndKeepsCommentNumbersMuted() {
        EngineAdapter adapter;
        ObjectExplorer explorer(&adapter);
        design::ThemeManager theme;
        theme.setMode(design::ThemeMode::Light);
        theme.applyTo(explorer);
        auto* ddl = explorer.findChild<QPlainTextEdit*>("objectDdl");
        QVERIFY(ddl);
        ddl->setPlainText("SELECT 42; -- 99");
        const auto colorAt = [&](int offset) {
            QCoreApplication::processEvents();
            for (const auto& range : ddl->document()->firstBlock().layout()->formats())
                if (offset >= range.start && offset < range.start + range.length)
                    return range.format.foreground().color();
            return QColor{};
        };
        QCOMPARE(colorAt(14), QColor("#6a737d"));
        theme.setMode(design::ThemeMode::Dark);
        theme.applyTo(explorer);
        QCOMPARE(colorAt(0), QColor("#a0a0a0"));
        QCOMPARE(colorAt(14), QColor(0x8b, 0x8b, 0x8b, 0x94));
    }
    void ddlUsesSqlSyntaxColors() {
        EngineAdapter adapter;
        ObjectExplorer explorer(&adapter);
        auto* ddl = explorer.findChild<QPlainTextEdit*>("objectDdl");
        QVERIFY(ddl);
        ddl->setPlainText("CREATE TABLE customers (name TEXT DEFAULT 'Alice'); -- note");
        QCoreApplication::processEvents();
        const auto formats = ddl->document()->firstBlock().layout()->formats();
        const auto colorAt = [&](int offset) {
            for (const auto& range : formats)
                if (offset >= range.start && offset < range.start + range.length)
                    return range.format.foreground().color();
            return QColor{};
        };
        QVERIFY(colorAt(0).isValid());
        QVERIFY(colorAt(36).isValid());
        QVERIFY(colorAt(47).isValid());
        QVERIFY(colorAt(0) != colorAt(47));
        ddl->setPlainText("CREATE TABLE \"SELECT\" (name TEXT DEFAULT '-- active'); -- note");
        QCoreApplication::processEvents();
        const auto quotedFormats = ddl->document()->firstBlock().layout()->formats();
        const auto quotedColor = [&](int offset) {
            for (const auto& range : quotedFormats)
                if (offset >= range.start && offset < range.start + range.length)
                    return range.format.foreground().color();
            return QColor{};
        };
        QVERIFY(quotedColor(14).isValid());
        QVERIFY(quotedColor(14) != quotedColor(0));
        QVERIFY(quotedColor(43) != quotedColor(55));
        ddl->setPlainText("CREATE /* SELECT\nFROM */ TABLE items DEFAULT 'hello\nSELECT';");
        QCoreApplication::processEvents();
        const auto second = ddl->document()->findBlockByNumber(1).layout()->formats();
        const auto third = ddl->document()->findBlockByNumber(2).layout()->formats();
        const auto formatAt = [](const auto& ranges, int offset) {
            for (const auto& range : ranges)
                if (offset >= range.start && offset < range.start + range.length)
                    return range.format.foreground().color();
            return QColor{};
        };
        QVERIFY(formatAt(second, 0).isValid());
        QVERIFY(formatAt(second, 0) != formatAt(second, 8));
        QVERIFY(formatAt(third, 0).isValid());
        QVERIFY(formatAt(third, 0) != formatAt(second, 8));
        ddl->setPlainText("'SELECT' CREATE");
        QCoreApplication::processEvents();
        const auto firstQuoted = ddl->document()->firstBlock().layout()->formats();
        QVERIFY(formatAt(firstQuoted, 1).isValid());
        QCOMPARE(formatAt(firstQuoted, 1), formatAt(firstQuoted, 0));
        QVERIFY(formatAt(firstQuoted, 1) != formatAt(firstQuoted, 9));
    }
};
QTEST_MAIN(ObjectExplorerDdlTest)
#include "object_explorer_ddl_test.moc"
