#pragma once
#include "bridge/completion_service.h"
#include "bridge/engine_adapter.h"
#include <QAbstractItemModel>
#include <QHash>
#include <QMultiHash>
#include <QString>
#include <QVariantList>
#include <functional>
#include <memory>
#include <optional>
#include <vector>
namespace choscordb {
struct NavigatorObject {
    QString id;
    QString name;
    QString qualifiedName;
    QString kind;
    bool hasChildren = false;
    QVariantList properties;
    QString databaseType;
    NavigatorObject() = default;
    NavigatorObject(QString id, QString name, QString qualifiedName, QString kind, bool hasChildren,
                    QVariantList properties = {})
        : id(std::move(id)), name(std::move(name)), qualifiedName(std::move(qualifiedName)),
          kind(std::move(kind)), hasChildren(hasChildren), properties(std::move(properties)) {}
};
struct CompletionSnapshot {
    std::vector<NavigatorObject> objects;
    bool partial = false;
};
struct NavigatorObjectSnapshot {
    quint64 connection = 0;
    QString objectId;
    QString name;
    QString qualifiedName;
    QString kind;
    QString parentObjectId;
    QVariantList properties;
};
class NavigatorModel final : public QAbstractItemModel {
    Q_OBJECT
  public:
    enum Role {
        ConnectionRole = Qt::UserRole + 1,
        ObjectIdRole,
        QualifiedNameRole,
        KindRole,
        ErrorRole,
        ChildrenLoadedRole,
        PropertiesRole,
        HasMoreRole,
        DatabaseTypeRole,
        StatusRole,
        RetryRole,
        SidebarChildRole
    };
    explicit NavigatorModel(QObject* parent = nullptr);
    ~NavigatorModel() override;
    QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& index) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool hasChildren(const QModelIndex& parent = {}) const override;
    bool canFetchMore(const QModelIndex& parent) const override;
    void fetchMore(const QModelIndex& parent) override;
    bool addConnection(quint64 id, const QString& label);
    bool renameConnection(quint64 id, const QString& label);
    bool addPendingConnection(quint64 id, const QString& label);
    bool removeConnection(quint64 id);
    bool applyChildren(quint64 connection, const QString& parentObjectId, quint64 token,
                       std::vector<NavigatorObject> children);
    bool applyChildrenPage(quint64 connection, const QString& parentObjectId, quint64 token,
                           std::vector<NavigatorObject> children, quint64 offset, bool hasMore,
                           quint64 nextOffset);
    bool applyCachedChildren(quint64 connection, const QString& parentObjectId, quint64 token,
                             std::vector<NavigatorObject> children);
    void requestNextPage(const QModelIndex& index);
    bool failChildren(quint64 connection, const QString& parentObjectId, quint64 token,
                      const QString& error);
    void refresh(const QModelIndex& index);
    bool refreshObject(quint64 connection, const QString& objectId);
    bool matchesObject(quint64 connection, const QString& objectId, const QString& kind,
                       const QString& qualifiedName, const QString& parentObjectId,
                       const QString& relationSubtype = {}, bool requireBrowsable = false) const;
    void setDriverResolver(std::function<QString(quint64)> resolver);
    void setShowSystemSchemas(bool show);
    bool showSystemSchemas() const { return showSystemSchemas_; }
    bool canShowUnverifiedObject(quint64 connection, const QString& qualifiedName = {}) const;
    bool isBrowsable(const QModelIndex& index) const;
    quint64 pendingRequestToken(const QModelIndex& index) const;
    std::optional<NavigatorObjectSnapshot> objectSnapshot(quint64 connection,
                                                          const QString& objectId) const;
    CompletionSnapshot completionSnapshot(quint64 connection, const CompletionLimits& limits) const;
    static QString relationSubtype(const QVariantList& properties);
  signals:
    void completionChanged(quint64 connection);
    void childrenRequested(quint64 connection, const QString& parentObjectId, quint64 requestToken,
                           bool fresh);

    void childrenPageRequested(quint64 connection, const QString& parentObjectId,
                               quint64 requestToken, quint64 offset);

  private:
    struct Node;
    Node* node(const QModelIndex& index) const;
    Node* find(quint64 connection, const QString& id) const;
    QModelIndex indexFor(Node* node) const;
    bool isBrowsable(const Node* node) const;
    bool hiddenBySystemSchema(const Node* node) const;
    void clearChildren(Node* node);
    void request(Node* node, bool fresh);
    bool insertChildren(Node* node, std::vector<NavigatorObject> children, bool hasMore,
                        quint64 nextOffset, bool cached);
    std::unique_ptr<Node> makeChild(Node* parent, NavigatorObject object) const;
    bool reconcileSaved(Node* node, std::vector<NavigatorObject>& children);
    void setStatus(Node* node, RowStatusCause cause, const QString& detail = {});
    void registerNode(Node* node);
    void unregisterSubtree(const Node* node);
    std::vector<std::unique_ptr<Node>> roots_;
    QHash<quint64, QMultiHash<QString, Node*>> nodesById_;
    mutable QHash<QString, bool> unverifiedVisibleByDriver_;
    std::function<QString(quint64)> driverResolver_;
    bool showSystemSchemas_ = false;
    quint64 nextToken_ = 0;
};
}
