#include "app/query_workspace.h"
#include "widgets/sql_file_dialog/sql_file_dialog.h"

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
}
