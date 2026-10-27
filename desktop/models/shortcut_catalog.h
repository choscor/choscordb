#pragma once
#include "bridge/engine_adapter.h"
#include <QList>
#include <QString>
namespace choscordb {
// Two scopes can use one key: Qt routes a grid key to the focused grid only.
enum class ShortcutScope { Window, ResultGrid };
struct ShortcutDescriptor {
    QString id, label, defaultSequence;
    bool configurable = true;
    ShortcutScope scope = ShortcutScope::Window;
};
// Key sequences are Qt key-binding integration: Rust validates the stored
// preference document, and this check covers parsing and conflicts between
// the native actions in the catalog.
struct ShortcutValidation {
    QString message;
    qsizetype command = -1; // catalog index of the override at fault, if any
    bool ok() const { return message.isEmpty(); }
};
ShortcutValidation validateShortcuts(const QList<ShortcutOverride>& shortcuts,
                                     const QList<ShortcutDescriptor>& catalog);
} // namespace choscordb
