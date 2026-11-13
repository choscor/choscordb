#pragma once

#include <QString>

namespace choscordb {

struct ObjectActionStatement {
    bool valid = false;
    QString sql{};
    QString error{};
    QString newObjectId{};
    QString newQualifiedName{};
};

class ObjectActionSql {
  public:
    static ObjectActionStatement drop(const QString& driver, const QString& kind,
                                      const QString& objectId, const QString& qualifiedName,
                                      const QString& relationSubtype = {});
    static ObjectActionStatement rename(const QString& driver, const QString& kind,
                                        const QString& objectId, const QString& qualifiedName,
                                        const QString& newName,
                                        const QString& relationSubtype = {});
};

}
