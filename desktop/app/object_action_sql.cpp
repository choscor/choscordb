#include "app/object_action_sql.h"
#include "choscordb-bridge/src/lib.rs.h"

#include <QByteArray>
#include <array>

namespace choscordb {
namespace {

QString text(const rust::String& value) {
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

ObjectActionStatement prepare(const QString& driver, const QString& kind, const QString& objectId,
                              const QString& qualifiedName, const QString& newName,
                              const QString& relationSubtype, bool rename) {
    const std::array<QString, 6> inputs{driver,        kind,    objectId,
                                        qualifiedName, newName, relationSubtype};
    std::array<QByteArray, 6> bytes;
    for (size_t i = 0; i < inputs.size(); ++i) {
        bytes[i] = inputs[i].toUtf8();
        if (QString::fromUtf8(bytes[i]) != inputs[i])
            return {.error = QStringLiteral("The object action input is not valid Unicode.")};
    }
    const auto part = [&bytes](size_t index) {
        return rust::Str(bytes[index].constData(), static_cast<size_t>(bytes[index].size()));
    };
    const auto result =
        prepare_object_action(part(0), part(1), part(2), part(3), part(4), part(5), rename);
    return {.valid = result.valid,
            .sql = text(result.sql),
            .error = text(result.error),
            .newObjectId = text(result.new_object_id),
            .newQualifiedName = text(result.new_qualified_name)};
}

} // namespace

ObjectActionStatement ObjectActionSql::drop(const QString& driver, const QString& kind,
                                            const QString& objectId, const QString& qualifiedName,
                                            const QString& relationSubtype) {
    return prepare(driver, kind, objectId, qualifiedName, {}, relationSubtype, false);
}

ObjectActionStatement ObjectActionSql::rename(const QString& driver, const QString& kind,
                                              const QString& objectId, const QString& qualifiedName,
                                              const QString& newName,
                                              const QString& relationSubtype) {
    return prepare(driver, kind, objectId, qualifiedName, newName, relationSubtype, true);
}

} // namespace choscordb
