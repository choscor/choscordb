#pragma once
#include "bridge/engine_adapter.h"
#include <QList>
#include <QString>
namespace choscordb {
enum class ShortcutScope { Window, ResultGrid };
struct ShortcutDescriptor {
    QString id, label, defaultSequence;
    bool configurable = true;
    ShortcutScope scope = ShortcutScope::Window;
};
struct ShortcutValidation {
    QString message;
    qsizetype command = -1;
    bool ok() const { return message.isEmpty(); }
};
ShortcutValidation validateShortcuts(const QList<ShortcutOverride>& shortcuts,
                                     const QList<ShortcutDescriptor>& catalog);
}
