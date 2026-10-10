#include "widgets/object_erd_widget.h"

#include "design_system/theme.h"
#include <QEvent>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>

namespace choscordb {
namespace {
constexpr int tableIdRole = 0;
constexpr int tableNameRole = 1;

QString marker(const ObjectGraphColumn& column) {
    if (column.primaryKey && column.foreignKey)
        return QStringLiteral("PK/FK");
    if (column.primaryKey)
        return QStringLiteral("PK");
    if (column.foreignKey)
        return QStringLiteral("FK");
    return {};
}
} // namespace

ObjectErdWidget::ObjectErdWidget(QWidget* parent)
    : QWidget(parent), view_(new QGraphicsView(this)), scene_(new QGraphicsScene(this)) {
    setObjectName("objectErd");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    view_->setObjectName("objectErdView");
    view_->setAccessibleName(tr("Entity relationship diagram"));
    view_->setAccessibleDescription(
        tr("Scroll vertically or pinch to zoom; scroll horizontally or drag to pan. "
           "Use arrow keys to select a table, Enter to open it, plus and minus to zoom, "
           "and 0 to fit the whole diagram."));
    view_->setScene(scene_);
    view_->setRenderHint(QPainter::Antialiasing);
    view_->setDragMode(QGraphicsView::ScrollHandDrag);
    view_->setTransformationAnchor(QGraphicsView::NoAnchor);
    view_->setFocusPolicy(Qt::StrongFocus);
    view_->setFrameShape(QFrame::NoFrame);
    view_->viewport()->installEventFilter(this);
    view_->installEventFilter(this);
    layout->addWidget(view_, 1);
}

void ObjectErdWidget::clearGraph() {
    panning_ = false;
    userNavigated_ = false;
    minZoom_ = 0.1;
    graph_ = {};
    selectedId_.clear();
    boxes_.clear();
    scene_->clear();
    view_->resetTransform();
}

void ObjectErdWidget::setGraph(const ObjectGraph& graph, const QString& selectedId) {
    graph_ = graph;
    selectedId_ = selectedId;
    userNavigated_ = false;
    minZoom_ = 0.1;
    render();
    QTimer::singleShot(0, this, [this] { initialView(); });
}

void ObjectErdWidget::initialView() {
    if (scene_->items().isEmpty())
        return;
    const auto viewport = view_->viewport()->size();
    const auto bounds = scene_->sceneRect().size();
    const qreal fullScale =
        qMin(qreal(viewport.width()) / bounds.width(), qreal(viewport.height()) / bounds.height());
    if (fullScale >= 0.85) {
        fitGraph();
        return;
    }
    view_->resetTransform();
    if (auto* selected = boxes_.value(selectedId_, nullptr)) {
        const auto focus = selected->rect().center();
        const QRectF visibleAroundFocus(focus.x() - viewport.width() / 2.0,
                                        focus.y() - viewport.height() / 2.0, viewport.width(),
                                        viewport.height());
        scene_->setSceneRect(scene_->sceneRect().united(visibleAroundFocus));
        view_->centerOn(focus);
    }
}

qreal ObjectErdWidget::zoomFactor() const {
    return view_->transform().m11();
}

void ObjectErdWidget::fitGraph() {
    if (scene_->items().isEmpty())
        return;
    const QRectF bounds = scene_->sceneRect();
    view_->fitInView(bounds.adjusted(-bounds.width() * 0.03, -bounds.height() * 0.03,
                                     bounds.width() * 0.03, bounds.height() * 0.03),
                     Qt::KeepAspectRatio);
    if (view_->transform().m11() > 1.0)
        view_->scale(1.0 / view_->transform().m11(), 1.0 / view_->transform().m22());
    minZoom_ = qMin(qreal(0.1), zoomFactor());
}

void ObjectErdWidget::zoom(qreal factor) {
    zoomAt(factor, view_->viewport()->rect().center());
}

void ObjectErdWidget::zoomAt(qreal factor, const QPoint& anchor) {
    const qreal current = zoomFactor();
    const qreal next = qBound(minZoom_, current * factor, qreal(4.0));
    if (qFuzzyCompare(current, next))
        return;
    userNavigated_ = true;
    const QPointF fixedScenePoint = view_->mapToScene(anchor);
    view_->scale(next / current, next / current);
    const QPoint moved = view_->mapFromScene(fixedScenePoint);
    view_->horizontalScrollBar()->setValue(view_->horizontalScrollBar()->value() + moved.x() -
                                           anchor.x());
    view_->verticalScrollBar()->setValue(view_->verticalScrollBar()->value() + moved.y() -
                                         anchor.y());
}

void ObjectErdWidget::activate(const QString& id) {
    for (const auto& table : graph_.tables)
        if (table.id == id) {
            emit tableActivated(table.id, table.qualifiedName);
            return;
        }
}

void ObjectErdWidget::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange && !graph_.tables.isEmpty())
        render();
}

