#pragma once

#include "bridge/engine_adapter.h"
#include <QGraphicsView>
#include <QHash>
#include <QPoint>
#include <QWidget>

class QGraphicsRectItem;
class QGraphicsScene;
class QPushButton;

namespace choscordb {

// Read-only one-hop graph presentation. Object identity and loading stay with ObjectExplorer.
class ObjectErdWidget final : public QWidget {
    Q_OBJECT
  public:
    explicit ObjectErdWidget(QWidget* parent = nullptr);
    void setGraph(const ObjectGraph& graph, const QString& selectedId);
    void clearGraph();
    const ObjectGraph& graph() const { return graph_; }
    qreal zoomFactor() const;
    void fitGraph();

  signals:
    void tableActivated(const QString& id, const QString& qualifiedName);

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void changeEvent(QEvent* event) override;

  private:
    void render();
    void zoom(qreal factor);
    void activate(const QString& id);
    ObjectGraph graph_;
    QString selectedId_;
    QGraphicsView* view_;
    QGraphicsScene* scene_;
    QHash<QString, QGraphicsRectItem*> boxes_;
    qreal horizontalWidth_ = 0;
    bool stacked_ = false;
    bool panning_ = false;
    QPoint panStart_;
    QPoint scrollStart_;
    QPushButton* zoomIn_;
    QPushButton* zoomOut_;
    QPushButton* fit_;
};

} // namespace choscordb
