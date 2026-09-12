#pragma once
#include <QModelIndex>
#include <QObject>
#include <QPointer>
class QMenu;
class QTreeView;
class QLineEdit;
class QWidget;
class QSortFilterProxyModel;
namespace choscordb {
class EngineAdapter;
class NavigatorModel;
class NavigatorController final : public QObject {
    Q_OBJECT
  public:
    NavigatorController(EngineAdapter* engine, QTreeView* tree, QLineEdit* filter,
                        QWidget* dialogParent);
    NavigatorModel* model() const { return model_; }
    void addConnection(quint64 connection, const QString& label);
    void populateContextMenu(QMenu* menu, const QModelIndex& sourceIndex);
    void refreshCurrent();
    void disconnectCurrent();
  signals:
    void disconnectRequested(quint64 connection);
    void sqlGenerated(quint64 connection, const QString& sql);
    void generationFailed(const QString& error);

  private:
    NavigatorModel* model_;
    QPointer<EngineAdapter> engine_;
    QTreeView* tree_;
    QSortFilterProxyModel* proxy_;
};
} // namespace choscordb
