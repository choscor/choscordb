#include "app/navigator_controller.h"
#include "bridge/engine_adapter.h"
#include "bridge/template_service.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/menu/menu.h"
#include "models/navigator_model.h"
#include <QApplication>
#include <QClipboard>
#include <QLineEdit>
#include <QMenu>
#include <QPersistentModelIndex>
#include <QSortFilterProxyModel>
#include <QTimer>
#include <QTreeView>
#include <limits>
namespace choscordb {
namespace {
QString text(const rust::String& s) {
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}
class SelectedConnectionProxy final : public QSortFilterProxyModel {
  public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    void select(quint64 connection) {
        if (hasSelection_ && selected_ == connection)
            return;
        selected_ = connection;
        hasSelection_ = true;
        refreshFilter();
    }
    void clear() {
        hasSelection_ = false;
        refreshFilter();
    }
    quint64 selected() const { return selected_; }
    bool hasSelection() const { return hasSelection_; }

  protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override {
        if (!parent.isValid()) {
            const auto root = sourceModel()->index(row, 0);
            if (!hasSelection_ ||
                root.data(NavigatorModel::ConnectionRole).toULongLong() != selected_)
                return false;
        }
        return QSortFilterProxyModel::filterAcceptsRow(row, parent);
    }

  private:
    void refreshFilter() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        beginFilterChange();
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
        invalidateFilter();
#endif
    }
    quint64 selected_ = 0;
    bool hasSelection_ = false;
};
} // namespace
NavigatorController::NavigatorController(EngineAdapter* engine, QTreeView* tree, QLineEdit* filter,
                                         QWidget* dialogParent)
    : QObject(tree), model_(new NavigatorModel(this)), engine_(engine), tree_(tree),
      proxy_(new SelectedConnectionProxy(this)), filter_(filter) {
    Q_UNUSED(dialogParent);
    auto* proxy = proxy_;
    proxy->setSourceModel(model_);
    proxy->setRecursiveFilteringEnabled(true);
    proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    tree->setModel(proxy);
    connect(filter, &QLineEdit::textChanged, proxy, &QSortFilterProxyModel::setFilterFixedString);
    connect(filter, &QLineEdit::textChanged, this, [this] {
        ++searchGeneration_;
        searchRequests_ = 0;
        searchPending_ = false;
        searchAborted_ = false;
        if (filter_->text().trimmed().isEmpty()) {
            emit searchStatusChanged({});
            return;
        }
        emit searchStatusChanged(tr("Searching objects…"));
        advanceSearch(searchGeneration_);
    });
    connect(model_, &NavigatorModel::completionChanged, this, [this](quint64 connection) {
        if (connection == selectedConnection() && !filter_->text().trimmed().isEmpty()) {
            searchPending_ = false;
            const auto generation = searchGeneration_;
            QTimer::singleShot(0, this, [this, generation] { advanceSearch(generation); });
        }
    });
    connect(model_, &NavigatorModel::childrenRequested, engine, &EngineAdapter::loadMetadata);
    connect(model_, &NavigatorModel::childrenPageRequested, engine,
            &EngineAdapter::loadMetadataPage);
    connect(tree, &QTreeView::activated, this, [this](const QModelIndex& index) {
        const auto source = proxy_->mapToSource(index);
        if (source.data(NavigatorModel::KindRole).toString() == "load_more")
            model_->requestNextPage(source);
    });
    connect(engine, &EngineAdapter::metadataSubmissionFailed, this,
            [this](quint64 connection, const QString& parent, quint64 token, const QString& error) {
                const bool accepted = model_->failChildren(connection, parent, token, error);
                if (accepted && searchPending_ && connection == selectedConnection()) {
                    searchPending_ = false;
                    if (!filter_->text().trimmed().isEmpty()) {
                        searchAborted_ = true;
                        emit searchStatusChanged(
                            tr("Search incomplete: %1. Refine the text or retry.").arg(error));
                    }
                }
            });
    connect(
        engine, &EngineAdapter::eventReady, this,
        [this](const BridgeEvent& e) {
            const auto kind = text(e.kind);
            if (kind == "disconnected")
                model_->removeConnection(e.id);
            else if (kind == "metadata") {
                std::vector<NavigatorObject> objects;
                objects.reserve(e.objects.size());
                for (const auto& object : e.objects) {
                    QVariantList properties;
                    for (const auto& property : object.properties)
                        properties.append(QVariantMap{{"name", text(property.name)},
                                                      {"value", text(property.value)},
                                                      {"availability", text(property.availability)},
                                                      {"reason", text(property.reason)}});
                    objects.push_back({text(object.id), text(object.name),
                                       text(object.qualified_name), text(object.kind),
                                       object.has_children, std::move(properties)});
                }
                const bool accepted = model_->applyChildrenPage(
                    e.id, text(e.parent), e.request_token, std::move(objects), e.metadata_offset,
                    e.has_more_metadata, e.next_metadata_offset);
                if (accepted && e.id == selectedConnection() && searchPending_) {
                    searchPending_ = false;
                    const auto generation = searchGeneration_;
                    QTimer::singleShot(0, this, [this, generation] { advanceSearch(generation); });
                }
            } else if (kind == "metadata_failed") {
                const bool accepted =
                    model_->failChildren(e.id, text(e.parent), e.request_token, text(e.error));
                if (accepted && searchPending_ && e.id == selectedConnection()) {
                    searchPending_ = false;
                    if (!filter_->text().trimmed().isEmpty()) {
                        searchAborted_ = true;
                        emit searchStatusChanged(
                            tr("Search incomplete: %1. Refine the text or retry.")
                                .arg(text(e.error)));
                    }
                }
            }
        },
        Qt::DirectConnection);
    tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree, &QTreeView::customContextMenuRequested, this,
            [this, tree, proxy](const QPoint& point) {
                const auto index = proxy->mapToSource(tree->indexAt(point));
                if (!index.isValid())
                    return;
                QMenu menu(tree);
                populateContextMenu(&menu, index);
                design::execContextMenu(menu, tree->viewport()->mapToGlobal(point));
            });
}
void NavigatorController::refreshCurrent() {
    const auto source = proxy_->mapToSource(tree_->currentIndex());
    if (source.isValid())
        model_->refresh(source);
}
void NavigatorController::disconnectCurrent() {
    const auto source = proxy_->mapToSource(tree_->currentIndex());
    if (source.isValid() && source.data(NavigatorModel::KindRole).toString() == "connection")
        emit disconnectRequested(source.data(NavigatorModel::ConnectionRole).toULongLong());
}
void NavigatorController::populateContextMenu(QMenu* menu, const QModelIndex& sourceIndex) {
    if (!menu || !sourceIndex.isValid() || sourceIndex.model() != model_)
        return;
    const QPersistentModelIndex index(sourceIndex);
    if (index.data(NavigatorModel::KindRole).toString() == "load_more" ||
        index.data(NavigatorModel::HasMoreRole).toBool()) {
        auto* more = menu->addAction(tr("Load more objects"));
        more->setObjectName("loadMoreMetadata");
        connect(more, &QAction::triggered, this, [this, index] {
            if (index.isValid())
                model_->requestNextPage(index);
        });
        if (index.data(NavigatorModel::KindRole).toString() == "load_more")
            return;
    }
    if (index.data(NavigatorModel::KindRole).toString() == "connection") {
        auto* disconnect = menu->addAction(tr("Disconnect"));
        disconnect->setObjectName("disconnectSession");
        connect(disconnect, &QAction::triggered, this, [this, index] {
            if (index.isValid() && index.data(NavigatorModel::KindRole).toString() == "connection")
                emit disconnectRequested(index.data(NavigatorModel::ConnectionRole).toULongLong());
        });
        menu->addSeparator();
    }
    auto* refresh = menu->addAction(tr("Refresh"));
    connect(refresh, &QAction::triggered, this, [this, index] {
        if (index.isValid())
            model_->refresh(index);
    });
    auto* copy = menu->addAction(tr("Copy qualified name"));
    copy->setEnabled(!index.data(NavigatorModel::QualifiedNameRole).toString().isEmpty());
    connect(copy, &QAction::triggered, this, [index] {
        if (index.isValid())
            QApplication::clipboard()->setText(
                index.data(NavigatorModel::QualifiedNameRole).toString());
    });
    const auto objectKind = index.data(NavigatorModel::KindRole).toString();
    auto* ddl = menu->addAction(tr("Show DDL"));
    ddl->setEnabled(objectKind == "table" || objectKind == "view" || objectKind == "index" ||
                    objectKind == "sequence" || objectKind == "function");
    connect(ddl, &QAction::triggered, this, [this, index] {
        if (index.isValid())
            emit ddlRequested(index.data(NavigatorModel::ConnectionRole).toULongLong(),
                              index.data(NavigatorModel::ObjectIdRole).toString(),
                              index.data(Qt::DisplayRole).toString(),
                              index.data(NavigatorModel::KindRole).toString(),
                              index.data(NavigatorModel::PropertiesRole).toList());
    });
    if (objectKind != "table" && objectKind != "view")
        return;
    auto* generate = menu->addMenu(tr("Generate SQL"));
    const bool loaded = index.data(NavigatorModel::ChildrenLoadedRole).toBool();
    bool hasColumn = false;
    // The menu need only find one column to enable UPDATE; cap even this scan.
    const auto maximum = SqlTemplateService::limits();
    const auto scanLimit = maximum.maxColumns * 4 + 64;
    for (int row = 0; loaded && row < model_->rowCount(index) && quint64(row) < scanLimit; ++row)
        if (model_->index(row, 0, index).data(NavigatorModel::KindRole).toString() == "column") {
            hasColumn = true;
            break;
        }
    for (const auto& kind :
         {QString("select"), QString("insert"), QString("update"), QString("delete")}) {
        auto* action = generate->addAction(kind.toUpper());
        action->setObjectName("generate_" + kind);
        action->setEnabled(kind == "select" || kind == "delete" ||
                           (loaded && (kind == "insert" || hasColumn)));
        connect(action, &QAction::triggered, this, [this, index, kind] {
            if (!index.isValid() || index.model() != model_) {
                emit generationFailed(tr("The selected object is no longer available."));
                return;
            }
            const auto objectKind = index.data(NavigatorModel::KindRole).toString();
            if (objectKind != "table" && objectKind != "view") {
                emit generationFailed(tr("Select a table or view to generate SQL."));
                return;
            }
            QStringList columns;
            if (kind == "insert" || kind == "update") {
                if (!index.data(NavigatorModel::ChildrenLoadedRole).toBool()) {
                    emit generationFailed(
                        tr("Expand this object to load columns before generating SQL."));
                    return;
                }
                const auto limits = SqlTemplateService::limits();
                quint64 characters = 0;
                if (quint64(model_->rowCount(index)) > limits.maxColumns * 4 + 64) {
                    emit generationFailed(tr("Too many metadata objects to generate SQL."));
                    return;
                }
                for (int row = 0; row < model_->rowCount(index); ++row) {
                    const auto child = model_->index(row, 0, index);
                    if (child.data(NavigatorModel::KindRole).toString() != "column")
                        continue;
                    const auto name = child.data(Qt::DisplayRole).toString();
                    if (quint64(columns.size()) == limits.maxColumns ||
                        quint64(name.size()) > limits.maxBytes - characters) {
                        emit generationFailed(
                            tr("Column metadata exceeds the SQL template limits."));
                        return;
                    }
                    characters += quint64(name.size());
                    QString owned(name.constData(), name.size());
                    owned.squeeze();
                    columns.append(std::move(owned));
                }
            }
            const auto result = SqlTemplateService::generate(
                kind, index.data(NavigatorModel::QualifiedNameRole).toString(), columns);
            if (!result.valid) {
                emit generationFailed(result.error);
                return;
            }
            emit sqlGenerated(index.data(NavigatorModel::ConnectionRole).toULongLong(), result.sql);
        });
    }
    if (!loaded) {
        auto* hint = generate->addAction(tr("Expand this object to load columns."));
        hint->setEnabled(false);
    }
}
void NavigatorController::addConnection(quint64 connection, const QString& label) {
    model_->addConnection(connection, label);
}
void NavigatorController::setSelectedConnection(quint64 connection) {
    model_->removeConnection(std::numeric_limits<quint64>::max());
    static_cast<SelectedConnectionProxy*>(proxy_)->select(connection);
    ++searchGeneration_;
    searchRequests_ = 0;
    searchPending_ = false;
    searchAborted_ = false;
    if (!filter_->text().trimmed().isEmpty()) {
        emit searchStatusChanged(tr("Searching objects…"));
        advanceSearch(searchGeneration_);
    }
}
void NavigatorController::clearSelectedConnection() {
    model_->removeConnection(std::numeric_limits<quint64>::max());
    static_cast<SelectedConnectionProxy*>(proxy_)->clear();
    ++searchGeneration_;
    searchPending_ = false;
    searchAborted_ = false;
    emit searchStatusChanged({});
}
void NavigatorController::setPendingConnection(const QString& label) {
    constexpr auto pendingId = std::numeric_limits<quint64>::max();
    model_->removeConnection(pendingId);
    model_->addPendingConnection(pendingId, label);
    static_cast<SelectedConnectionProxy*>(proxy_)->select(pendingId);
    ++searchGeneration_;
    searchPending_ = false;
    searchAborted_ = false;
    emit searchStatusChanged({});
}
quint64 NavigatorController::selectedConnection() const {
    return static_cast<SelectedConnectionProxy*>(proxy_)->selected();
}
void NavigatorController::advanceSearch(quint64 generation) {
    if (generation != searchGeneration_ || searchPending_ || searchAborted_ ||
        filter_->text().trimmed().isEmpty())
        return;
    const auto connection = selectedConnection();
    if (!static_cast<SelectedConnectionProxy*>(proxy_)->hasSelection() ||
        connection == std::numeric_limits<quint64>::max())
        return;
    QModelIndex root;
    for (int row = 0; row < model_->rowCount(); ++row) {
        auto candidate = model_->index(row, 0);
        if (candidate.data(NavigatorModel::ConnectionRole).toULongLong() == connection) {
            root = candidate;
            break;
        }
    }
    if (!root.isValid())
        return;
    std::vector<QModelIndex> stack{root};
    std::vector<QModelIndex> matches;
    int visited = 0;
    while (!stack.empty()) {
        if (++visited > 20000) {
            emit searchStatusChanged(tr("Search limit reached. Refine the text."));
            return;
        }
        auto current = stack.back();
        stack.pop_back();
        const auto kind = current.data(NavigatorModel::KindRole).toString();
        if (current.data(Qt::DisplayRole).toString().contains(filter_->text(), Qt::CaseInsensitive))
            matches.push_back(current);
        if (kind != "connection" && kind != "database" && kind != "schema" && kind != "group")
            continue;
        if (model_->canFetchMore(current) || current.data(NavigatorModel::HasMoreRole).toBool()) {
            if (searchRequests_ >= 256) {
                emit searchStatusChanged(tr("Search limit reached. Refine the text."));
                return;
            }
            ++searchRequests_;
            searchPending_ = true;
            if (current.data(NavigatorModel::HasMoreRole).toBool())
                model_->requestNextPage(current);
            else
                model_->fetchMore(current);
            return;
        }
        for (int row = model_->rowCount(current) - 1; row >= 0; --row)
            stack.push_back(model_->index(row, 0, current));
    }
    for (const auto& match : matches) {
        for (auto ancestor = match.parent(); ancestor.isValid(); ancestor = ancestor.parent()) {
            const auto visible = proxy_->mapFromSource(ancestor);
            if (visible.isValid())
                tree_->expand(visible);
        }
    }
    emit searchStatusChanged({});
}
} // namespace choscordb
