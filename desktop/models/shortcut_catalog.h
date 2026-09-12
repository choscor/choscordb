#pragma once
#include "bridge/engine_adapter.h"
#include <QList>
#include <QString>
namespace choscordb {
struct ShortcutDescriptor {
    QString id, label, defaultSequence;
    bool configurable = true;
};
QString shortcutValidationError(const EditorPreferences& preferences,
                                const QList<ShortcutDescriptor>& catalog);
} // namespace choscordb
