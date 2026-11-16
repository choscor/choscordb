#include "app/query_workspace.h"
#include "bridge/engine_adapter.h"
#include "bridge/request_token.h"
#include "bridge/rust_text.h"
#include "choscordb-bridge/src/lib.rs.h"

namespace choscordb {
using bridge_detail::fromRust;
bool QueryWorkspace::switchDatabase(quint64 connection, const SavedProfile& profile) {
    if (stopping_ || workInFlight()) {
        message(tr("Finish or cancel active database work before you switch the database."));
        return false;
    }
    if (queryConnection_ == connection && !resolvePendingEdits())
        return false;
    const auto token = nextRequestToken();
    if (!adapter_->switchDatabase(connection, profile, token))
        return false;
    databaseSwitches_.insert(connection, {token, profile});
    return true;
}

void QueryWorkspace::setTransactionActive(quint64 connection, bool active) {
    if (active)
        pendingTransactions_.insert(connection);
    else
        pendingTransactions_.remove(connection);
    emit transactionStateChanged(connection, active);
}

void QueryWorkspace::handleDatabaseSwitchEvent(const BridgeEvent& e) {
    const auto kind = fromRust(e.kind);
    if (kind != "database_switched" && kind != "database_switch_failed")
        return;
    const auto pending = databaseSwitches_.constFind(e.id);
    if (pending == databaseSwitches_.cend() || pending->token != e.request_token)
        return;
    const auto profile = pending->profile;
    databaseSwitches_.erase(pending);
    if (kind == "database_switch_failed") {
        message(tr("Could not switch the database: %1").arg(fromRust(e.error)));
        return;
    }
    connectionSources_.insert(e.id, profile);
    if (profile.id.isEmpty())
        for (auto& live : liveConnections_)
            if (live.id == e.id)
                live.label = profile.path;
    if (e.has_transaction_state)
        setTransactionActive(e.id, e.transaction_active);
    if (queryConnection_ == e.id)
        invalidateResult();
    for (const auto& result : statementResults_)
        if (result.connection == e.id)
            adapter_->releaseQuery(result.query);
    dropStatementResults(e.id);
    for (const auto& notice : e.warnings)
        emit connectionNotice(fromRust(notice));
    documentChanged();
    emit databaseSwitched(e.id);
    updateActions();
}
}
