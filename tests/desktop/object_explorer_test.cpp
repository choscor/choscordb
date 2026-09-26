#include "app/object_explorer.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/theme_manager.h"
#include <QAction>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QMenu>
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
using namespace choscordb;
class ObjectExplorerTest final : public QObject {
    Q_OBJECT
  private slots:
    void ddlHasEditorAlignedLineNumberGutter() {
        EngineAdapter adapter;
        ObjectExplorer explorer(&adapter);
        design::ThemeManager theme;
        theme.setMode(design::ThemeMode::Light);
        theme.applyTo(explorer);
        auto* ddl = explorer.findChild<QPlainTextEdit*>("objectDdl");
        QVERIFY(ddl);
        auto* gutter = ddl->findChild<QWidget*>("objectDdlLineNumbers");
        QVERIFY(gutter);
        explorer.findChild<QStackedWidget*>()->setCurrentWidget(ddl);
        explorer.resize(700, 450);
        explorer.show();
        ddl->setPlainText("CREATE TABLE sample (\n  id BIGINT\n);");
        QCoreApplication::processEvents();
        QVERIFY(gutter->isVisible());
        QCOMPARE(gutter->geometry().left(), 0);
        QCOMPARE(gutter->geometry().top(), 0);
        QCOMPARE(ddl->viewport()->geometry().top(), 0);
        QCOMPARE(ddl->document()->documentMargin(), 0.0);
        QCOMPARE(gutter->font().family(), ddl->font().family());
        QCOMPARE(gutter->geometry().height(), ddl->contentsRect().height());
        QVERIFY(ddl->viewport()->geometry().left() >= gutter->width());
        const auto initialWidth = gutter->width();
        QVERIFY(initialWidth >= ddl->fontMetrics().horizontalAdvance("000"));
        const auto background = design::resolvedThemeForWidget(*ddl).colors.elevatedSurface;
        const auto lightImage = gutter->grab().toImage();
        QCOMPARE(lightImage.pixelColor(1, lightImage.height() / 2), background);
        bool numberPainted = false;
        for (int y = 0; y < lightImage.height() && !numberPainted; ++y)
            for (int x = 0; x < lightImage.width(); ++x)
                if (lightImage.pixelColor(x, y) != background) {
                    numberPainted = true;
                    break;
                }
        QVERIFY(numberPainted);
        ddl->setPlainText(QString("x\n").repeated(999) + "x");
        QCoreApplication::processEvents();
        QVERIFY(gutter->width() > initialWidth);
        const auto beforeScroll = gutter->grab().toImage();
        auto* scroll = ddl->verticalScrollBar();
        QVERIFY(scroll->maximum() > 0);
        scroll->setValue(scroll->maximum());
        QCoreApplication::processEvents();
        QVERIFY(gutter->grab().toImage() != beforeScroll);
        theme.setMode(design::ThemeMode::Dark);
        theme.applyTo(explorer);
        const auto darkBackground = design::resolvedThemeForWidget(*ddl).colors.elevatedSurface;
        QVERIFY(darkBackground != background);
        const auto darkImage = gutter->grab().toImage();
        QCOMPARE(darkImage.pixelColor(1, darkImage.height() / 2), darkBackground);
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
                 design::resolveTypography(design::TypographyRole::Monospace).family());
        QCOMPARE(ddl->font().pixelSize(), 13);
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
        QCOMPARE(colorAt(0), QColor("#885da7"));
        QCOMPARE(colorAt(7), QColor("#287f66"));
        QCOMPARE(colorAt(12), QColor("#936b3f"));
        QCOMPARE(colorAt(16), QColor("#6f7879"));
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
        QCOMPARE(colorAt(14), QColor("#6f7879"));
        theme.setMode(design::ThemeMode::Dark);
        theme.applyTo(explorer);
        QCOMPARE(colorAt(0), QColor("#a984c8"));
        QCOMPARE(colorAt(14), QColor("#9ca6a7"));
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
    void restoredObjectStaysInertUntilActivatedAndRetainsPane() {
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
        const auto create = adapter.execute(*connection, "CREATE TABLE restored(id INTEGER)");
        QVERIFY(create);
        adapter.fetchPage(*create);
        QTRY_COMPARE(finished, 1);
        ObjectExplorer explorer(&adapter);
        explorer.show();
        QSignalSpy inspections(&adapter, &EngineAdapter::objectInspectionReady);
        QSignalSpy failures(&adapter, &EngineAdapter::objectInspectionFailed);
        explorer.restoreObject(*connection, R"(["main","restored"])", "restored");
        explorer.selectPane(3);
        QCOMPARE(explorer.paneIndex(), 3);
        QTest::qWait(50);
        QCOMPARE(inspections.count(), 0);
        explorer.activateRestoredObject();
        QVERIFY2(failures.isEmpty(),
                 qPrintable(failures.isEmpty() ? QString{} : failures.first().at(3).toString()));
        QTRY_COMPARE(inspections.count(), 1);
        QTRY_VERIFY(explorer.findChild<QPlainTextEdit*>("objectDdl")
                        ->toPlainText()
                        .contains("CREATE TABLE restored"));
        explorer.activateRestoredObject();
        QTest::qWait(50);
        QCOMPARE(inspections.count(), 1);
    }
    void missingRestoredConnectionOffersManualReconnect() {
        EngineAdapter adapter;
        ObjectExplorer explorer(&adapter);
        explorer.show();
        QSignalSpy inspections(&adapter, &EngineAdapter::objectInspectionReady);
        QSignalSpy reconnect(&explorer, &ObjectExplorer::reconnectRequested);
        explorer.restoreObject(std::nullopt, R"(["main","lost"])", "lost");
        explorer.selectPane(3);
        explorer.activateRestoredObject();
        QCOMPARE(explorer.paneIndex(), 3);
        QCOMPARE(inspections.count(), 0);
        auto* status = explorer.findChild<QLabel*>("objectStatus");
        QCOMPARE(status->property("state").toString(), QString("disconnected"));
        QVERIFY(!explorer.findChild<QPushButton*>("objectReconnect"));
        auto* action = explorer.findChild<QAction*>("objectReconnect");
        QVERIFY(action);
        QVERIFY(explorer.actions().contains(action));
        QVERIFY(action->isEnabled());
        action->trigger();
        QCOMPARE(reconnect.count(), 1);
    }
    void actionsBelongToContextualHeaderAndStatusToFooter() {
        EngineAdapter adapter;
        ObjectExplorer explorer(&adapter);
        auto* header = explorer.findChild<QWidget*>("objectHeader");
        auto* footer = explorer.findChild<QWidget*>("objectFooter");
        QVERIFY(header);
        QVERIFY(footer);
        for (const char* name : {"objectRefresh", "objectOpenQuery", "objectGenerateSql"}) {
            auto* action = explorer.findChild<QPushButton*>(name);
            QVERIFY(action);
            QCOMPARE(action->parentWidget(), header);
            QVERIFY(action->text().isEmpty());
            QVERIFY(!action->icon().isNull());
            QVERIFY(!action->accessibleName().isEmpty());
        }
        QCOMPARE(explorer.findChild<QLabel*>("objectStatus")->parentWidget(), footer);
        explorer.show();
        QCoreApplication::processEvents();
        auto* tabs = explorer.findChild<QTabBar*>("objectTabs");
        auto* refresh = explorer.findChild<QPushButton*>("objectRefresh");
        QVERIFY(tabs && refresh);
        QCOMPARE(header->geometry().top(), 0);
        QVERIFY(refresh->mapTo(&explorer, QPoint(0, 0)).x() > header->geometry().left());
        QVERIFY(refresh->mapTo(&explorer, QPoint(0, 0)).y() > header->geometry().top());
        QCOMPARE(header->geometry().bottom() + 1, tabs->geometry().top());
        for (const char* name : {"objectRetry", "objectReconnect"}) {
            QVERIFY(!explorer.findChild<QPushButton*>(name));
            auto* action = explorer.findChild<QAction*>(name);
            QVERIFY(action);
            QVERIFY(explorer.actions().contains(action));
            QVERIFY(!action->isEnabled());
        }
        QVERIFY(refresh->geometry().left() < header->width() / 2);
    }
    void reopeningSameObjectKeepsSelectedPaneWithoutReadingAgain() {
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
        const auto create = adapter.execute(*connection, "CREATE TABLE account(id INTEGER)");
        QVERIFY(create);
        adapter.fetchPage(*create);
        QTRY_COMPARE(finished, 1);
        ObjectExplorer explorer(&adapter);
        explorer.show();
        QSignalSpy inspections(&adapter, &EngineAdapter::objectInspectionReady);
        const QString identity = R"(["main","account"])";
        explorer.openObject(*connection, identity, "account");
        auto* tabs = explorer.findChild<QTabBar*>("objectTabs");
        QVERIFY(tabs);
        tabs->setCurrentIndex(3);
        QTRY_VERIFY(inspections.count() >= 2);
        const int reads = inspections.count();
        explorer.openObject(*connection, identity, "account");
        QCOMPARE(tabs->currentIndex(), 3);
        QTest::qWait(50);
        QCOMPARE(inspections.count(), reads);
        explorer.openObject(*connection, identity, "account", {},
                            {QVariantMap{{"name", "Owner"}, {"value", "team"}}});
        QCOMPARE(tabs->currentIndex(), 3);
    }
    void schemaIndexShowsDetailsAndDoesNotGenerateTableSql() {
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
        const auto create = adapter.execute(*connection, "CREATE TABLE account(id INTEGER)");
        QVERIFY(create);
        adapter.fetchPage(*create);
        QTRY_COMPARE(finished, 1);
        const auto index =
            adapter.execute(*connection, "CREATE INDEX account_id_idx ON account(id)");
        QVERIFY(index);
        adapter.fetchPage(*index);
        QTRY_COMPARE(finished, 2);
        ObjectExplorer explorer(&adapter);
        explorer.show();
        QSignalSpy generated(&explorer, &ObjectExplorer::sqlGenerated);
        explorer.openObject(*connection, R"(["main","account","index","account_id_idx"])",
                            "\"main\".\"account_id_idx\"", "index");
        auto* tabs = explorer.findChild<QTabBar*>("objectTabs");
        auto* table = explorer.findChild<QTableView*>("objectMetadata");
        QVERIFY(tabs);
        QTRY_COMPARE(table->model()->rowCount(), 4);
        QCOMPARE(tabs->tabText(0), QString("Details"));
        QCOMPARE(table->model()->index(0, 1).data().toString(), QString("account_id_idx"));
        QCOMPARE(table->model()->index(1, 1).data().toString(), QString("Index"));
        QCOMPARE(table->model()->index(2, 1).data().toString(), QString("main"));
        QVERIFY(!explorer.findChild<QPushButton*>("objectOpenQuery")->isEnabled());
        QVERIFY(!explorer.findChild<QPushButton*>("objectGenerateSql")->isEnabled());
        QCOMPARE(generated.count(), 0);
        tabs->setCurrentIndex(3);
        auto* ddl = explorer.findChild<QPlainTextEdit*>("objectDdl");
        QTRY_VERIFY(ddl->toPlainText().contains("CREATE INDEX account_id_idx ON account(id)"));
    }
    void functionDetailsShowAvailableAndUnsupportedProperties() {
        EngineAdapter adapter;
        ObjectExplorer explorer(&adapter);
        explorer.show();
        explorer.openObject(
            19, "pg:function:42", "\"public\".\"compute\"(integer)", "function",
            {QVariantMap{{"name", "Language"}, {"value", "sql"}, {"availability", "available"}},
             QVariantMap{{"name", "Source"},
                         {"availability", "unsupported"},
                         {"reason", "Definition is hidden"}}});
        auto* table = explorer.findChild<QTableView*>("objectMetadata");
        auto* tabs = explorer.findChild<QTabBar*>("objectTabs");
        QCOMPARE(table->model()->rowCount(), 5);
        QCOMPARE(table->model()->index(2, 1).data().toString(), QString("public"));
        QCOMPARE(table->model()->index(3, 1).data().toString(), QString("sql"));
        QCOMPARE(table->model()->index(4, 1).data().toString(),
                 QString("Unsupported: Definition is hidden"));
        QVERIFY(!tabs->isTabVisible(4));
    }
    void panesLoadRealIndexesKeysDdlAndDataOnlyWhenSelected() {
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
        const auto create = adapter.execute(
            *connection, "CREATE TABLE account(id INTEGER PRIMARY KEY, nickname TEXT UNIQUE)");
        QVERIFY(create);
        adapter.fetchPage(*create);
        QTRY_COMPARE(finished, 1);
        ObjectExplorer explorer(&adapter);
        explorer.show();
        QSignalSpy inspection(&adapter, &EngineAdapter::objectInspectionReady);
        QSignalSpy data(&explorer, &ObjectExplorer::dataRequested);
        explorer.openObject(*connection, R"(["main","account"])", "\"main\".\"account\"");
        auto* table = explorer.findChild<QTableView*>("objectMetadata");
        auto* tabs = explorer.findChild<QTabBar*>("objectTabs");
        QTRY_COMPARE(inspection.count(), 1);
        QCOMPARE(data.count(), 0);
        QTest::mouseClick(tabs, Qt::LeftButton, Qt::NoModifier, tabs->tabRect(1).center());
        QTRY_COMPARE(inspection.count(), 2);
        QCOMPARE(table->model()->rowCount(), 1);
        QCOMPARE(table->model()->index(0, 0).data().toString(),
                 QString("sqlite_autoindex_account_1"));
        QTest::mouseClick(tabs, Qt::LeftButton, Qt::NoModifier, tabs->tabRect(2).center());
        QTRY_COMPARE(inspection.count(), 3);
        // The fixture declares both a primary key and a UNIQUE constraint.
        QCOMPARE(table->model()->rowCount(), 2);
        QCOMPARE(table->model()->index(0, 0).data().toString(), QString("id"));
        QCOMPARE(table->model()->index(0, 1).data().toString(), QString("Primary key"));
        QCOMPARE(table->model()->index(1, 1).data().toString(), QString("Unique key"));
        QTest::mouseClick(tabs, Qt::LeftButton, Qt::NoModifier, tabs->tabRect(3).center());
        QTRY_COMPARE(inspection.count(), 4);
        auto* ddl = explorer.findChild<QPlainTextEdit*>("objectDdl");
        QVERIFY(ddl);
        QVERIFY(ddl->toPlainText().contains("nickname TEXT UNIQUE"));
        QVERIFY(ddl->isReadOnly());
        QCOMPARE(data.count(), 0);
        QTest::mouseClick(tabs, Qt::LeftButton, Qt::NoModifier, tabs->tabRect(4).center());
        QCOMPARE(data.count(), 1);
        QCOMPARE(data.first().at(0).toULongLong(), *connection);
        QCOMPARE(data.first().at(1).toString(), QString(R"(["main","account"])"));
        QCOMPARE(inspection.count(), 4);
    }
    void disconnectClearsLoadedMetadataAndInvalidatesData() {
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
        const auto create = adapter.execute(*connection, "CREATE TABLE retained(value INTEGER)");
        QVERIFY(create);
        adapter.fetchPage(*create);
        QTRY_COMPARE(finished, 1);
        ObjectExplorer explorer(&adapter);
        explorer.show();
        explorer.openObject(*connection, R"(["main","retained"])", "retained");
        auto* table = explorer.findChild<QTableView*>("objectMetadata");
        auto* status = explorer.findChild<QLabel*>("objectStatus");
        QTRY_COMPARE(table->model()->rowCount(), 1);
        QSignalSpy invalidated(&explorer, &ObjectExplorer::objectChanged);
        QVERIFY(adapter.disconnectConnection(*connection));
        QTRY_COMPARE(status->property("state").toString(), QString("disconnected"));
        QCOMPARE(table->model()->rowCount(), 0);
        QCOMPARE(invalidated.count(), 1);
    }
    void refreshEmptyUnsupportedAndRetryKeepDistinctStates() {
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
        auto create = adapter.execute(*connection, "CREATE TABLE changing(value TEXT)");
        QVERIFY(create);
        adapter.fetchPage(*create);
        QTRY_COMPARE(finished, 1);
        ObjectExplorer explorer(&adapter);
        explorer.resize(640, 480);
        explorer.show();
        explorer.openObject(*connection, R"(["main","changing"])", "changing");
        auto* table = explorer.findChild<QTableView*>("objectMetadata");
        auto* status = explorer.findChild<QLabel*>("objectStatus");
        auto* tabs = explorer.findChild<QTabBar*>("objectTabs");
        QTRY_COMPARE(table->model()->rowCount(), 1);
        auto alter = adapter.execute(*connection, "ALTER TABLE changing ADD COLUMN extra INTEGER");
        QVERIFY(alter);
        adapter.fetchPage(*alter);
        QTRY_COMPARE(finished, 2);
        auto* refresh = explorer.findChild<QPushButton*>("objectRefresh");
        QVERIFY(refresh);
        QTest::mouseClick(refresh, Qt::LeftButton);
        QTRY_COMPARE(table->model()->rowCount(), 2);
        QCOMPARE(table->model()->index(1, 0).data().toString(), QString("extra"));
        tabs->setCurrentIndex(1);
        QTRY_COMPARE(status->property("state").toString(), QString("empty"));
        QCOMPARE(table->model()->rowCount(), 0);
        QVERIFY(status->text().contains("No indexes"));
        explorer.openObject(*connection, R"(["main"])", "main");
        tabs->setCurrentIndex(3);
        QTRY_COMPARE(status->property("state").toString(), QString("unsupported"));
        QVERIFY(status->text().size() > QString("Unsupported:").size());
        QSignalSpy failed(&adapter, &EngineAdapter::objectInspectionFailed);
        explorer.openObject(*connection, R"(["main","later"])", "later");
        tabs->setCurrentIndex(3);
        QTRY_COMPARE(status->property("state").toString(), QString("failed"));
        QVERIFY(!failed.isEmpty());
        const auto failedToken = failed.last().at(2).toULongLong();
        auto later = adapter.execute(*connection, "CREATE TABLE later(value TEXT)");
        QVERIFY(later);
        adapter.fetchPage(*later);
        QTRY_COMPARE(finished, 3);
        auto* retry = explorer.findChild<QAction*>("objectRetry");
        QVERIFY(retry);
        QVERIFY(retry->isEnabled());
        retry->trigger();
        QTRY_COMPARE(status->property("state").toString(), QString("loaded"));
        auto* ddl = explorer.findChild<QPlainTextEdit*>("objectDdl");
        QVERIFY(ddl->toPlainText().contains("CREATE TABLE later"));
        adapter.objectInspectionFailed(*connection, R"(["main","later"])", failedToken,
                                       "Late obsolete failure");
        QCOMPARE(status->property("state").toString(), QString("loaded"));
        QVERIFY(ddl->toPlainText().contains("CREATE TABLE later"));
    }
    void openAndGenerateSqlUseTheObjectAndNeverExecute() {
        EngineAdapter adapter;
        bool connected = false;
        int finished = 0, queued = 0;
        connect(&adapter, &EngineAdapter::eventReady, this, [&](const BridgeEvent& event) {
            if (event.kind == "connected")
                connected = true;
            if (event.kind == "query_finished")
                ++finished;
            if (event.kind == "query_state" && event.state == "queued")
                ++queued;
        });
        const auto connection = adapter.connectSqlite(":memory:");
        QVERIFY(connection);
        QTRY_VERIFY(connected);
        auto create =
            adapter.execute(*connection, "CREATE TABLE \"a.b\"(\"col name\" TEXT, other INTEGER)");
        QVERIFY(create);
        adapter.fetchPage(*create);
        QTRY_COMPARE(finished, 1);
        ObjectExplorer explorer(&adapter);
        explorer.resize(640, 480);
        explorer.show();
        explorer.openObject(*connection, R"(["main","a.b"])", "\"main\".\"a.b\"");
        QTRY_COMPARE(explorer.findChild<QTableView*>("objectMetadata")->model()->rowCount(), 2);
        QSignalSpy generated(&explorer, &ObjectExplorer::sqlGenerated);
        auto* open = explorer.findChild<QPushButton*>("objectOpenQuery");
        QVERIFY(open);
        QTest::mouseClick(open, Qt::LeftButton);
        QCOMPARE(generated.count(), 1);
        QCOMPARE(generated.first().first().toULongLong(), *connection);
        QCOMPARE(generated.first().at(1).toString(), QString("SELECT * FROM \"main\".\"a.b\";"));
        auto* insert = explorer.findChild<QAction*>("objectGenerate_insert");
        QVERIFY(insert);
        QVERIFY(insert->isEnabled());
        insert->trigger();
        QCOMPARE(generated.count(), 2);
        QVERIFY(generated.last().at(1).toString().contains(
            "INSERT INTO \"main\".\"a.b\" (\"col name\", \"other\") VALUES ($1, $2);"));
        auto positive = adapter.execute(*connection, "SELECT 1");
        QVERIFY(positive);
        adapter.fetchPage(*positive);
        QTRY_COMPARE(finished, 2);
        QCOMPARE(queued, 2);
    }
    void columnsDisplayRealPropertiesForUnicodeObject() {
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
        const auto create = adapter.execute(
            *connection,
            "CREATE TABLE \"dữ liệu\"(label TEXT NOT NULL DEFAULT 'actual', amount INTEGER)");
        QVERIFY(create);
        adapter.fetchPage(*create);
        QTRY_COMPARE(finished, 1);
        ObjectExplorer explorer(&adapter);
        explorer.show();
        explorer.openObject(*connection, R"(["main","dữ liệu"])", "main.dữ liệu");
        auto* table = explorer.findChild<QTableView*>("objectMetadata");
        QVERIFY(table);
        QTRY_COMPARE(table->model()->rowCount(), 2);
        QCOMPARE(table->model()->index(0, 0).data().toString(), QString("label"));
        QCOMPARE(table->model()->index(1, 0).data().toString(), QString("amount"));
        QStringList values;
        for (int column = 0; column < table->model()->columnCount(); ++column)
            values.append(table->model()->index(0, column).data().toString());
        QVERIFY(values.contains("'actual'"));
        QVERIFY(values.contains("No"));
        QVERIFY(values.contains("TEXT"));
        QCOMPARE(table->editTriggers(), QAbstractItemView::NoEditTriggers);
        QCOMPARE(table->rowHeight(0), 33);
        QVERIFY(!table->verticalHeader()->isVisible());
        QVERIFY(
            !qvariant_cast<QIcon>(table->model()->index(0, 0).data(Qt::DecorationRole)).isNull());
        QVERIFY(table->alternatingRowColors());
        QVERIFY(!table->showGrid());
        QVERIFY(!table->wordWrap());
        QVERIFY(table->columnWidth(4) > 100);
    }
};
QTEST_MAIN(ObjectExplorerTest)
#include "object_explorer_test.moc"
