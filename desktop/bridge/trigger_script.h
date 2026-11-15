#pragma once
#include "bridge/sql_plan.h"

namespace choscordb {
struct TriggerScriptKind {
    QString code, label;
    bool needsTrigger = false;
};
struct TriggerScriptRequest {
    QString driver, kind, tableId, tableName, trigger, definition;
};
}