bool ObjectErdWidget::eventFilter(QObject* watched, QEvent* event) {
    if ((watched == view_ || watched == view_->viewport()) &&
        event->type() == QEvent::NativeGesture) {
        const auto* gesture = static_cast<QNativeGestureEvent*>(event);
        if (gesture->gestureType() == Qt::ZoomNativeGesture) {
            const QPoint anchor =
                view_->viewport()->mapFromGlobal(gesture->globalPosition().toPoint());
            zoomAt(qExp(gesture->value()), anchor);
            return true;
        }
    }
    if (watched == view_->viewport()) {
        if (event->type() == QEvent::MouseButtonPress) {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton && !graph_.tables.isEmpty()) {
                panning_ = true;
                panStart_ = mouse->pos();
                scrollStart_ = QPoint(view_->horizontalScrollBar()->value(),
                                      view_->verticalScrollBar()->value());
                return true;
            }
        }
        if (event->type() == QEvent::MouseMove && panning_) {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->buttons() & Qt::LeftButton) {
                const auto delta = mouse->pos() - panStart_;
                if (!delta.isNull())
                    userNavigated_ = true;
                view_->horizontalScrollBar()->setValue(scrollStart_.x() - delta.x());
                view_->verticalScrollBar()->setValue(scrollStart_.y() - delta.y());
                return true;
            }
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            if (panning_ && mouse->button() == Qt::LeftButton) {
                panning_ = false;
                return true;
            }
        }
        if (event->type() == QEvent::Wheel) {
            const auto* wheel = static_cast<QWheelEvent*>(event);
            const QPoint delta =
                wheel->pixelDelta().isNull() ? wheel->angleDelta() / 3 : wheel->pixelDelta();
            if (!delta.isNull())
                userNavigated_ = true;
            const bool wheelZoom = delta.y() != 0;
            if (wheelZoom || wheel->modifiers() & (Qt::ControlModifier | Qt::MetaModifier)) {
                zoomAt(qExp(delta.y() / 400.0), wheel->position().toPoint());
                return true;
            }
            view_->horizontalScrollBar()->setValue(view_->horizontalScrollBar()->value() -
                                                   delta.x());
            view_->verticalScrollBar()->setValue(view_->verticalScrollBar()->value() - delta.y());
            return true;
        }
        if (event->type() == QEvent::MouseButtonDblClick) {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            for (auto* item = view_->itemAt(mouse->pos()); item; item = item->parentItem())
                if (item->data(tableIdRole).isValid()) {
                    activate(item->data(tableIdRole).toString());
                    return true;
                }
        }
        if (event->type() == QEvent::Resize)
            QTimer::singleShot(0, this, [this] {
                if (!graph_.tables.isEmpty() && !denseLayout_ &&
                    (view_->viewport()->width() < horizontalWidth_) != stacked_) {
                    render();
                    initialView();
                } else if (!userNavigated_ && zoomFactor() == 1.0) {
                    initialView();
                }
            });
    }
    if ((watched == view_ || watched == view_->viewport()) && event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Plus || key->key() == Qt::Key_Equal) {
            zoom(1.2);
            return true;
        }
        if (key->key() == Qt::Key_Minus) {
            zoom(1.0 / 1.2);
            return true;
        }
        if (key->key() == Qt::Key_0) {
            userNavigated_ = true;
            fitGraph();
            return true;
        }
        if ((key->key() == Qt::Key_Right || key->key() == Qt::Key_Down ||
             key->key() == Qt::Key_Left || key->key() == Qt::Key_Up) &&
            !graph_.tables.isEmpty()) {
            QStringList ids;
            for (const auto& table : graph_.tables)
                ids.append(table.id);
            const int current = ids.indexOf(selectedId_);
            const int step = key->key() == Qt::Key_Right || key->key() == Qt::Key_Down ? 1 : -1;
            selectedId_ = ids.at((current + step + ids.size()) % ids.size());
            for (auto it = boxes_.begin(); it != boxes_.end(); ++it)
                it.value()->setSelected(it.key() == selectedId_);
            userNavigated_ = true;
            view_->ensureVisible(boxes_.value(selectedId_));
            return true;
        }
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            activate(selectedId_);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ObjectErdWidget::render() {
    scene_->clear();
    boxes_.clear();
    if (graph_.tables.isEmpty())
        return;
    const auto colors = design::resolvedThemeForWidget(*this).colors;
    view_->setBackgroundBrush(colors.bg);
    const QFont headerFont = design::resolveTypography(design::TypographyRole::Title);
    const QFont rowFont = design::resolveTypography(design::TypographyRole::Metadata);
    const QFontMetrics headerMeasure(headerFont), rowMeasure(rowFont);
    const qreal pad = design::spacing(design::Spacing::Two);
    const qreal rowHeight = qMax(design::dimension(design::Dimension::Row),
                                 rowMeasure.height() + design::spacing(design::Spacing::One));
    const qreal headerHeight = qMax(design::dimension(design::Dimension::Control),
                                    headerMeasure.height() + design::spacing(design::Spacing::Two));
    const qreal gap = design::spacing(design::Spacing::Four) * 6;
    const qreal minWidth = design::dimension(design::Dimension::Control) * 7;
    struct Placement {
        QRectF rect;
        QHash<QString, qreal> columnY;
    };
    QHash<QString, Placement> placed;
    QSet<QString> outgoing, incoming;
    for (const auto& edge : graph_.edges) {
        if (edge.sourceId == selectedId_ && edge.targetId != selectedId_)
            outgoing.insert(edge.targetId);
        if (edge.targetId == selectedId_ && edge.sourceId != selectedId_)
            incoming.insert(edge.sourceId);
    }
    QList<ObjectGraphTable> ordered = graph_.tables;
    std::sort(ordered.begin(), ordered.end(),
              [](const auto& a, const auto& b) { return a.id < b.id; });
    const auto widthFor = [&](const ObjectGraphTable& table) {
        qreal width =
            qMax(minWidth, qreal(headerMeasure.horizontalAdvance(table.qualifiedName)) + pad * 2);
        for (const auto& column : table.columns)
            width = qMax(width, pad * 9 + rowMeasure.horizontalAdvance(column.name) +
                                    rowMeasure.horizontalAdvance(column.databaseType));
        return width;
    };
    qreal centerWidth = minWidth;
    qreal centerHeight = headerHeight + rowHeight;
    qreal neighborWidth = 0;
    qreal neighborHeight = 0;
    for (const auto& table : ordered)
        if (table.id == selectedId_) {
            centerWidth = widthFor(table);
            centerHeight = headerHeight + qMax(1, table.columns.size()) * rowHeight;
        } else {
            neighborWidth = qMax(neighborWidth, widthFor(table));
            neighborHeight += headerHeight + qMax(1, table.columns.size()) * rowHeight;
        }
    horizontalWidth_ = centerWidth + neighborWidth + gap;
    denseLayout_ = ordered.size() > 6 ||
                   neighborHeight >
                       qMax(centerHeight * 3, design::dimension(design::Dimension::Control) * 18.0);
    stacked_ = !denseLayout_ && view_->viewport()->width() < horizontalWidth_;
    const auto place = [&](const ObjectGraphTable& table, qreal x, qreal y) {
        Placement placement;
        placement.rect =
            QRectF(x, y, widthFor(table), headerHeight + qMax(1, table.columns.size()) * rowHeight);
        for (int row = 0; row < table.columns.size(); ++row)
            placement.columnY.insert(table.columns.at(row).name,
                                     y + headerHeight + (row + 0.5) * rowHeight);
        placed.insert(table.id, placement);
    };
    if (denseLayout_) {
        // A bounded-height lane keeps a large one-hop neighborhood near its center.
        // Lane membership depends only on graph data, so resizing cannot shuffle tables.
        const qreal rowGap = design::spacing(design::Spacing::Four) * 2;
        const qreal laneHeight =
            qMax(centerHeight * 2, design::dimension(design::Dimension::Control) * 18.0);
        QList<ObjectGraphTable> left, right;
        for (const auto& table : ordered) {
            if (table.id == selectedId_)
                place(table, 0, 0);
            else if (incoming.contains(table.id) && !outgoing.contains(table.id))
                left.append(table);
            else
                right.append(table);
        }
        const auto placeSide = [&](const QList<ObjectGraphTable>& side, bool toRight) {
            QList<QList<ObjectGraphTable>> lanes;
            qreal used = 0;
            for (const auto& table : side) {
                const qreal height = headerHeight + qMax(1, table.columns.size()) * rowHeight;
                if (lanes.isEmpty() ||
                    (used + rowGap + height > laneHeight && !lanes.last().isEmpty())) {
                    lanes.append(QList<ObjectGraphTable>{});
                    used = 0;
                }
                if (used > 0)
                    used += rowGap;
                lanes.last().append(table);
                used += height;
            }
            qreal distance = toRight ? centerWidth + gap : -gap;
            for (const auto& lane : lanes) {
                qreal width = 0, height = 0;
                for (const auto& table : lane) {
                    width = qMax(width, widthFor(table));
                    height += headerHeight + qMax(1, table.columns.size()) * rowHeight;
                }
                height += rowGap * qMax(0, lane.size() - 1);
                qreal y = (centerHeight - height) / 2;
                const qreal x = toRight ? distance : distance - width;
                for (const auto& table : lane) {
                    place(table, x, y);
                    y += headerHeight + qMax(1, table.columns.size()) * rowHeight + rowGap;
                }
                distance += (toRight ? 1 : -1) * (width + gap);
            }
        };
        placeSide(left, false);
        placeSide(right, true);
    } else {
        qreal leftY = 0, rightY = 0, stackedY = centerHeight + gap / 2;
        for (const auto& table : ordered) {
            const qreal width = widthFor(table);
            const qreal height = headerHeight + qMax(1, table.columns.size()) * rowHeight;
            qreal x = 0, y = 0;
            if (table.id != selectedId_) {
                if (stacked_) {
                    x = (centerWidth - width) / 2;
                    y = stackedY;
                    stackedY += height + design::spacing(design::Spacing::Four) * 2;
                } else {
                    const bool onRight =
                        outgoing.contains(table.id) || !incoming.contains(table.id);
                    x = onRight ? centerWidth + gap : -gap - width;
                    auto& nextY = onRight ? rightY : leftY;
                    y = nextY;
                    nextY += height + design::spacing(design::Spacing::Four) * 2;
                }
            }
            place(table, x, y);
        }
        const auto centerRect = placed.value(selectedId_).rect;
        const qreal sideHeight = qMax(leftY, rightY);
        const qreal shiftY =
            !stacked_ && sideHeight > 0 ? centerRect.height() / 2 - sideHeight / 2 : 0;
        for (auto it = placed.begin(); it != placed.end(); ++it) {
            if (it.key() == selectedId_)
                continue;
            it->rect.translate(0, shiftY);
            for (auto col = it->columnY.begin(); col != it->columnY.end(); ++col)
                col.value() += shiftY;
        }
    }
    QHash<QString, int> parallel;
    qreal top = 0, bottom = centerHeight;
    for (const auto& placement : placed) {
        top = qMin(top, placement.rect.top());
        bottom = qMax(bottom, placement.rect.bottom());
    }
    int routed = 0;
    for (const auto& edge : graph_.edges) {
        if (!placed.contains(edge.sourceId) || !placed.contains(edge.targetId))
            continue;
        const QString pair = edge.sourceId + QChar(0) + edge.targetId;
        const int parallelNumber = parallel.value(pair);
        parallel.insert(pair, parallelNumber + 1);
        const qreal separation = parallelNumber == 0
                                     ? 0
                                     : (parallelNumber % 2 ? 1 : -1) * ((parallelNumber + 1) / 2) *
                                           design::spacing(design::Spacing::Four);
        const auto& source = placed[edge.sourceId];
        const auto& target = placed[edge.targetId];
        const int count = qMax(1, edge.sourceColumns.size());
        for (int index = 0; index < count; ++index) {
            const QString sourceColumn = edge.sourceColumns.value(index);
            const QString targetColumn = edge.targetColumns.value(index);
            const bool sourceKnown = source.columnY.contains(sourceColumn);
            const bool targetKnown = target.columnY.contains(targetColumn);
            const qreal sy = sourceKnown ? source.columnY.value(sourceColumn)
                                         : source.rect.top() + headerHeight / 2;
            const qreal ty = targetKnown ? target.columnY.value(targetColumn)
                                         : target.rect.top() + headerHeight / 2;
            const bool self = edge.sourceId == edge.targetId;
            const bool toRight = target.rect.center().x() >= source.rect.center().x();
            const QPointF start(toRight ? source.rect.right() : source.rect.left(), sy);
            const QPointF end(self      ? target.rect.right()
                              : toRight ? target.rect.left()
                                        : target.rect.right(),
                              ty);
            QPainterPath path(start);
            if (self) {
                const qreal loop = gap / 2 + index * design::spacing(design::Spacing::Two) +
                                   parallelNumber * design::spacing(design::Spacing::Four);
                path.cubicTo(QPointF(start.x() + loop, start.y() - rowHeight),
                             QPointF(end.x() + loop, end.y() + rowHeight), end);
            } else if (denseLayout_ && qAbs(end.x() - start.x()) > gap * 1.5) {
                // The vertical legs sit in the gaps beside their endpoint boxes.
                // Cross above or below the lanes so distant links do not cut through boxes.
                const qreal firstX = start.x() + (toRight ? gap / 2 : -gap / 2);
                const qreal lastX = end.x() + (toRight ? -gap / 2 : gap / 2);
                const qreal detour = gap / 2 + (routed / 2) * design::spacing(design::Spacing::One);
                const qreal railY = routed % 2 ? bottom + detour : top - detour;
                ++routed;
                path.lineTo(firstX, sy);
                path.lineTo(firstX, railY);
                path.lineTo(lastX, railY);
                path.lineTo(lastX, ty);
                path.lineTo(end);
            } else {
                const qreal bend = (start.x() + end.x()) / 2;
                path.cubicTo(QPointF(bend, start.y() + separation),
                             QPointF(bend, end.y() + separation), end);
            }
            auto* line =
                scene_->addPath(path, QPen(colors.primary, design::focusSpec().borderWidth + 1));
            line->setToolTip(
                tr("%1: %2.%3 → %4.%5")
                    .arg(edge.id, edge.sourceId,
                         sourceKnown ? sourceColumn : tr("source column unavailable"),
                         edge.targetId,
                         targetKnown ? targetColumn : tr("target column unavailable")));
            line->setData(tableIdRole, QVariant{});
            const qreal direction = self || end.x() < start.x() ? 1 : -1;
            QPainterPath arrow(end);
            arrow.lineTo(end + QPointF(direction * design::spacing(design::Spacing::Two),
                                       -design::spacing(design::Spacing::One)));
            arrow.moveTo(end);
            arrow.lineTo(end + QPointF(direction * design::spacing(design::Spacing::Two),
                                       design::spacing(design::Spacing::One)));
            scene_->addPath(arrow, QPen(colors.primary, design::focusSpec().borderWidth + 1));
        }
    }
    for (const auto& table : ordered) {
        const auto placement = placed.value(table.id);
        const bool central = table.id == selectedId_;
        auto* box = scene_->addRect(placement.rect,
                                    QPen(central ? colors.primary : colors.border,
                                         central ? 2 : design::focusSpec().borderWidth),
                                    QBrush(central ? colors.selection : colors.surfaceRaised));
        box->setData(tableIdRole, table.id);
        box->setData(tableNameRole, table.qualifiedName);
        box->setToolTip(tr("Open %1").arg(table.qualifiedName));
        box->setFlag(QGraphicsItem::ItemIsSelectable);
        boxes_.insert(table.id, box);
        auto addText = [&](const QString& value, const QFont& font, const QColor& color, qreal x,
                           qreal y) {
            auto* text = new QGraphicsSimpleTextItem(value, box);
            text->setFont(font);
            text->setBrush(color);
            text->setPos(x, y);
            text->setToolTip(value);
        };
        addText(table.qualifiedName, headerFont, colors.fg, placement.rect.x() + pad,
                placement.rect.y() + (headerHeight - headerMeasure.height()) / 2);
        for (int row = 0; row < table.columns.size(); ++row) {
            const auto& column = table.columns.at(row);
            const qreal y = placement.rect.y() + headerHeight + row * rowHeight;
            addText(marker(column), rowFont, colors.primary, placement.rect.x() + pad,
                    y + (rowHeight - rowMeasure.height()) / 2);
            addText(column.name, rowFont, colors.fg, placement.rect.x() + pad * 7,
                    y + (rowHeight - rowMeasure.height()) / 2);
            const qreal typeWidth = rowMeasure.horizontalAdvance(column.databaseType);
            addText(column.databaseType, rowFont, colors.fgMuted,
                    placement.rect.right() - pad - typeWidth,
                    y + (rowHeight - rowMeasure.height()) / 2);
        }
    }
    const qreal margin = stacked_ ? pad * 2 : gap / 2;
    scene_->setSceneRect(scene_->itemsBoundingRect().adjusted(-margin, -margin, margin, margin));
}

} // namespace choscordb
