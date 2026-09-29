#pragma once
#include <QList>
#include <QModelIndex>
#include <QObject>
#include <QPersistentModelIndex>
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
bool showsSidebarChild(const QModelIndex& index);
struct QuickObjectResult {
    quint64 connection = 0;
    QString objectId;
    QString name;
    QString qualifiedName;
    QString kind;
    QString parentObjectId;
    QString context;
    QVariantList properties;
    QString targetObjectId;
    QString targetQualifiedName;
    QString targetKind;
    QString targetParentObjectId;
    QVariantList targetProperties;
    int targetPane = -1;
};
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
    bool isVisibleConnection(quint64 connection) const;
    bool hasSelectedConnection() const;
    void startQuickObjectSearch(const QString& query,
                                std::optional<quint64> connection = std::nullopt);
    void cancelQuickObjectSearch();
    QList<QuickObjectResult> quickObjectResults() const { return quickObjectResults_; }
    QString quickObjectSearchStatus() const { return quickObjectStatus_; }
    bool quickObjectSearchIncomplete() const { return quickObjectIncomplete_; }
    quint64 quickObjectSearchGeneration() const { return quickObjectGeneration_; }
    void setDriverResolver(std::function<QString(quint64)> resolver);
    void setPinStateResolver(std::function<std::optional<bool>(const QModelIndex&)> resolver);
    void setShowSystemSchemas(bool show);
    void populateContextMenu(QMenu* menu, const QModelIndex& sourceIndex);
    bool revealObject(quint64 connection, const QStringList& ancestryIds, const QString& objectId,
                      const QString& kind, const QString& qualifiedName,
                      const QString& relationSubtype,
                      std::function<void(RevealResult, const QString&)> finished,
                      std::function<bool()> stillCurrent = {});
    // Finds the exact live source row without changing explorer visibility, filter, or selection.
    // A failed lookup supplies an invalid index; false means the connection is not live in model().
    bool
    resolveObject(quint64 connection, const QStringList& ancestryIds, const QString& objectId,
                  const QString& kind, const QString& qualifiedName, const QString& relationSubtype,
                  std::function<void(RevealResult, const QString&, const QModelIndex&)> finished,
                  std::function<bool()> stillCurrent = {}, bool refreshFinalParent = true);
    void refreshCurrent();
    void disconnectCurrent();
  signals:
    void selectedConnectionsChanged();
    void browsingVisibilityChanged();
    void disconnectRequested(quint64 connection);
    void sqlGenerated(quint64 connection, const QString& sql);
    void ddlRequested(quint64 connection, const QString& objectId, const QString& label,
                      const QString& kind, const QVariantList& properties);
    void generationFailed(const QString& error);
    void searchStatusChanged(const QString& status);
    void quickObjectSearchChanged();
    void objectActionRequested(const QString& action, quint64 connection, const QString& objectId,
                               const QString& shortName, const QString& kind,
                               const QString& parentObjectId, const QString& qualifiedName,
                               const QString& relationSubtype);
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
    QPersistentModelIndex searchPendingIndex_;
    quint64 searchPendingToken_ = 0;
    QString searchError_;
    bool matchesSearchRequest(quint64 connection, const QString& parent, quint64 token) const;
    QList<QuickObjectResult> quickObjectResults_;
    QString quickObjectStatus_;
    QString quickObjectQuery_;
    quint64 quickObjectGeneration_ = 0;
    quint64 quickObjectConnection_ = 0;
    bool quickObjectConnectionValid_ = false;
    int quickObjectRequests_ = 0;
    bool quickObjectIncomplete_ = false;
    void advanceSearch(quint64 generation);
    void advanceQuickObjectSearch(quint64 generation);
    bool
    lookupObject(quint64 connection, const QStringList& ancestryIds, const QString& objectId,
                 const QString& kind, const QString& qualifiedName, const QString& relationSubtype,
                 std::function<void(RevealResult, const QString&, const QModelIndex&)> finished,
                 std::function<bool()> stillCurrent, bool requireVisibleConnection,
                 bool refreshFinalParent);
};
} // namespace choscordb
