#pragma once
#include <QModelIndex>
#include <QObject>
#include <QPointer>
#include <QVariant>
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
    void setSelectedConnection(quint64 connection);
    void clearSelectedConnection();
    void setPendingConnection(const QString& label);
    quint64 selectedConnection() const;
    void populateContextMenu(QMenu* menu, const QModelIndex& sourceIndex);
    void refreshCurrent();
    void disconnectCurrent();
  signals:
    void disconnectRequested(quint64 connection);
    void sqlGenerated(quint64 connection, const QString& sql);
    void ddlRequested(quint64 connection, const QString& objectId, const QString& label,
                      const QString& kind, const QVariantList& properties);
    void generationFailed(const QString& error);
    void searchStatusChanged(const QString& status);

  private:
    NavigatorModel* model_;
    QPointer<EngineAdapter> engine_;
    QTreeView* tree_;
    QSortFilterProxyModel* proxy_;
    QLineEdit* filter_;
    quint64 searchGeneration_ = 0;
    int searchRequests_ = 0;
    bool searchPending_ = false;
    bool searchAborted_ = false;
    void advanceSearch(quint64 generation);
};
} // namespace choscordb
