#pragma once

#include <QString>

namespace choscordb {

struct ObjectActionStatement {
    bool valid = false;
    QString sql;
    QString error;
    QString newObjectId;
    QString newQualifiedName;
};

class ObjectActionSql {
  public:
    // qualifiedName must come from navigator metadata. PostgreSQL object IDs are opaque OIDs,
    // so its quoted schema and name are decoded from that trusted metadata field.
    static ObjectActionStatement drop(const QString& driver, const QString& kind,
                                      const QString& objectId, const QString& qualifiedName,
                                      const QString& relationSubtype = {});
    static ObjectActionStatement rename(const QString& driver, const QString& kind,
                                        const QString& objectId, const QString& qualifiedName,
                                        const QString& newName,
                                        const QString& relationSubtype = {});
};

} // namespace choscordb
