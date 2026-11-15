#include "app/query_workspace.h"
#include "bridge/request_token.h"
#include "design_system/menu/menu.h"
#include "models/result_table_model.h"

#include <QAction>
#include <QHeaderView>
#include <QMenu>
#include <QSet>
#include <QTableView>

namespace choscordb {
void QueryWorkspace::setupColumnChooser() {
    connect(model_, &QAbstractItemModel::modelReset, this, [this] {
        applyHiddenColumns();
        updateGridActions();
    });
    connect(adapter_, &EngineAdapter::hiddenColumnsReady, this,
            [this](quint64 token, const QString& context, const QStringList& hidden) {
                if (token != hiddenColumnsToken_ || context != columnContext_)
                    return;
                hiddenColumns_ = hidden;
                applyHiddenColumns();
            });
    connect(adapter_, &EngineAdapter::recoveryFailed, this,
            [this](quint64 token, const QString& error) {
                if (token == hiddenColumnsToken_)
                    message(tr("Hidden columns: %1").arg(error));
            });
}
void QueryWorkspace::setColumnContext(const QString& context) {
    if (context == columnContext_)
        return;
    columnContext_ = context;
    hiddenColumns_.clear();
    hiddenColumnsToken_ = 0;
    applyHiddenColumns();
    if (context.isEmpty())
        return;
    hiddenColumnsToken_ = nextRequestToken();
    adapter_->getHiddenColumns(context, hiddenColumnsToken_);
}
QStringList QueryWorkspace::resultColumnNames() const {
    QStringList names;
    names.reserve(model_->columnCount());
    for (int column = 0; column < model_->columnCount(); ++column)
        names.append(model_->headerData(column, Qt::Horizontal, ResultTableModel::HeaderNameRole)
                         .toString());
    return names;
}
void QueryWorkspace::applyHiddenColumns() {
    auto* header = widgets_.grid->horizontalHeader();
    const QSet<QString> hidden(hiddenColumns_.cbegin(), hiddenColumns_.cend());
    const auto names = resultColumnNames();
    for (int column = 0; column < names.size(); ++column)
        header->setSectionHidden(column, hidden.contains(names[column]));
}
void QueryWorkspace::setColumnHidden(const QString& name, bool hide) {
    const auto next =
        EngineAdapter::toggleHiddenColumn(hiddenColumns_, name, hide, resultColumnNames());
    if (!next.error.isEmpty()) {
        message(next.error);
        return;
    }
    storeHiddenColumns(next.hidden);
}
void QueryWorkspace::storeHiddenColumns(const QStringList& hidden) {
    hiddenColumns_ = hidden;
    applyHiddenColumns();
    if (columnContext_.isEmpty())
        return;
    hiddenColumnsToken_ = nextRequestToken();
    adapter_->setHiddenColumns(columnContext_, hiddenColumns_, hiddenColumnsToken_);
}
void QueryWorkspace::populateColumnMenu(QMenu& menu) {
    const QSet<QString> hidden(hiddenColumns_.cbegin(), hiddenColumns_.cend());
    QSet<QString> listed;
    for (const auto& name : resultColumnNames()) {
        if (listed.contains(name))
            continue;
        listed.insert(name);
        auto* action = menu.addAction(name);
        action->setCheckable(true);
        action->setChecked(!hidden.contains(name));
        connect(action, &QAction::toggled, this,
                [this, name](bool shown) { setColumnHidden(name, !shown); });
    }
    menu.addSeparator();
    auto* showAll = menu.addAction(tr("Show all columns"));
    showAll->setEnabled(!hiddenColumns_.isEmpty());
    connect(showAll, &QAction::triggered, this, [this] { storeHiddenColumns({}); });
}
void QueryWorkspace::showColumnChooser() {
    QMenu menu(widgets_.grid);
    menu.setObjectName("resultColumnChooser");
    populateColumnMenu(menu);
    auto* header = widgets_.grid->horizontalHeader();
    design::execContextMenu(menu, header->mapToGlobal(QPoint(0, header->height())));
}
}
