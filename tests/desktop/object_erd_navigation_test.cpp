#include "app/object_explorer.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/theme_manager.h"
#include "widgets/object_erd_widget.h"
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QInputDevice>
#include <QLabel>
#include <QMap>
#include <QNativeGestureEvent>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QPointingDevice>
#include <QPushButton>
#include <QTest>
#include <QWheelEvent>
#include <iterator>
using namespace choscordb;
class ObjectErdNavigationTest final : public QObject {
    Q_OBJECT
  private slots:
    void erdReportsMissingRelatedTableAsIncomplete() {
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
            "CREATE TABLE child(id INTEGER, parent_id INTEGER REFERENCES missing(id))");
        QVERIFY(create);
        adapter.fetchPage(*create);
        QTRY_COMPARE(finished, 1);
        ObjectExplorer explorer(&adapter);
        explorer.show();
        explorer.openObject(*connection, R"(["main","child"])", "main.child", "table");
        explorer.selectPane(4);
        auto* status = explorer.findChild<QLabel*>("objectStatus");
        auto* erd = explorer.findChild<ObjectErdWidget*>("objectErd");
        QTRY_COMPARE(status->property("state").toString(), QString("incomplete"));
        QVERIFY(status->toolTip().contains("missing"));
        auto* footer = explorer.findChild<QWidget*>("objectFooter");
        QCOMPARE(footer->palette().color(QPalette::Window),
                 design::resolvedThemeForWidget(explorer).colors.surfaceRaised);
        QCOMPARE(erd->graph().edges.size(), 1);
        QCOMPARE(erd->graph().tables.size(), 2);
    }
    void sparseTallGraphKeepsReadableScaleWhenStacked() {
        ObjectErdWidget erd;
        erd.resize(900, 420);
        erd.show();
        ObjectGraph graph;
        ObjectGraphTable center{"center", "main.center", {}};
        ObjectGraphTable neighbor{"neighbor", "main.neighbor", {}};
        for (int i = 0; i < 20; ++i) {
            center.columns.append({QString("center_%1").arg(i), "INTEGER", false, false});
            neighbor.columns.append({QString("neighbor_%1").arg(i), "INTEGER", false, false});
        }
        graph.tables = {center, neighbor};
        graph.edges.append({"fk", "center", "neighbor", {"center_0"}, {"neighbor_0"}});
        erd.setGraph(graph, "center");
        QCoreApplication::processEvents();
        QVERIFY(erd.zoomFactor() >= 0.85);
        erd.resize(360, 420);
        QCoreApplication::processEvents();
        QVERIFY(erd.zoomFactor() >= 0.85);
    }
    void tallSmallGraphStaysReadableWhenResizedNarrow() {
        ObjectErdWidget erd;
        erd.resize(900, 420);
        erd.show();
        ObjectGraph graph;
        graph.tables.append({"center", "main.center", {{"id", "INTEGER", true, false}}});
        for (int i = 0; i < 3; ++i) {
            ObjectGraphTable neighbor{QString("neighbor_%1").arg(i), "main.neighbor", {}};
            for (int column = 0; column < 30; ++column)
                neighbor.columns.append(
                    {QString("column_%1").arg(column), "INTEGER", false, false});
            graph.tables.append(neighbor);
            graph.edges.append(
                {QString("fk_%1").arg(i), "center", neighbor.id, {"id"}, {"column_0"}});
        }
        erd.setGraph(graph, "center");
        QCoreApplication::processEvents();
        QVERIFY(erd.zoomFactor() >= 0.85);
        QVERIFY(erd.findChild<QGraphicsView*>("objectErdView")->scene()->sceneRect().height() <
                1400);
        erd.resize(360, 420);
        QCoreApplication::processEvents();
        QVERIFY(erd.zoomFactor() >= 0.85);
    }
    void mouseWheelZoomsAtCursorWithoutModifier() {
        ObjectErdWidget erd;
        erd.resize(440, 300);
        erd.show();
        ObjectGraph graph;
        graph.tables.append({"center", "main.center", {{"id", "INTEGER", true, false}}});
        for (int i = 0; i < 12; ++i) {
            const QString id = QString("neighbor_%1").arg(i);
            ObjectGraphTable table{id, QString("main.%1").arg(id), {}};
            for (int column = 0; column < 10; ++column)
                table.columns.append({QString("column_%1").arg(column), "INTEGER", false, false});
            graph.tables.append(table);
            graph.edges.append({QString("fk_%1").arg(i), "center", id, {"id"}, {"column_0"}});
        }
        erd.setGraph(graph, "center");
        QCoreApplication::processEvents();
        auto* view = erd.findChild<QGraphicsView*>("objectErdView");
        QVERIFY(view);
        view->centerOn(view->scene()->sceneRect().center());
        const QPoint anchor(130, 90);
        const auto sceneAtAnchor = view->mapToScene(anchor);
        const auto initialScale = erd.zoomFactor();
        QPointingDevice mouse("test wheel mouse", 1, QInputDevice::DeviceType::Mouse,
                              QPointingDevice::PointerType::Generic,
                              QInputDevice::Capability::Scroll, 1, 3);
        QWheelEvent zoomIn(anchor, view->viewport()->mapToGlobal(anchor), {0, 40}, {0, 120},
                           Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false,
                           Qt::MouseEventNotSynthesized, &mouse);
        QCoreApplication::sendEvent(view->viewport(), &zoomIn);
        QVERIFY(erd.zoomFactor() > initialScale);
        QVERIFY(QLineF(sceneAtAnchor, view->mapToScene(anchor)).length() < 3);

        QWheelEvent zoomOut(anchor, view->viewport()->mapToGlobal(anchor), {0, -40}, {0, -120},
                            Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false,
                            Qt::MouseEventNotSynthesized, &mouse);
        QCoreApplication::sendEvent(view->viewport(), &zoomOut);
        QVERIFY(qAbs(erd.zoomFactor() - initialScale) < 0.001);
        QVERIFY(QLineF(sceneAtAnchor, view->mapToScene(anchor)).length() < 3);
    }
    void erdHasNoControlRowAndTrackpadGesturesPanOrZoom() {
        ObjectErdWidget erd;
        erd.resize(440, 300);
        erd.show();
        ObjectGraph graph;
        graph.tables.append({"center", "main.center", {{"id", "INTEGER", true, false}}});
        for (int i = 0; i < 12; ++i) {
            const QString id = QString("neighbor_%1").arg(i);
            ObjectGraphTable table{id, QString("main.%1").arg(id), {}};
            for (int column = 0; column < 10; ++column)
                table.columns.append({QString("column_%1").arg(column), "INTEGER", false, false});
            graph.tables.append(table);
            graph.edges.append({QString("fk_%1").arg(i), "center", id, {"id"}, {"column_0"}});
        }
        erd.setGraph(graph, "center");
        QCoreApplication::processEvents();
        auto* view = erd.findChild<QGraphicsView*>("objectErdView");
        QVERIFY(view);
        QCOMPARE(view->geometry().top(), 0);
        QVERIFY(!erd.findChild<QPushButton*>("objectErdZoomIn"));
        QVERIFY(!erd.findChild<QPushButton*>("objectErdZoomOut"));
        QVERIFY(!erd.findChild<QPushButton*>("objectErdFit"));
        const QPoint anchor(130, 90);
        const auto atAnchor = view->mapToScene(anchor);
        const auto before = erd.zoomFactor();
        QNativeGestureEvent pinch(Qt::ZoomNativeGesture, QPointingDevice::primaryPointingDevice(),
                                  2, anchor, view->viewport()->mapToGlobal(anchor),
                                  view->viewport()->mapToGlobal(anchor), 0.25, {}, 1);
        QCoreApplication::sendEvent(view->viewport(), &pinch);
        QVERIFY(erd.zoomFactor() > before);
        QVERIFY(QLineF(atAnchor, view->mapToScene(anchor)).length() < 3);
        QNativeGestureEvent pinchOut(Qt::ZoomNativeGesture,
                                     QPointingDevice::primaryPointingDevice(), 2, anchor,
                                     view->viewport()->mapToGlobal(anchor),
                                     view->viewport()->mapToGlobal(anchor), -0.2, {}, 1);
        QCoreApplication::sendEvent(view->viewport(), &pinchOut);
        QVERIFY(erd.zoomFactor() < before * 1.25);
        for (int i = 0; i < 40; ++i)
            QCoreApplication::sendEvent(view->viewport(), &pinch);
        QVERIFY(erd.zoomFactor() <= 4.0);
        view->centerOn(view->scene()->sceneRect().center());
        const QPoint center = view->viewport()->rect().center();
        const auto beforeWheelZoom = view->mapToScene(center);
        QPointingDevice touchpad(
            "test touchpad", 2, QInputDevice::DeviceType::TouchPad,
            QPointingDevice::PointerType::Finger,
            QInputDevice::Capability::PixelScroll | QInputDevice::Capability::Scroll, 1, 0);
        QWheelEvent vertical(center, view->viewport()->mapToGlobal(center), {0, -80}, {0, -120},
                             Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false,
                             Qt::MouseEventNotSynthesized, &touchpad);
        const auto beforeScrollScale = erd.zoomFactor();
        QCoreApplication::sendEvent(view->viewport(), &vertical);
        QVERIFY(erd.zoomFactor() < beforeScrollScale);
        QVERIFY(QLineF(beforeWheelZoom, view->mapToScene(center)).length() < 3);
        const auto afterScrollScale = erd.zoomFactor();
        const auto beforeHorizontal = view->mapToScene(center);
        QWheelEvent horizontal(center, view->viewport()->mapToGlobal(center), {-80, 0}, {},
                               Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false,
                               Qt::MouseEventNotSynthesized, &touchpad);
        QCoreApplication::sendEvent(view->viewport(), &horizontal);
        QCOMPARE(erd.zoomFactor(), afterScrollScale);
        QVERIFY(qAbs(view->mapToScene(center).x() - beforeHorizontal.x()) > 1);
        erd.resize(120, 200);
        QCoreApplication::processEvents();
        QTest::keyClick(view, Qt::Key_0);
        QVERIFY(erd.zoomFactor() < 0.1);
        const auto overviewScale = erd.zoomFactor();
        QCoreApplication::sendEvent(view->viewport(), &pinchOut);
        QCOMPARE(erd.zoomFactor(), overviewScale);
        QNativeGestureEvent smallPinchIn(Qt::ZoomNativeGesture,
                                         QPointingDevice::primaryPointingDevice(), 2, anchor,
                                         view->viewport()->mapToGlobal(anchor),
                                         view->viewport()->mapToGlobal(anchor), 0.2, {}, 2);
        QCoreApplication::sendEvent(view->viewport(), &smallPinchIn);
        QVERIFY(erd.zoomFactor() > overviewScale);
        QCoreApplication::sendEvent(view->viewport(), &pinchOut);
        QVERIFY(qAbs(erd.zoomFactor() - overviewScale) < 0.001);
    }
    void erdDenseGraphKeepsBoxesReadableAndStable() {
        ObjectErdWidget erd;
        design::ThemeManager theme;
        theme.setMode(design::ThemeMode::Light);
        theme.applyTo(erd);
        erd.resize(760, 520);
        erd.show();
        ObjectGraph graph;
        const QString centerId = QStringLiteral("center");
        graph.tables.append({centerId,
                             "main.center",
                             {{"id", "INTEGER", true, false},
                              {"owner_id", "INTEGER", false, true},
                              {"parent_id", "INTEGER", false, true}}});
        graph.edges.append({"fk_parent", centerId, centerId, {"parent_id"}, {"id"}});
        graph.edges.append({"fk_self_parallel_a", centerId, centerId, {"parent_id"}, {"id"}});
        graph.edges.append({"fk_self_parallel_b", centerId, centerId, {"parent_id"}, {"id"}});
        for (int i = 0; i < 24; ++i) {
            const QString id = QString("neighbor_%1").arg(i, 2, 10, QChar('0'));
            ObjectGraphTable table{id, QString("main.%1").arg(id), {}};
            for (int column = 0; column < 8; ++column)
                table.columns.append(
                    {QString("column_%1").arg(column), "INTEGER", column == 0, column == 1});
            graph.tables.append(table);
            if (i % 2 == 0)
                graph.edges.append(
                    {QString("fk_out_%1").arg(i), centerId, id, {"owner_id"}, {"column_0"}});
            else
                graph.edges.append(
                    {QString("fk_in_%1").arg(i), id, centerId, {"column_1"}, {"id"}});
        }
        graph.edges.append({"fk_parallel", centerId, "neighbor_00", {"parent_id"}, {"column_1"}});
        graph.edges.append({"fk_composite",
                            centerId,
                            "neighbor_02",
                            {"id", "owner_id"},
                            {"column_0", "column_1"}});
        auto* view = erd.findChild<QGraphicsView*>("objectErdView");
        QVERIFY(view);
        auto boxes = [&] {
            QMap<QString, QRectF> result;
            for (auto* item : view->scene()->items())
                if (auto* box = qgraphicsitem_cast<QGraphicsRectItem*>(item);
                    box && box->data(0).isValid())
                    result.insert(box->data(0).toString(), box->sceneBoundingRect());
            return result;
        };
        erd.setGraph(graph, centerId);
        QCoreApplication::processEvents();
        const auto lightImage = erd.grab().toImage();
        const auto first = boxes();
        QCOMPARE(first.size(), graph.tables.size());
        for (const auto& table : graph.tables) {
            QGraphicsRectItem* box = nullptr;
            for (auto* item : view->scene()->items())
                if (auto* candidate = qgraphicsitem_cast<QGraphicsRectItem*>(item);
                    candidate && candidate->data(0).toString() == table.id) {
                    box = candidate;
                    break;
                }
            QVERIFY(box);
            QCOMPARE(box->childItems().size(), 1 + table.columns.size() * 3);
            QStringList text;
            for (auto* child : box->childItems())
                if (auto* label = qgraphicsitem_cast<QGraphicsSimpleTextItem*>(child))
                    text.append(label->text());
            QVERIFY(text.contains(table.qualifiedName));
            for (const auto& column : table.columns) {
                QVERIFY(text.contains(column.name));
                QVERIFY(text.contains(column.databaseType));
            }
            QVERIFY(text.contains("PK"));
            QVERIFY(text.contains("FK"));
        }
        for (auto it = first.cbegin(); it != first.cend(); ++it)
            for (auto other = std::next(it); other != first.cend(); ++other)
                QVERIFY2(!it.value().intersects(other.value()),
                         qPrintable(it.key() + " overlaps " + other.key()));
        const auto columnY = [&](const QString& id, const QString& name) {
            for (auto* item : view->scene()->items())
                if (auto* box = qgraphicsitem_cast<QGraphicsRectItem*>(item);
                    box && box->data(0).toString() == id)
                    for (auto* child : box->childItems())
                        if (auto* label = qgraphicsitem_cast<QGraphicsSimpleTextItem*>(child);
                            label && label->text() == name)
                            return label->sceneBoundingRect().center().y();
            return qreal(-10000);
        };
        QList<QPainterPath> selfPaths;
        for (const auto& edge : graph.edges) {
            QList<QGraphicsPathItem*> connectors;
            for (auto* item : view->scene()->items())
                if (auto* line = qgraphicsitem_cast<QGraphicsPathItem*>(item);
                    line && line->toolTip().startsWith(edge.id + ":"))
                    connectors.append(line);
            QCOMPARE(connectors.size(), edge.sourceColumns.size());
            if (edge.sourceId == edge.targetId)
                selfPaths.append(connectors.first()->path());
            for (int pair = 0; pair < edge.sourceColumns.size(); ++pair) {
                QGraphicsPathItem* connector = nullptr;
                for (auto* line : connectors)
                    if (line->toolTip().contains("." + edge.sourceColumns.at(pair) + " → ") &&
                        line->toolTip().endsWith("." + edge.targetColumns.at(pair)))
                        connector = line;
                QVERIFY(connector);
                const auto start = connector->path().elementAt(0);
                const auto end = connector->path().elementAt(connector->path().elementCount() - 1);
                const auto source = first.value(edge.sourceId);
                const auto target = first.value(edge.targetId);
                QVERIFY(qAbs(start.x - source.left()) < 3 || qAbs(start.x - source.right()) < 3);
                QVERIFY(qAbs(end.x - target.left()) < 3 || qAbs(end.x - target.right()) < 3);
                QVERIFY(qAbs(start.y - columnY(edge.sourceId, edge.sourceColumns.at(pair))) < 8);
                QVERIFY(qAbs(end.y - columnY(edge.targetId, edge.targetColumns.at(pair))) < 8);
            }
            if (edge.sourceId == edge.targetId)
                continue;
            for (auto it = first.cbegin(); it != first.cend(); ++it) {
                if (it.key() == edge.sourceId || it.key() == edge.targetId)
                    continue;
                for (auto* connector : connectors) {
                    QPainterPath boxPath;
                    boxPath.addRect(it.value());
                    QPainterPathStroker stroker;
                    stroker.setWidth(1);
                    QVERIFY2(!stroker.createStroke(connector->path()).intersects(boxPath),
                             qPrintable(edge.id + " crosses " + it.key()));
                }
            }
        }
        QCOMPARE(selfPaths.size(), 3);
        for (int i = 0; i < selfPaths.size(); ++i)
            for (int j = i + 1; j < selfPaths.size(); ++j)
                QVERIFY(selfPaths.at(i) != selfPaths.at(j));
        // Twenty-four detailed neighbors need multiple lanes, not a single tall stack.
        QVERIFY(view->scene()->sceneRect().height() < 1800);
        QVERIFY(erd.zoomFactor() >= 0.85);
        const auto focus = view->mapFromScene(first.value(centerId).center());
        QVERIFY(view->viewport()->rect().contains(focus));
        QVERIFY(qAbs(focus.x() - view->viewport()->rect().center().x()) < 100);
        QVERIFY(qAbs(focus.y() - view->viewport()->rect().center().y()) < 100);
        erd.setGraph(graph, centerId);
        QCoreApplication::processEvents();
        QCOMPARE(boxes(), first);
        theme.setMode(design::ThemeMode::Dark);
        theme.applyTo(erd);
        erd.setGraph(graph, centerId);
        QCoreApplication::processEvents();
        const auto darkImage = erd.grab().toImage();
        QVERIFY(lightImage != darkImage);
        erd.resize(440, 520);
        QCoreApplication::processEvents();
        QCOMPARE(boxes(), first);
        const auto narrowFocus = view->mapFromScene(first.value(centerId).center());
        QVERIFY(qAbs(narrowFocus.x() - view->viewport()->rect().center().x()) < 100);
        const auto darkNarrowImage = erd.grab().toImage();
        theme.setMode(design::ThemeMode::Light);
        theme.applyTo(erd);
        erd.setGraph(graph, centerId);
        QCoreApplication::processEvents();
        const auto lightNarrowImage = erd.grab().toImage();
        QVERIFY(lightNarrowImage != darkNarrowImage);
        const auto captureDir = qEnvironmentVariable("CHOSCORDB_ERD_CAPTURE_DIR");
        if (!captureDir.isEmpty()) {
            QVERIFY(lightImage.save(captureDir + "/erd-dense-light.png"));
            QVERIFY(darkImage.save(captureDir + "/erd-dense-dark.png"));
            QVERIFY(lightNarrowImage.save(captureDir + "/erd-dense-light-narrow.png"));
            QVERIFY(darkNarrowImage.save(captureDir + "/erd-dense-dark-narrow.png"));
        }
        const QPoint viewportCenter = view->viewport()->rect().center();
        QWheelEvent pan(viewportCenter, view->viewport()->mapToGlobal(viewportCenter), {-120, 0},
                        {}, Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
        QCoreApplication::sendEvent(view->viewport(), &pan);
        const auto pannedFocus = view->mapFromScene(first.value(centerId).center());
        QVERIFY(qAbs(pannedFocus.x() - viewportCenter.x()) > 50);
        erd.resize(500, 520);
        QCoreApplication::processEvents();
        const auto resizedFocus = view->mapFromScene(first.value(centerId).center());
        QVERIFY(qAbs(resizedFocus.x() - view->viewport()->rect().center().x()) > 40);
        view->setFocus();
        QTest::keyClick(view, Qt::Key_0);
        QVERIFY(erd.zoomFactor() < 0.85);
        QVERIFY(view->viewport()->rect().contains(
            view->mapFromScene(view->scene()->sceneRect().topLeft())));
        const auto fittedBottomRight = view->mapFromScene(view->scene()->sceneRect().bottomRight());
        QVERIFY2(view->viewport()->rect().adjusted(0, 0, 2, 2).contains(fittedBottomRight),
                 qPrintable(QString("bottom right %1,%2; viewport %3,%4")
                                .arg(fittedBottomRight.x())
                                .arg(fittedBottomRight.y())
                                .arg(view->viewport()->width())
                                .arg(view->viewport()->height())));
    }
};
QTEST_MAIN(ObjectErdNavigationTest)
#include "object_erd_navigation_test.moc"
