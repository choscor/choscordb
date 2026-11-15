#pragma once

#include <QString>
#include <QStringList>

namespace choscordb {

struct ObjectActionRequest {
    QString action, driver, kind, objectId, qualifiedName, newName, relationSubtype;
    bool cascade = false, restartIdentity = false, withData = false;
};

struct ObjectActionStatement {
    bool valid = false;
    QString sql{};
    QStringList statements{};
    QString error{};
    QString newObjectId{};
    QString newQualifiedName{};
    QString newName{};
};

}
