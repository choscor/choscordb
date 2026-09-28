#include "app/object_action_sql.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <optional>
#include <utility>

namespace choscordb {
namespace {

constexpr qsizetype kMaximumNameSize = 1024 * 1024;

bool validName(const QString& name, const QString& driver, bool newName = false) {
    if (name.isEmpty() || (newName && name.trimmed().isEmpty()) || name.size() > kMaximumNameSize ||
        name.contains(QChar::Null))
        return false;
    for (qsizetype i = 0; i < name.size(); ++i) {
        const auto character = name.at(i);
        if (character.isHighSurrogate()) {
            if (driver == QStringLiteral("mysql") || i + 1 == name.size() ||
                !name.at(++i).isLowSurrogate())
                return false;
        } else if (character.isLowSurrogate()) {
            return false;
        }
    }
    if (driver == QStringLiteral("mysql"))
        return name.size() <= 64 && !name.endsWith(QLatin1Char(' '));
    if (driver == QStringLiteral("postgres"))
        return name.toUtf8().size() <= 63;
    if (newName && driver == QStringLiteral("sqlite") &&
        name.startsWith(QStringLiteral("sqlite_"), Qt::CaseInsensitive))
        return false;
    return true;
}

QString quoted(const QString& name, QChar delimiter) {
    QString escaped = name;
    escaped.replace(delimiter, QString(2, delimiter));
    QString result;
    result.reserve(escaped.size() + 2);
    result += delimiter;
    result += escaped;
    result += delimiter;
    return result;
}

std::optional<QString> quotedPart(const QString& input, qsizetype& position) {
    if (position >= input.size() || input.at(position) != QLatin1Char('"'))
        return std::nullopt;
    ++position;
    QString part;
    while (position < input.size()) {
        const auto character = input.at(position++);
        if (character != QLatin1Char('"')) {
            part += character;
            continue;
        }
        if (position < input.size() && input.at(position) == QLatin1Char('"')) {
            part += QLatin1Char('"');
            ++position;
            continue;
        }
        return part;
    }
    return std::nullopt;
}

std::optional<std::pair<QString, QString>> identity(const QString& driver, const QString& objectId,
                                                    const QString& qualifiedName) {
    if (objectId.size() > kMaximumNameSize * 2 || qualifiedName.size() > kMaximumNameSize * 2)
        return std::nullopt;
    if (driver == QStringLiteral("postgres")) {
        const auto idParts = objectId.split(QLatin1Char(':'));
        if (idParts.size() != 3 || idParts[0] != QStringLiteral("pg") ||
            idParts[1] != QStringLiteral("relation"))
            return std::nullopt;
        bool integer = false;
        const auto oid = idParts[2].toUInt(&integer);
        if (!integer || oid == 0 || idParts[2] != QString::number(oid))
            return std::nullopt;
        qsizetype position = 0;
        const auto schema = quotedPart(qualifiedName, position);
        if (!schema || position >= qualifiedName.size() ||
            qualifiedName.at(position++) != QLatin1Char('.'))
            return std::nullopt;
        const auto name = quotedPart(qualifiedName, position);
        if (!name || position != qualifiedName.size() || !validName(*schema, driver) ||
            !validName(*name, driver))
            return std::nullopt;
        return std::pair{*schema, *name};
    }
    if (driver != QStringLiteral("sqlite") && driver != QStringLiteral("mysql"))
        return std::nullopt;
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(objectId.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray())
        return std::nullopt;
    const auto parts = document.array();
    if (parts.size() != 2 || !parts.at(0).isString() || !parts.at(1).isString())
        return std::nullopt;
    const auto schema = parts.at(0).toString();
    const auto name = parts.at(1).toString();
    if (!validName(schema, driver) || !validName(name, driver))
        return std::nullopt;
    return std::pair{schema, name};
}

ObjectActionStatement prepare(const QString& driver, const QString& kind, const QString& objectId,
                              const QString& qualifiedName, const std::optional<QString>& newName,
                              const QString& relationSubtype) {
    if (kind != QStringLiteral("table") && kind != QStringLiteral("view"))
        return {.error = QStringLiteral("Only tables and views support this action.")};
    if (!relationSubtype.isEmpty() && !(driver == QStringLiteral("postgres") &&
                                        ((relationSubtype == QStringLiteral("materialized_view") &&
                                          kind == QStringLiteral("view")) ||
                                         (relationSubtype == QStringLiteral("foreign_table") &&
                                          kind == QStringLiteral("table")))))
        return {.error = QStringLiteral("The selected relation type is not supported.")};
    if (newName && driver == QStringLiteral("sqlite") && kind == QStringLiteral("view"))
        return {.error = QStringLiteral("SQLite does not support renaming views directly.")};
    const auto parts = identity(driver, objectId, qualifiedName);
    if (!parts)
        return {.error = QStringLiteral("The selected object has an invalid identity.")};
    const auto& [schema, oldName] = *parts;
    const auto delimiter = driver == QStringLiteral("mysql") ? QLatin1Char('`') : QLatin1Char('"');
    const auto original = quoted(schema, delimiter) + QLatin1Char('.') + quoted(oldName, delimiter);
    QString keyword =
        kind == QStringLiteral("table") ? QStringLiteral("TABLE") : QStringLiteral("VIEW");
    if (relationSubtype == QStringLiteral("materialized_view"))
        keyword = QStringLiteral("MATERIALIZED VIEW");
    else if (relationSubtype == QStringLiteral("foreign_table"))
        keyword = QStringLiteral("FOREIGN TABLE");
    if (!newName)
        return {.valid = true,
                .sql = QStringLiteral("DROP ") + keyword + QLatin1Char(' ') + original +
                       QLatin1Char(';'),
                .newQualifiedName = original};
    if (!validName(*newName, driver, true))
        return {.error = QStringLiteral("Enter a valid unqualified object name.")};
    if (*newName == oldName)
        return {.error = QStringLiteral("Enter a different object name.")};
    const auto target = quoted(schema, delimiter) + QLatin1Char('.') + quoted(*newName, delimiter);
    QString sql;
    if (driver == QStringLiteral("mysql"))
        sql = QStringLiteral("RENAME TABLE ") + original + QStringLiteral(" TO ") + target +
              QLatin1Char(';');
    else
        sql = QStringLiteral("ALTER ") + keyword + QLatin1Char(' ') + original +
              QStringLiteral(" RENAME TO ") + quoted(*newName, delimiter) + QLatin1Char(';');
    QString renamedId = objectId;
    if (driver != QStringLiteral("postgres"))
        renamedId = QString::fromUtf8(
            QJsonDocument(QJsonArray{schema, *newName}).toJson(QJsonDocument::Compact));
    return {.valid = true, .sql = sql, .newObjectId = renamedId, .newQualifiedName = target};
}

} // namespace

ObjectActionStatement ObjectActionSql::drop(const QString& driver, const QString& kind,
                                            const QString& objectId, const QString& qualifiedName,
                                            const QString& relationSubtype) {
    return prepare(driver, kind, objectId, qualifiedName, std::nullopt, relationSubtype);
}

ObjectActionStatement ObjectActionSql::rename(const QString& driver, const QString& kind,
                                              const QString& objectId, const QString& qualifiedName,
                                              const QString& newName,
                                              const QString& relationSubtype) {
    return prepare(driver, kind, objectId, qualifiedName, newName, relationSubtype);
}

} // namespace choscordb
