#include "app/object_action_sql.h"
#include "bridge/object_action_dto.h"
#include "bridge/rust_text.h"

namespace choscordb {
namespace {

using bridge_detail::fromRust;
using bridge_detail::toRust;

bool validUnicode(std::initializer_list<const QString*> inputs) {
    for (const auto* input : inputs)
        if (QString::fromUtf8(input->toUtf8()) != *input)
            return false;
    return true;
}

const QString invalidInput = QStringLiteral("The object action input is not valid Unicode.");

}

ObjectActionStatement ObjectActionSql::prepare(const ObjectActionRequest& request) {
    if (!validUnicode({&request.action, &request.driver, &request.kind, &request.objectId,
                       &request.qualifiedName, &request.newName, &request.relationSubtype}))
        return {.error = invalidInput};
    const auto result = prepare_object_action(objectActionDto(request));
    QStringList statements;
    for (const auto& statement : result.statements)
        statements.append(fromRust(statement));
    return {.valid = result.valid,
            .sql = fromRust(result.sql),
            .statements = statements,
            .error = fromRust(result.error),
            .newObjectId = fromRust(result.new_object_id),
            .newQualifiedName = fromRust(result.new_qualified_name),
            .newName = result.valid ? request.newName : QString{}};
}

QString ObjectActionSql::unavailableReason(const QString& action, const QString& driver,
                                           const QString& kind, const QString& relationSubtype) {
    return fromRust(object_action_unavailable_reason(toRust(action), toRust(driver), toRust(kind),
                                                     toRust(relationSubtype)));
}

TruncateTraits ObjectActionSql::truncateTraits(const QString& driver) {
    const auto traits = truncate_traits_policy(toRust(driver));
    return {.cascade = traits.cascade, .restartIdentity = traits.restart_identity};
}

QList<ObjectScriptKind> ObjectActionSql::scripts() {
    QList<ObjectScriptKind> result;
    for (const auto& script : object_scripts())
        result.append({fromRust(script.code), fromRust(script.label)});
    return result;
}

QString ObjectActionSql::scriptUnavailableReason(const QString& script, const QString& driver,
                                                 const QString& kind,
                                                 const QString& relationSubtype) {
    return fromRust(object_script_unavailable_reason(toRust(script), toRust(driver), toRust(kind),
                                                     toRust(relationSubtype)));
}

ObjectScript ObjectActionSql::script(const QString& script, const QString& driver,
                                     const QString& kind, const QString& objectId,
                                     const QString& qualifiedName, const QString& relationSubtype) {
    if (!validUnicode({&script, &driver, &kind, &objectId, &qualifiedName, &relationSubtype}))
        return {.error = invalidInput};
    const auto result =
        object_script_policy(toRust(script), toRust(driver), toRust(kind), toRust(objectId),
                             toRust(qualifiedName), toRust(relationSubtype));
    return {.valid = result.valid,
            .fromDdl = result.from_ddl,
            .sql = fromRust(result.sql),
            .error = fromRust(result.error)};
}

}
