#pragma once
#include "bridge/sql_plan.h"

namespace choscordb {
struct DatabaseActionKindInfo {
    QString code, label;
    bool existing = false, newName = false, settings = false, confirmation = false;
};
struct DatabaseAction {
    QString driver, kind, current, name, newName, encoding, collation, confirmation;
};
}
