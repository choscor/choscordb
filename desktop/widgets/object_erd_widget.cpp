#include "widgets/object_erd_widget.h"

#include "design_system/button/button.h"
#include "design_system/theme.h"
#include <QEvent>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
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
    const auto metrics = design::resolveMetrics(design::Density::Compact, true);
    auto* controls = new QWidget(this);
    auto* row = new QHBoxLayout(controls);
    row->setContentsMargins(metrics.spacingMedium, metrics.spacingSmall, metrics.spacingMedium,
                            metrics.spacingSmall);
    row->setSpacing(metrics.spacingSmall);
    auto makeControl = [this, row](const QString& label, const QString& name) {
        auto* button = new design::Button(label, this);
        button->setObjectName(name);
        button->setAccessibleName(label);
        button->setVariant(design::ButtonVariant::Outline);
        button->setButtonSize(design::ButtonSize::Small);
        row->addWidget(button);
        return button;
    };
    zoomIn_ = makeControl(tr("Zoom in"), "objectErdZoomIn");
    zoomOut_ = makeControl(tr("Zoom out"), "objectErdZoomOut");
    fit_ = makeControl(tr("Fit diagram"), "objectErdFit");
    row->addStretch();
    layout->addWidget(controls);
    view_->setObjectName("objectErdView");
    view_->setAccessibleName(tr("Entity relationship diagram. Use arrow keys to select a table, "
                                "Enter to open it, plus and minus to zoom, and 0 to fit."));
    view_->setScene(scene_);
    view_->setRenderHint(QPainter::Antialiasing);
    view_->setDragMode(QGraphicsView::ScrollHandDrag);
    view_->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    view_->setFocusPolicy(Qt::StrongFocus);
    view_->setFrameShape(QFrame::NoFrame);
    view_->viewport()->installEventFilter(this);
    view_->installEventFilter(this);
    layout->addWidget(view_, 1);
    connect(zoomIn_, &QPushButton::clicked, this, [this] { zoom(1.2); });
    connect(zoomOut_, &QPushButton::clicked, this, [this] { zoom(1.0 / 1.2); });
    connect(fit_, &QPushButton::clicked, this, &ObjectErdWidget::fitGraph);
}

void ObjectErdWidget::clearGraph() {
    panning_ = false;
    graph_ = {};
    selectedId_.clear();
    boxes_.clear();
    scene_->clear();
    view_->resetTransform();
}

void ObjectErdWidget::setGraph(const ObjectGraph& graph, const QString& selectedId) {
    graph_ = graph;
    selectedId_ = selectedId;
    render();
    QTimer::singleShot(0, this, [this] { fitGraph(); });
}

qreal ObjectErdWidget::zoomFactor() const {
    return view_->transform().m11();
}

void ObjectErdWidget::fitGraph() {
    if (scene_->items().isEmpty())
        return;
    view_->fitInView(scene_->sceneRect(), Qt::KeepAspectRatio);
    if (view_->transform().m11() > 1.0)
        view_->scale(1.0 / view_->transform().m11(), 1.0 / view_->transform().m22());
}

