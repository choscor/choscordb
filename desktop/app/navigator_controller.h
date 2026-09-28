#pragma once
#include <QList>
#include <QModelIndex>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <functional>
#include <optional>
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
    enum class RevealResult { Found, Unavailable, Retry };
    NavigatorController(EngineAdapter* engine, QTreeView* tree, QLineEdit* filter,
                        QWidget* dialogParent);
    NavigatorModel* model() const { return model_; }
    void addConnection(quint64 connection, const QString& label);
    void renameConnection(quint64 connection, const QString& label);
    void setVisibleConnections(const QList<quint64>& orderedIds);
    void setSelectedConnection(quint64 connection);
    void clearSelectedConnection();
    void setPendingConnection(quint64 pendingId, const QString& label);
    void removePendingConnection(quint64 pendingId);
    void setPendingConnection(const QString& label);
    quint64 selectedConnection() const;
    void setDriverResolver(std::function<QString(quint64)> resolver);
    void setPinStateResolver(std::function<std::optional<bool>(const QModelIndex&)> resolver);
    void populateContextMenu(QMenu* menu, const QModelIndex& sourceIndex);
    bool revealObject(quint64 connection, const QStringList& ancestryIds, const QString& objectId,
                      const QString& kind, const QString& qualifiedName,
                      const QString& relationSubtype,
                      std::function<void(RevealResult, const QString&)> finished,
                      std::function<bool()> stillCurrent = {});
    void refreshCurrent();
    void disconnectCurrent();
  signals:
    void disconnectRequested(quint64 connection);
    void sqlGenerated(quint64 connection, const QString& sql);
    void ddlRequested(quint64 connection, const QString& objectId, const QString& label,
                      const QString& kind, const QVariantList& properties);
    void generationFailed(const QString& error);
    void searchStatusChanged(const QString& status);
    void objectActionRequested(const QString& action, quint64 connection,
                               const QString& objectId, const QString& shortName,
                               const QString& kind, const QString& parentObjectId,
                               const QString& qualifiedName, const QString& relationSubtype);
    void pinRequested(const QModelIndex& sourceIndex, bool unpin);

  private:
    NavigatorModel* model_;
    QPointer<EngineAdapter> engine_;
    QTreeView* tree_;
    QSortFilterProxyModel* proxy_;
    QLineEdit* filter_;
    std::function<QString(quint64)> driverResolver_;
    std::function<std::optional<bool>(const QModelIndex&)> pinStateResolver_;
    quint64 searchGeneration_ = 0;
    int searchRequests_ = 0;
    bool searchPending_ = false;
    quint64 searchPendingConnection_ = 0;
    QString searchError_;
    void advanceSearch(quint64 generation);
};
} // namespace choscordb
