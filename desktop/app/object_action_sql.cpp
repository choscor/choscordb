#include "app/object_action_sql.h"
#include "bridge/rust_text.h"

#include <QByteArray>
#include <array>

namespace choscordb {
namespace {

using bridge_detail::fromRust;
using bridge_detail::utf8View;

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
    const auto part = [&bytes](size_t index) { return utf8View(bytes[index]); };
    const auto result =
        prepare_object_action(part(0), part(1), part(2), part(3), part(4), part(5), rename);
    return {.valid = result.valid,
            .sql = fromRust(result.sql),
            .error = fromRust(result.error),
            .newObjectId = fromRust(result.new_object_id),
            .newQualifiedName = fromRust(result.new_qualified_name)};
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
