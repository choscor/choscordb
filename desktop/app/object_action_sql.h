#pragma once

#include "bridge/object_action.h"
#include <QList>
#include <QString>
#include <QStringList>

namespace choscordb {

struct ObjectScript {
    bool valid = false, fromDdl = false;
    QString sql{}, error{};
};

struct ObjectScriptKind {
    QString code, label;
};

struct TruncateTraits {
    bool cascade = false, restartIdentity = false;
};

class ObjectActionSql {
  public:
    static ObjectActionStatement prepare(const ObjectActionRequest& request);
    static QString unavailableReason(const QString& action, const QString& driver,
                                     const QString& kind, const QString& relationSubtype);
    static TruncateTraits truncateTraits(const QString& driver);
    static QList<ObjectScriptKind> scripts();
    static QString scriptUnavailableReason(const QString& script, const QString& driver,
                                           const QString& kind, const QString& relationSubtype);
    static ObjectScript script(const QString& script, const QString& driver, const QString& kind,
                               const QString& objectId, const QString& qualifiedName,
                               const QString& relationSubtype);
};

}
