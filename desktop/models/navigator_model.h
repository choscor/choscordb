#pragma once
#include <QAbstractItemModel>
#include <QString>
#include <memory>
#include <vector>
namespace choscordb {
struct NavigatorObject {
    QString id;
    QString name;
    QString qualifiedName;
    QString kind;
    bool hasChildren = false;
};
struct CompletionSnapshot {
    std::vector<NavigatorObject> objects;
    bool partial = false;
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
        ChildrenLoadedRole
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
    bool removeConnection(quint64 id);
    // Token must be echoed from childrenRequested, never derived on response.
    // Refresh supersedes earlier requests; late replies cannot replace new data.
    bool applyChildren(quint64 connection, const QString& parentObjectId, quint64 token,
                       std::vector<NavigatorObject> children);
    bool failChildren(quint64 connection, const QString& parentObjectId, quint64 token,
                      const QString& error);
    void refresh(const QModelIndex& index);
    // Accepted loaded metadata only. partial marks omitted or still-unloaded data;
    // maxUtf8Bytes charges all copied strings, including object IDs.
    CompletionSnapshot completionSnapshot(quint64 connection, quint64 maxEntries,
                                          quint64 maxUtf8Bytes) const;
  signals:
    void completionChanged(quint64 connection);
    // Dispatched on the next event-loop turn so fetchMore callers cannot be
    // reentered by a synchronous metadata provider.
    void childrenRequested(quint64 connection, const QString& parentObjectId, quint64 requestToken);

  private:
    struct Node;
    Node* node(const QModelIndex& index) const;
    Node* find(quint64 connection, const QString& id) const;
    QModelIndex indexFor(Node* node) const;
    void clearChildren(Node* node);
    std::vector<std::unique_ptr<Node>> roots_;
    quint64 nextToken_ = 0;
};
} // namespace choscordb
