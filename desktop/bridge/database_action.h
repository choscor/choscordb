#pragma once
#include <QString>

namespace choscordb {
struct DatabaseActionKindInfo {
    QString code, label;
    bool existing = false, newName = false, settings = false, confirmation = false;
};
struct DatabaseAction {
    QString driver, kind, current, name, newName, encoding, collation, confirmation;
};
struct DatabaseActionPlan {
    QString sql, error;
};
}
