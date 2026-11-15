#include "app/query_workspace.h"
#include "widgets/backup_dialog/backup_dialog.h"
#include "widgets/sql_file_dialog/sql_file_dialog.h"
#include <QPushButton>

namespace choscordb {
void QueryWorkspace::runSqlFile() {
    const auto connection = selectedConnection();
    if (!connection || !connectionAvailable(*connection)) {
        message(tr("Connect to a database to run a SQL file."));
        return;
    }
    const auto source = connectionSource(*connection);
    auto* dialog = new SqlFileDialog(adapter_,
                                     {*connection, driverForConnection(*connection),
                                      connectionSqlModes_.value(*connection),
                                      connectionTags_.value(*connection).safety,
                                      source ? source->name : driverForConnection(*connection)},
                                     widgets_.dialogParent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->open();
}
void QueryWorkspace::backupDatabase(bool restore) {
    const auto connection = selectedConnection();
    if (!connection || !connectionAvailable(*connection)) {
        message(restore ? tr("Connect to a database to restore a backup.")
                        : tr("Connect to a database to back it up."));
        return;
    }
    const auto source = connectionSource(*connection);
    auto* dialog = new BackupDialog(
        adapter_,
        {*connection, driverForConnection(*connection), connectionTags_.value(*connection).safety,
         source ? source->name : driverForConnection(*connection), restore},
        widgets_.dialogParent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->open();
}
void QueryWorkspace::stepResultPage(bool next) {
    if (auto* button = next ? widgets_.nextPage : widgets_.previousPage)
        button->click();
}
void QueryWorkspace::exportResult() {
    if (widgets_.exportResult)
        widgets_.exportResult->click();
}
}
