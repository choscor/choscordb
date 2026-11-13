#pragma once
#include "app/object_data_workspace.h"
#include "app/object_explorer.h"
#include "app/query_workspace.h"
#include "bridge/engine_adapter.h"
#include <QLabel>

inline choscordb::QueryState summaryState(const QLabel* summary) {
    const bool objectData = summary && summary->objectName() == "objectDataSummary";
    for (auto* owner = summary ? summary->parent() : nullptr; owner; owner = owner->parent()) {
        if (objectData) {
            if (auto* data = qobject_cast<choscordb::ObjectDataWorkspace*>(owner))
                return data->results()->executionState();
            if (auto* data = owner->findChild<choscordb::ObjectDataWorkspace*>())
                return data->results()->executionState();
        } else if (auto* workspace =
                       owner->findChild<choscordb::QueryWorkspace*>(Qt::FindDirectChildrenOnly)) {
            return workspace->executionState();
        }
    }
    return choscordb::QueryState::Idle;
}

inline bool queryCompleted(choscordb::QueryState state) {
    return state == choscordb::QueryState::CompletedRows ||
           state == choscordb::QueryState::CompletedEmpty;
}

inline choscordb::PanelState objectPanelState(const QLabel* status) {
    for (auto* owner = status ? status->parent() : nullptr; owner; owner = owner->parent())
        if (auto* explorer = qobject_cast<choscordb::ObjectExplorer*>(owner))
            return explorer->panelState();
    return choscordb::PanelState::Idle;
}
