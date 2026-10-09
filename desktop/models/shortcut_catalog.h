#pragma once
#include "bridge/engine_adapter.h"
#include <QList>
#include <QString>
namespace choscordb {
struct ShortcutDescriptor {
    QString id, label, defaultSequence;
    bool configurable = true;
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
