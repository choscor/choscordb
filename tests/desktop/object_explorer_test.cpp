#include "app/object_explorer.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/theme_manager.h"
#include "widgets/object_erd_widget.h"
#include <QAction>
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
class ObjectExplorerTest final : public QObject {
    Q_OBJECT
  private slots:
    void erdPaneAppearsOnlyForTables() {
        EngineAdapter adapter;
        ObjectExplorer explorer(&adapter);
        auto* tabs = explorer.findChild<QTabBar*>("objectTabs");
        explorer.restoreObject(std::nullopt, R"(["main","t"])", "main.t", "table");
        QVERIFY(tabs->isTabVisible(4));
        QVERIFY(tabs->isTabVisible(5));
        explorer.restoreObject(std::nullopt, R"(["main","v"])", "main.v", "view");
        QVERIFY(!tabs->isTabVisible(4));
        QVERIFY(tabs->isTabVisible(5));
        explorer.restoreObject(std::nullopt, R"(["main","ix"])", "main.ix", "index");
        QVERIFY(!tabs->isTabVisible(4));
        QVERIFY(!tabs->isTabVisible(5));
    }
    void erdDrawsTypedColumnsAndDirectedRelationshipsWithKeyboardNavigation() {
        ObjectErdWidget erd;
        design::ThemeManager theme;
        theme.setMode(design::ThemeMode::Light);
        theme.applyTo(erd);
        erd.resize(700, 420);
        erd.show();
        ObjectGraph graph;
        graph.tables = {{R"(["main","orders"])",
                         "main.orders",
                         {{"id", "INTEGER", true, false},
                          {"customer_id", "INTEGER", false, true},
                          {"customer_name", "TEXT", false, true},
                          {"parent_id", "INTEGER", false, true},
                          {"note", "TEXT", false, false}}},
                        {R"(["main","customers"])",
                         "main.customers",
                         {{"id", "INTEGER", true, false}, {"name", "TEXT", false, false}}},
                        {R"(["main","ghost"])", "main.ghost", {}}};
        graph.edges = {
            {"fk_customer",
             R"(["main","orders"])",
             R"(["main","customers"])",
             {"customer_id", "customer_name"},
             {"id", "name"}},
            {"fk_parent", R"(["main","orders"])", R"(["main","orders"])", {"parent_id"}, {"id"}},
            {"fk_missing", R"(["main","orders"])", R"(["main","ghost"])", {"customer_id"}, {""}}};
        erd.setGraph(graph, R"(["main","orders"])");
        QCoreApplication::processEvents();
        auto* view = erd.findChild<QGraphicsView*>("objectErdView");
        QVERIFY(view);
        QVERIFY(view->scene());
        QCOMPARE(view->dragMode(), QGraphicsView::ScrollHandDrag);
        auto boxFor = [&](const QString& id) -> QGraphicsRectItem* {
            for (auto* item : view->scene()->items())
                if (auto* box = qgraphicsitem_cast<QGraphicsRectItem*>(item);
                    box && box->data(0).toString() == id)
                    return box;
            return nullptr;
        };
        auto* ordersBox = boxFor(R"(["main","orders"])");
        auto* customersBox = boxFor(R"(["main","customers"])");
        auto* ghostBox = boxFor(R"(["main","ghost"])");
        QVERIFY(ordersBox);
        QVERIFY(customersBox);
        QVERIFY(ghostBox);
        auto childText = [](QGraphicsRectItem* box) {
            QList<QGraphicsSimpleTextItem*> children;
            for (auto* item : box->childItems())
                if (auto* text = qgraphicsitem_cast<QGraphicsSimpleTextItem*>(item))
                    children.append(text);
            return children;
        };
        auto childWithText = [&](QGraphicsRectItem* box,
                                 const QString& value) -> QGraphicsSimpleTextItem* {
            for (auto* text : childText(box))
                if (text->text() == value)
                    return text;
            return nullptr;
        };
        QCOMPARE(childText(ordersBox).size(), 1 + 5 * 3);
        QCOMPARE(childText(customersBox).size(), 1 + 2 * 3);
        QCOMPARE(childText(ghostBox).size(), 1);
        for (const auto& value : {"main.orders", "customer_id", "customer_name", "parent_id",
                                  "note", "INTEGER", "TEXT", "PK", "FK"})
            QVERIFY2(childWithText(ordersBox, value), value);
        for (const auto& value : {"main.customers", "id", "name", "INTEGER", "TEXT", "PK"})
            QVERIFY2(childWithText(customersBox, value), value);
        QVERIFY(!childWithText(customersBox, "customer_id"));
        auto lineFor = [&](const QString& constraint, const QString& source,
                           const QString& target) -> QGraphicsPathItem* {
            for (auto* item : view->scene()->items())
                if (auto* path = qgraphicsitem_cast<QGraphicsPathItem*>(item);
                    path && path->toolTip().startsWith(constraint + ":") &&
                    path->toolTip().contains("." + source + " → ") &&
                    path->toolTip().endsWith("." + target))
                    return path;
            return nullptr;
        };
        auto* firstPair = lineFor("fk_customer", "customer_id", "id");
        auto* secondPair = lineFor("fk_customer", "customer_name", "name");
        auto* selfPair = lineFor("fk_parent", "parent_id", "id");
        auto* missingPair = lineFor("fk_missing", "customer_id", "target column unavailable");
        QVERIFY(firstPair);
        QVERIFY(secondPair);
        QVERIFY(selfPair);
        QVERIFY(missingPair);
        auto assertEndpoint = [&](QGraphicsPathItem* line, QGraphicsRectItem* sourceBox,
                                  const QString& sourceColumn, QGraphicsRectItem* targetBox,
                                  const QString& targetColumn) {
            const auto path = line->path();
            const auto first = path.elementAt(0);
            const auto last = path.elementAt(path.elementCount() - 1);
            QVERIFY(qAbs(first.x - sourceBox->rect().right()) < 0.01);
            QVERIFY(qAbs(last.x - targetBox->rect().left()) < 0.01);
            QVERIFY(qAbs(first.y -
                         childWithText(sourceBox, sourceColumn)->sceneBoundingRect().center().y()) <
                    8);
            QVERIFY(qAbs(last.y -
                         childWithText(targetBox, targetColumn)->sceneBoundingRect().center().y()) <
                    8);
            QGraphicsPathItem* arrow = nullptr;
            for (auto* item : view->scene()->items())
                if (auto* candidate = qgraphicsitem_cast<QGraphicsPathItem*>(item);
                    candidate && candidate->toolTip().isEmpty()) {
                    const auto tip = candidate->path().elementAt(0);
                    if (qAbs(tip.x - last.x) < 0.01 && qAbs(tip.y - last.y) < 0.01) {
                        arrow = candidate;
                        break;
                    }
                }
            QVERIFY(arrow);
            QVERIFY(arrow->path().elementAt(1).x < last.x);
        };
        assertEndpoint(firstPair, ordersBox, "customer_id", customersBox, "id");
        assertEndpoint(secondPair, ordersBox, "customer_name", customersBox, "name");
        QVERIFY(missingPair->toolTip().contains("target column unavailable"));
        const auto missingEnd =
            missingPair->path().elementAt(missingPair->path().elementCount() - 1);
        QVERIFY(missingEnd.y < ghostBox->rect().center().y());
        const auto fitZoom = erd.zoomFactor();
        QTest::keyClick(view, Qt::Key_Plus);
        QVERIFY(erd.zoomFactor() > fitZoom);
        const auto beforeWheel = erd.zoomFactor();
        QWheelEvent wheel(view->viewport()->rect().center(),
                          view->viewport()->mapToGlobal(view->viewport()->rect().center()), {},
                          {0, 120}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(view->viewport(), &wheel);
        QVERIFY(erd.zoomFactor() > beforeWheel);
        const auto beforeHorizontal = erd.zoomFactor();
        QWheelEvent horizontal(view->viewport()->rect().center(),
                               view->viewport()->mapToGlobal(view->viewport()->rect().center()), {},
                               {120, 0}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(view->viewport(), &horizontal);
        QCOMPARE(erd.zoomFactor(), beforeHorizontal);
        QWheelEvent pixelVertical(view->viewport()->rect().center(),
                                  view->viewport()->mapToGlobal(view->viewport()->rect().center()),
                                  {0, -40}, {}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase,
                                  false);
        QCoreApplication::sendEvent(view->viewport(), &pixelVertical);
        QVERIFY(erd.zoomFactor() < beforeHorizontal);
        for (int i = 0; i < 7; ++i)
            QTest::keyClick(view, Qt::Key_Plus);
        QVERIFY(view->horizontalScrollBar()->maximum() > 0);
        QVERIFY(view->verticalScrollBar()->maximum() > 0);
        const auto center = view->viewport()->rect().center();
        const auto sceneCenterBeforePan = view->mapToScene(center);
        const QPoint start(20, 20);
        QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, start);
        const auto end = start + QPoint(80, 60);
        QMouseEvent firstMove(QEvent::MouseMove, start + QPoint(20, 15),
                              view->viewport()->mapToGlobal(start + QPoint(20, 15)), Qt::NoButton,
                              Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(view->viewport(), &firstMove);
        QMouseEvent drag(QEvent::MouseMove, end, view->viewport()->mapToGlobal(end), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(view->viewport(), &drag);
        QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, end);
        const auto sceneCenterAfterPan = view->mapToScene(center);
        QVERIFY2(QLineF(sceneCenterBeforePan, sceneCenterAfterPan).length() > 1,
                 qPrintable(QString("zoom=%1 scroll=(%2/%3,%4/%5) center=%6,%7 -> %8,%9")
                                .arg(erd.zoomFactor())
                                .arg(view->horizontalScrollBar()->value())
                                .arg(view->horizontalScrollBar()->maximum())
                                .arg(view->verticalScrollBar()->value())
                                .arg(view->verticalScrollBar()->maximum())
                                .arg(sceneCenterBeforePan.x())
                                .arg(sceneCenterBeforePan.y())
                                .arg(sceneCenterAfterPan.x())
                                .arg(sceneCenterAfterPan.y())));
        QCOMPARE(erd.graph().tables.size(), 3);
        QCOMPARE(erd.graph().edges.size(), 3);
        QTest::keyClick(view, Qt::Key_0);
        QVERIFY(qAbs(erd.zoomFactor() - fitZoom) < 0.01);
        QSignalSpy activated(&erd, &ObjectErdWidget::tableActivated);
        view->setFocus();
        QTest::keyClick(view, Qt::Key_Right);
        QTest::keyClick(view, Qt::Key_Return);
        QCOMPARE(activated.count(), 1);
        QCOMPARE(activated.first().first().toString(), QString(R"(["main","customers"])"));
        QGraphicsRectItem* customerBox = nullptr;
        for (auto* item : view->scene()->items())
            if (auto* box = qgraphicsitem_cast<QGraphicsRectItem*>(item);
                box && box->data(0).toString() == R"(["main","customers"])") {
                customerBox = box;
                break;
            }
        QVERIFY(customerBox);
        QTest::mouseDClick(view->viewport(), Qt::LeftButton, Qt::NoModifier,
                           view->mapFromScene(customerBox->rect().center()));
        QCOMPARE(activated.count(), 2);
        QCOMPARE(activated.last().first().toString(), QString(R"(["main","customers"])"));
        const auto lightImage = erd.grab().toImage();
        theme.setMode(design::ThemeMode::Dark);
        theme.applyTo(erd);
        erd.setGraph(graph, R"(["main","orders"])");
        QCoreApplication::processEvents();
        const auto darkImage = erd.grab().toImage();
        QVERIFY(lightImage != darkImage);
        erd.resize(350, 420);
        QCoreApplication::processEvents();
        auto* narrowLine = lineFor("fk_customer", "customer_id", "id");
        QVERIFY(narrowLine);
        const auto narrowPath = narrowLine->path();
        const auto narrowStart = narrowPath.elementAt(0);
        const auto narrowEnd = narrowPath.elementAt(narrowPath.elementCount() - 1);
        QVERIFY(narrowEnd.x < narrowStart.x);
        QGraphicsPathItem* narrowArrow = nullptr;
        for (auto* item : view->scene()->items())
            if (auto* candidate = qgraphicsitem_cast<QGraphicsPathItem*>(item);
                candidate && candidate->toolTip().isEmpty()) {
                const auto tip = candidate->path().elementAt(0);
                if (qAbs(tip.x - narrowEnd.x) < 0.01 && qAbs(tip.y - narrowEnd.y) < 0.01) {
                    narrowArrow = candidate;
                    break;
                }
            }
        QVERIFY(narrowArrow);
        QVERIFY(narrowArrow->path().elementAt(1).x > narrowEnd.x);
        const auto narrowImage = erd.grab().toImage();
        QCOMPARE(narrowImage.width(), 350);
        const auto captureDir = qEnvironmentVariable("CHOSCORDB_ERD_CAPTURE_DIR");
        if (!captureDir.isEmpty()) {
            QVERIFY(lightImage.save(captureDir + "/erd-light.png"));
            QVERIFY(darkImage.save(captureDir + "/erd-dark.png"));
            QVERIFY(narrowImage.save(captureDir + "/erd-dark-narrow.png"));
        }
    }
    void erdLoadsLazilyRefreshesAndRejectsStaleReplies() {
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
        const auto create =
            adapter.execute(*connection, "CREATE TABLE parent(id INTEGER PRIMARY KEY)");
        QVERIFY(create);
        adapter.fetchPage(*create);
        QTRY_COMPARE(finished, 1);
        ObjectExplorer explorer(&adapter);
        explorer.resize(600, 400);
        explorer.show();
        QSignalSpy graphs(&adapter, &EngineAdapter::objectGraphReady);
        explorer.openObject(*connection, R"(["main","parent"])", "main.parent", "table");
        auto* tabs = explorer.findChild<QTabBar*>("objectTabs");
        auto* status = explorer.findChild<QLabel*>("objectStatus");
        auto* erd = explorer.findChild<ObjectErdWidget*>("objectErd");
        QVERIFY(tabs->isTabVisible(4));
        QCOMPARE(graphs.count(), 0);
        explorer.selectPane(4);
        QTRY_COMPARE(graphs.count(), 1);
        QCOMPARE(erd->findChild<QGraphicsView*>("objectErdView")->geometry().top(), 0);
        QCOMPARE(status->property("state").toString(), QString("empty"));
        QVERIFY(status->text().contains("No foreign-key relationships"));
        QCOMPARE(erd->graph().tables.size(), 1);
        const auto oldToken = graphs.first().at(2).toULongLong();
        explorer.selectPane(3);
        explorer.selectPane(4);
        QCOMPARE(graphs.count(), 1);
        const auto createChild = adapter.execute(
            *connection,
            "CREATE TABLE child(id INTEGER PRIMARY KEY, parent_id INTEGER REFERENCES parent(id))");
        QVERIFY(createChild);
        adapter.fetchPage(*createChild);
        QTRY_COMPARE(finished, 2);
        QTest::mouseClick(explorer.findChild<QPushButton*>("objectRefresh"), Qt::LeftButton);
        QTRY_COMPARE(graphs.count(), 2);
        QCOMPARE(erd->graph().tables.size(), 2);
        QCOMPARE(erd->graph().edges.size(), 1);
        QCOMPARE(status->property("state").toString(), QString("loaded"));
        ObjectGraph stale;
        stale.tables = {{R"(["main","parent"])", "stale", {}}};
        adapter.objectGraphReady(*connection, R"(["main","parent"])", oldToken, stale);
        QCOMPARE(erd->graph().tables.size(), 2);
        QVERIFY(adapter.disconnectConnection(*connection));
        QTRY_COMPARE(status->property("state").toString(), QString("disconnected"));
        QCOMPARE(erd->graph().tables.size(), 0);
    }
    void erdFailureCanRetryAfterTableAppears() {
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
        ObjectExplorer explorer(&adapter);
        explorer.show();
        explorer.openObject(*connection, R"(["main","later"])", "main.later", "table");
        explorer.selectPane(4);
        auto* status = explorer.findChild<QLabel*>("objectStatus");
        auto* retry = explorer.findChild<QAction*>("objectRetry");
        QTRY_COMPARE(status->property("state").toString(), QString("failed"));
        QVERIFY(retry->isEnabled());
        const auto create = adapter.execute(*connection, "CREATE TABLE later(id INTEGER)");
        QVERIFY(create);
        adapter.fetchPage(*create);
        QTRY_COMPARE(finished, 1);
        retry->trigger();
        QTRY_COMPARE(status->property("state").toString(), QString("empty"));
        QCOMPARE(explorer.findChild<ObjectErdWidget*>()->graph().tables.size(), 1);
    }
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
        QCOMPARE(ddl->document()->documentMargin(), 8.0);
        QCOMPARE(gutter->font().family(), ddl->font().family());
        QCOMPARE(gutter->geometry().height(), ddl->contentsRect().height());
        QVERIFY(ddl->viewport()->geometry().left() >= gutter->width());
        const auto initialWidth = gutter->width();
        QVERIFY(initialWidth >= ddl->fontMetrics().horizontalAdvance("000"));
        const auto background = design::resolvedThemeForWidget(*ddl).colors.surfaceRaised;
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
        const auto darkBackground = design::resolvedThemeForWidget(*ddl).colors.surfaceRaised;
        QVERIFY(darkBackground != background);
        const auto darkImage = gutter->grab().toImage();
        QCOMPARE(darkImage.pixelColor(1, darkImage.height() / 2), darkBackground);
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
        QCOMPARE(colorAt(7), QColor("#c2410c"));
        QCOMPARE(colorAt(12), QColor("#2f6aa3"));
        QCOMPARE(colorAt(16), QColor("#68737a"));
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
        QCOMPARE(colorAt(14), QColor("#68737a"));
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
        auto* footer = explorer.findChild<QWidget*>("objectFooter");
        design::ThemeManager theme;
        for (const auto mode : {design::ThemeMode::Light, design::ThemeMode::Dark}) {
            theme.setMode(mode);
            theme.applyTo(explorer);
            QCOMPARE(footer->palette().color(QPalette::Window),
                     theme.resolvedTheme().colors.surfaceRaised);
        }
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
        auto* footer = explorer.findChild<QWidget*>("objectFooter");
        QCOMPARE(footer->palette().color(QPalette::Window),
                 design::resolvedThemeForWidget(*footer).colors.surfaceRaised);
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
        QCOMPARE(QString::fromLatin1(footer->metaObject()->className()),
                 QString("choscordb::design::StatusLine"));
        for (const char* name : {"objectRefresh", "objectOpenQuery", "objectGenerateSql"}) {
            auto* action = explorer.findChild<QPushButton*>(name);
            QVERIFY(action);
            QCOMPARE(action->parentWidget(), header);
            QVERIFY(action->text().isEmpty());
            QVERIFY(!action->icon().isNull());
            QVERIFY(!action->accessibleName().isEmpty());
        }
        QCOMPARE(explorer.findChild<QLabel*>("objectStatus")->parentWidget(), footer);
        QVERIFY(!explorer.findChild<QLabel*>("objectStatus")->wordWrap());
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
        QVERIFY(!tabs->isTabVisible(5));
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
        QCOMPARE(tabs->count(), 6);
        QCOMPARE(tabs->tabText(4), QString("ERD"));
        QCOMPARE(tabs->tabText(5), QString("Data"));
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
        QTest::mouseClick(tabs, Qt::LeftButton, Qt::NoModifier, tabs->tabRect(5).center());
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
        auto* footer = explorer.findChild<QWidget*>("objectFooter");
        const auto colors = design::resolvedThemeForWidget(explorer).colors;
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
        QCOMPARE(footer->palette().color(QPalette::Window), colors.successSurface);
        QCOMPARE(table->model()->rowCount(), 0);
        QVERIFY(status->text().contains("No indexes"));
        explorer.openObject(*connection, R"(["main"])", "main");
        tabs->setCurrentIndex(3);
        QTRY_COMPARE(status->property("state").toString(), QString("unsupported"));
        QVERIFY(status->text().size() > QString("Unsupported:").size());
        QCOMPARE(footer->palette().color(QPalette::Window), colors.surfaceRaised);
        QSignalSpy failed(&adapter, &EngineAdapter::objectInspectionFailed);
        explorer.openObject(*connection, R"(["main","later"])", "later");
        tabs->setCurrentIndex(3);
        QTRY_COMPARE(status->property("state").toString(), QString("failed"));
        QVERIFY(!failed.isEmpty());
        QCOMPARE(footer->palette().color(QPalette::Window), colors.dangerSurface);
        QVERIFY(footer->toolTip().contains(failed.last().at(3).toString()));
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
        QCOMPARE(footer->palette().color(QPalette::Window), colors.successSurface);
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
