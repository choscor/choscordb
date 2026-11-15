#include "app/main_window.h"
#include "app/query_workspace.h"
#include "bridge/request_token.h"
#include "bridge/text_filter.h"
#include "design_system/quick_search/quick_search_dialog.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QTabWidget>

namespace choscordb {

void MainWindow::switchDatabase() {
    if (!databasePicker_) {
        databasePicker_ = new design::QuickSearchDialog(this);
        databasePicker_->setObjectName("databasePicker");
        connect(databasePicker_, &design::QuickSearchDialog::queryChanged, this,
                [this] { showDatabasePicker(databaseChoices_); });
        connect(databasePicker_, &design::QuickSearchDialog::activated, this,
                &MainWindow::openDatabase);
        connect(workspace_->adapter(), &EngineAdapter::databasesReady, this,
                [this](quint64 connection, quint64 token, const DatabaseList& list) {
                    if (token != databaseListToken_ || connection != databaseListConnection_)
                        return;
                    databaseListToken_ = 0;
                    databaseChoices_ = list;
                    showDatabasePicker(list);
                    databasePicker_->openSearch();
                });
        connect(workspace_->adapter(), &EngineAdapter::databasesFailed, this,
                [this](quint64 connection, quint64 token, const QString& error) {
                    if (token != databaseListToken_ || connection != databaseListConnection_)
                        return;
                    databaseListToken_ = 0;
                    reportError(tr("Could not list the databases: %1").arg(error));
                });
        connect(workspace_, &QueryWorkspace::connectionReady, this, [this](quint64 connection) {
            if (connection != databaseSwitchConnection_)
                return;
            databaseSwitchConnection_.reset();
            if (const auto editor = std::exchange(databaseSwitchEditor_, nullptr))
                workspace_->changeDocumentConnection(editor, connection);
        });
    }
    const auto connection = workspace_->selectedConnection();
    if (!connection || !workspace_->connectionSource(*connection)) {
        reportNotice(tr("Connect this tab to a saved connection or a SQLite file before you "
                        "switch databases."));
        return;
    }
    databaseListConnection_ = *connection;
    databaseListToken_ = nextRequestToken();
    if (!workspace_->adapter()->listDatabases(*connection, databaseListToken_))
        databaseListToken_ = 0;
}

void MainWindow::showDatabasePicker(const DatabaseList& list) {
    const TextFilter filter(databasePicker_->query().trimmed());
    QList<design::QuickSearchResult> rows;
    for (const auto& choice : list.choices)
        if (filter.matches(choice.label))
            rows.append({tr("Database"), choice.label,
                         choice.code == list.current ? tr("Current") : QString{}, choice.code,
                         design::Icon::Database});
    databasePicker_->setResults(rows);
    databasePicker_->setStatus(
        rows.isEmpty() ? list.choices.isEmpty() ? tr("No databases.") : tr("No matching databases.")
                       : QString{});
}

void MainWindow::openDatabase(const QString& target) {
    databasePicker_->close();
    const auto source = workspace_->connectionSource(databaseListConnection_);
    if (!source) {
        reportError(tr("The connection closed before the database switch."));
        return;
    }
    if (target == databaseChoices_.current)
        return;
    const auto switched = EngineAdapter::profileWithDatabase(*source, target);
    if (!switched.valid) {
        reportError(switched.error);
        return;
    }
    databaseSwitchEditor_ = qobject_cast<SqlEditor*>(editors_->currentWidget());
    databaseSwitchConnection_ = switched.profile.id.isEmpty()
                                    ? workspace_->connectSqlite(switched.profile.path)
                                    : workspace_->connectSavedProfile(switched.profile);
}

}
