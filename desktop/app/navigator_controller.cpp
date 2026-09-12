#include "app/navigator_controller.h"
#include "bridge/engine_adapter.h"
#include "bridge/template_service.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "models/navigator_model.h"
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QMenu>
#include <QPersistentModelIndex>
#include <QPlainTextEdit>
#include <QSortFilterProxyModel>
#include <QTreeView>
#include <QVBoxLayout>
namespace choscordb {
namespace {
QString text(const rust::String& s) {
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}
} // namespace
NavigatorController::NavigatorController(EngineAdapter* engine, QTreeView* tree, QLineEdit* filter,
                                         QWidget* dialogParent)
    : QObject(tree), model_(new NavigatorModel(this)), engine_(engine) {
    auto* proxy = new QSortFilterProxyModel(this);
    proxy->setSourceModel(model_);
    proxy->setRecursiveFilteringEnabled(true);
    proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    tree->setModel(proxy);
    connect(filter, &QLineEdit::textChanged, proxy, &QSortFilterProxyModel::setFilterFixedString);
    connect(model_, &NavigatorModel::childrenRequested, engine, &EngineAdapter::loadMetadata);
    connect(engine, &EngineAdapter::metadataSubmissionFailed, this,
            [this](quint64 connection, const QString& parent, quint64 token, const QString& error) {
                model_->failChildren(connection, parent, token, error);
            });
    connect(
        engine, &EngineAdapter::eventReady, this,
        [this, dialogParent](const BridgeEvent& e) {
            const auto kind = text(e.kind);
            if (kind == "disconnected")
                model_->removeConnection(e.id);
            else if (kind == "metadata") {
                std::vector<NavigatorObject> objects;
                objects.reserve(e.objects.size());
                for (const auto& object : e.objects)
                    objects.push_back({text(object.id), text(object.name),
                                       text(object.qualified_name), text(object.kind),
                                       object.has_children});
                model_->applyChildren(e.id, text(e.parent), e.request_token, std::move(objects));
            } else if (kind == "metadata_failed")
                model_->failChildren(e.id, text(e.parent), e.request_token, text(e.error));
            else if (kind == "ddl") {
                auto* dialog = new QDialog(dialogParent);
                dialog->setAttribute(Qt::WA_DeleteOnClose);
                dialog->setWindowTitle(tr("Object DDL"));
                dialog->resize(700, 500);
                auto* layout = new QVBoxLayout(dialog);
                auto* editor = new QPlainTextEdit;
                editor->setReadOnly(true);
                editor->setPlainText(text(e.ddl));
                layout->addWidget(editor);
                auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
                connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
                layout->addWidget(buttons);
                dialog->show();
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
                menu.exec(tree->viewport()->mapToGlobal(point));
            });
}
void NavigatorController::populateContextMenu(QMenu* menu, const QModelIndex& sourceIndex) {
    if (!menu || !sourceIndex.isValid() || sourceIndex.model() != model_)
        return;
    const QPersistentModelIndex index(sourceIndex);
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
    ddl->setEnabled(objectKind == "table" || objectKind == "view" || objectKind == "index");
    connect(ddl, &QAction::triggered, this, [this, index] {
        if (index.isValid() && engine_)
            engine_->objectDdl(index.data(NavigatorModel::ConnectionRole).toULongLong(),
                               index.data(NavigatorModel::ObjectIdRole).toString());
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
} // namespace choscordb