void ObjectErdWidget::zoom(qreal factor) {
    const auto next = zoomFactor() * factor;
    if (next < 0.1 || next > 4.0)
        return;
    view_->scale(factor, factor);
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
            int vertical = wheel->angleDelta().y();
            if (vertical == 0)
                vertical = wheel->pixelDelta().y();
            if (vertical == 0)
                return QWidget::eventFilter(watched, event);
            zoom(vertical > 0 ? 1.2 : 1.0 / 1.2);
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
                if (!graph_.tables.isEmpty() &&
                    (view_->viewport()->width() < horizontalWidth_) != stacked_) {
                    render();
                    fitGraph();
                } else if (zoomFactor() == 1.0) {
                    fitGraph();
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
    view_->setBackgroundBrush(colors.canvas);
    const auto metrics = design::resolveMetrics(design::Density::Compact, true);
    const QFont headerFont = design::resolveTypography(design::TypographyRole::Heading);
    const QFont rowFont = design::resolveTypography(design::TypographyRole::Metadata);
    const QFontMetrics headerMeasure(headerFont), rowMeasure(rowFont);
    const qreal pad = metrics.spacingMedium;
    const qreal rowHeight = qMax(metrics.dataRowHeight, rowMeasure.height() + metrics.spacingSmall);
    const qreal headerHeight =
        qMax(metrics.controlHeight, headerMeasure.height() + metrics.spacingMedium);
    const qreal gap = metrics.spacingLarge * 6;
    const qreal minWidth = metrics.controlHeight * 7;
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
    for (const auto& table : ordered)
        if (table.id == selectedId_) {
            centerWidth = widthFor(table);
            centerHeight = headerHeight + qMax(1, table.columns.size()) * rowHeight;
        } else {
            neighborWidth = qMax(neighborWidth, widthFor(table));
        }
    horizontalWidth_ = centerWidth + neighborWidth + gap;
    stacked_ = view_->viewport()->width() < horizontalWidth_;
    qreal leftY = 0, rightY = 0, stackedY = centerHeight + gap / 2;
    for (const auto& table : ordered) {
        const qreal width = widthFor(table);
        const qreal height = headerHeight + qMax(1, table.columns.size()) * rowHeight;
        qreal x = 0, y = 0;
        if (table.id != selectedId_) {
            if (stacked_) {
                x = (centerWidth - width) / 2;
                y = stackedY;
                stackedY += height + metrics.spacingLarge * 2;
            } else {
                const bool onRight = outgoing.contains(table.id) || !incoming.contains(table.id);
                x = onRight ? centerWidth + gap : -gap - width;
                auto& nextY = onRight ? rightY : leftY;
                y = nextY;
                nextY += height + metrics.spacingLarge * 2;
            }
        }
        Placement placement;
        placement.rect = QRectF(x, y, width, height);
        for (int row = 0; row < table.columns.size(); ++row)
            placement.columnY.insert(table.columns.at(row).name,
                                     y + headerHeight + (row + 0.5) * rowHeight);
        placed.insert(table.id, placement);
    }
    const auto centerRect = placed.value(selectedId_).rect;
    const qreal sideHeight = qMax(leftY, rightY);
    const qreal shiftY = !stacked_ && sideHeight > 0 ? centerRect.height() / 2 - sideHeight / 2 : 0;
    for (auto it = placed.begin(); it != placed.end(); ++it) {
        if (it.key() == selectedId_)
            continue;
        it->rect.translate(0, shiftY);
        for (auto col = it->columnY.begin(); col != it->columnY.end(); ++col)
            col.value() += shiftY;
    }
    QHash<QString, int> parallel;
    for (const auto& edge : graph_.edges) {
        if (!placed.contains(edge.sourceId) || !placed.contains(edge.targetId))
            continue;
        const QString pair = edge.sourceId + QChar(0) + edge.targetId;
        const int parallelNumber = parallel.value(pair);
        parallel.insert(pair, parallelNumber + 1);
        const qreal separation =
            parallelNumber == 0
                ? 0
                : (parallelNumber % 2 ? 1 : -1) * ((parallelNumber + 1) / 2) * metrics.spacingLarge;
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
                const qreal loop = gap / 2 + index * metrics.spacingMedium + qAbs(separation);
                path.cubicTo(QPointF(start.x() + loop, start.y() - rowHeight),
                             QPointF(end.x() + loop, end.y() + rowHeight), end);
            } else {
                const qreal bend = (start.x() + end.x()) / 2;
                path.cubicTo(QPointF(bend, start.y() + separation),
                             QPointF(bend, end.y() + separation), end);
            }
            auto* line = scene_->addPath(path, QPen(colors.action, metrics.separatorWidth + 1));
            line->setToolTip(
                tr("%1: %2.%3 → %4.%5")
                    .arg(edge.id, edge.sourceId,
                         sourceKnown ? sourceColumn : tr("source column unavailable"),
                         edge.targetId,
                         targetKnown ? targetColumn : tr("target column unavailable")));
            line->setData(tableIdRole, QVariant{});
            const qreal direction = self || end.x() < start.x() ? 1 : -1;
            QPainterPath arrow(end);
            arrow.lineTo(end + QPointF(direction * metrics.spacingMedium, -metrics.spacingSmall));
            arrow.moveTo(end);
            arrow.lineTo(end + QPointF(direction * metrics.spacingMedium, metrics.spacingSmall));
            scene_->addPath(arrow, QPen(colors.action, metrics.separatorWidth + 1));
        }
    }
    for (const auto& table : ordered) {
        const auto placement = placed.value(table.id);
        const bool central = table.id == selectedId_;
        auto* box = scene_->addRect(
            placement.rect,
            QPen(central ? colors.primary : colors.border, central ? 2 : metrics.separatorWidth),
            QBrush(central ? colors.subtleAccent : colors.elevatedSurface));
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
        addText(table.qualifiedName, headerFont, colors.text, placement.rect.x() + pad,
                placement.rect.y() + (headerHeight - headerMeasure.height()) / 2);
        for (int row = 0; row < table.columns.size(); ++row) {
            const auto& column = table.columns.at(row);
            const qreal y = placement.rect.y() + headerHeight + row * rowHeight;
            addText(marker(column), rowFont, colors.action, placement.rect.x() + pad,
                    y + (rowHeight - rowMeasure.height()) / 2);
            addText(column.name, rowFont, colors.text, placement.rect.x() + pad * 7,
                    y + (rowHeight - rowMeasure.height()) / 2);
            const qreal typeWidth = rowMeasure.horizontalAdvance(column.databaseType);
            addText(column.databaseType, rowFont, colors.mutedText,
                    placement.rect.right() - pad - typeWidth,
                    y + (rowHeight - rowMeasure.height()) / 2);
        }
    }
    const qreal margin = stacked_ ? pad * 2 : gap / 2;
    scene_->setSceneRect(scene_->itemsBoundingRect().adjusted(-margin, -margin, margin, margin));
}

} // namespace choscordb
