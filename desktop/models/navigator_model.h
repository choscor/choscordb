#pragma once
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
        DatabaseTypeRole
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
    // Token must be echoed from childrenRequested, never derived on response.
    // Refresh supersedes earlier requests; late replies cannot replace new data.
    bool applyChildren(quint64 connection, const QString& parentObjectId, quint64 token,
                       std::vector<NavigatorObject> children);
    bool applyChildrenPage(quint64 connection, const QString& parentObjectId, quint64 token,
                           std::vector<NavigatorObject> children, quint64 offset, bool hasMore,
                           quint64 nextOffset);
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
    // A (connection, object ID) must identify exactly one currently loaded node.
    // Duplicate IDs under different parents are intentionally not resolved.
    std::optional<NavigatorObjectSnapshot> objectSnapshot(quint64 connection,
                                                          const QString& objectId) const;
    // Accepted loaded metadata only. partial marks omitted or still-unloaded data;
    // maxUtf8Bytes charges all copied strings, including object IDs.
    CompletionSnapshot completionSnapshot(quint64 connection, quint64 maxEntries,
                                          quint64 maxUtf8Bytes) const;
    // The "Relation subtype" metadata property, or empty when absent.
    static QString relationSubtype(const QVariantList& properties);
  signals:
    void completionChanged(quint64 connection);
    // Dispatched on the next event-loop turn so fetchMore callers cannot be
    // reentered by a synchronous metadata provider.
    void childrenRequested(quint64 connection, const QString& parentObjectId, quint64 requestToken);

    void childrenPageRequested(quint64 connection, const QString& parentObjectId,
                               quint64 requestToken, quint64 offset, quint32 limit);

  private:
    struct Node;
    Node* node(const QModelIndex& index) const;
    Node* find(quint64 connection, const QString& id) const;
    QModelIndex indexFor(Node* node) const;
    bool isBrowsable(const Node* node) const;
    bool hiddenBySystemSchema(const Node* node) const;
    void clearChildren(Node* node);
    void registerNode(Node* node);
    void unregisterSubtree(const Node* node);
    std::vector<std::unique_ptr<Node>> roots_;
    // Loaded, non-placeholder nodes by connection and object ID. Only leaf
    // index rows may share an ID, so lookups stay O(1) for every loaded page.
    QHash<quint64, QMultiHash<QString, Node*>> nodesById_;
    mutable QHash<QString, bool> unverifiedVisibleByDriver_;
    std::function<QString(quint64)> driverResolver_;
    bool showSystemSchemas_ = false;
    quint64 nextToken_ = 0;
};
} // namespace choscordb
