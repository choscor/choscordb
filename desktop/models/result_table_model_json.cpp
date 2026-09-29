#include "models/result_table_model.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace choscordb {
namespace {
constexpr qsizetype JsonViewBytes = 16 * 1024 * 1024;
constexpr qsizetype JsonViewLimit = JsonViewBytes / sizeof(QChar);
QString outputLimitMessage(qsizetype limit) {
    return QObject::tr("The JSON output exceeds %1 bytes of display storage (16 MiB maximum).")
        .arg(limit * sizeof(QChar));
}
QString encodedOutputLimitMessage() {
    return QObject::tr("The JSON output exceeds 16 MiB of encoded text.");
}
bool appendJson(QString& output, QStringView part, qsizetype limit) {
    if (part.size() > limit - output.size())
        return false;
    output += part;
    return true;
}

bool appendJsonString(QString& output, QStringView value, qsizetype limit) {
    if (!appendJson(output, QStringLiteral("\""), limit))
        return false;
    constexpr char16_t hex[] = u"0123456789abcdef";
    qsizetype spanStart = 0;
    for (qsizetype i = 0; i < value.size(); ++i) {
        const auto character = value[i].unicode();
        if (QChar::isHighSurrogate(character)) {
            if (i + 1 >= value.size() || !QChar::isLowSurrogate(value[i + 1].unicode()))
                return false;
            ++i;
        } else if (QChar::isLowSurrogate(character)) {
            return false;
        } else if (character == '"' || character == '\\') {
            if (!appendJson(output, value.mid(spanStart, i - spanStart), limit) ||
                !appendJson(output, QStringLiteral("\\"), limit) ||
                !appendJson(output, value.mid(i, 1), limit))
                return false;
            spanStart = i + 1;
        } else if (character < 0x20) {
            const char16_t escaped[] = {
                u'\\', u'u', u'0', u'0', hex[character >> 4], hex[character & 0xf]};
            if (!appendJson(output, value.mid(spanStart, i - spanStart), limit) ||
                !appendJson(output, QStringView(escaped, 6), limit))
                return false;
            spanStart = i + 1;
        }
    }
    return appendJson(output, value.mid(spanStart), limit) &&
           appendJson(output, QStringLiteral("\""), limit);
}

bool isJsonType(const QString& type) {
    const auto normalized = type.trimmed();
    return normalized.compare(QStringLiteral("json"), Qt::CaseInsensitive) == 0 ||
           normalized.compare(QStringLiteral("jsonb"), Qt::CaseInsensitive) == 0;
}

bool isTextType(const QString& type) {
    const auto normalized = type.trimmed().toLower();
    if (normalized.isEmpty())
        return true;
    for (QStringView prefix :
         {QStringView{u"text"}, QStringView{u"varchar"}, QStringView{u"character varying"},
          QStringView{u"char"}, QStringView{u"character"}, QStringView{u"nchar"},
          QStringView{u"nvarchar"}, QStringView{u"clob"}, QStringView{u"citext"},
          QStringView{u"string"}, QStringView{u"bpchar"}}) {
        if (normalized == prefix ||
            (normalized.startsWith(prefix) && normalized.size() > prefix.size() &&
             normalized.at(prefix.size()) == '('))
            return true;
    }
    return false;
}

bool isBinaryType(const QString& type) {
    const auto normalized = type.trimmed().toLower();
    return normalized == "blob" || normalized == "bytea" || normalized == "binary" ||
           normalized.startsWith("varbinary(") || normalized == "varbinary";
}

bool validUnicode(QStringView value);

bool validResolvedValue(const Cell& original, const Cell& resolved, const ResultColumn& column) {
    const auto* deferred = std::get_if<DeferredValue>(&original);
    if (!deferred)
        return false;
    const bool expectedText =
        isJsonType(column.databaseType) ||
        (!column.databaseType.trimmed().isEmpty() && isTextType(column.databaseType)) ||
        (!deferred->type.trimmed().isEmpty() && isTextType(deferred->type));
    const bool expectedBinary = isBinaryType(column.databaseType) || isBinaryType(deferred->type);
    if (expectedText == expectedBinary)
        return false;
    if (expectedText) {
        const auto* text = std::get_if<QString>(&resolved);
        if (!text || !validUnicode(*text))
            return false;
        return static_cast<quint64>(text->toUtf8().size()) == deferred->bytes;
    }
    const auto* bytes = std::get_if<QByteArray>(&resolved);
    return bytes && static_cast<quint64>(bytes->size()) == deferred->bytes;
}

bool validUnicode(QStringView value) {
    for (qsizetype i = 0; i < value.size(); ++i) {
        const auto character = value[i].unicode();
        if (QChar::isHighSurrogate(character)) {
            if (++i >= value.size() || !QChar::isLowSurrogate(value[i].unicode()))
                return false;
        } else if (QChar::isLowSurrogate(character)) {
            return false;
        }
    }
    return true;
}

bool validJsonDocument(QStringView source) {
    if (!validUnicode(source) || source.size() > JsonViewLimit)
        return false;
    const auto encoded = source.toString().toUtf8();
    if (encoded.size() > JsonViewBytes)
        return false;
    QByteArray wrapped;
    wrapped.reserve(encoded.size() + 2);
    wrapped += '[';
    wrapped += encoded;
    wrapped += ']';
    QJsonParseError parseError;
    const auto parsed = QJsonDocument::fromJson(wrapped, &parseError);
    return parseError.error == QJsonParseError::NoError && parsed.isArray() &&
           parsed.array().size() == 1;
}

bool appendPrettyJson(QString& output, QStringView source, qsizetype limit, int depth) {
    const auto indent = [&](int amount) {
        return appendJson(output, QString(amount * 2, QChar(' ')), limit);
    };
    for (qsizetype i = 0; i < source.size();) {
        const auto character = source[i];
        if (character.isSpace()) {
            ++i;
            continue;
        }
        if (character == '"') {
            const auto start = i++;
            while (i < source.size()) {
                if (source[i] == '\\') {
                    i += 2;
                } else if (source[i++] == '"') {
                    break;
                }
            }
            if (!appendJson(output, source.mid(start, i - start), limit))
                return false;
            continue;
        }
        if (character == '{' || character == '[') {
            if (!appendJson(output, source.mid(i, 1), limit))
                return false;
            qsizetype next = i + 1;
            while (next < source.size() && source[next].isSpace())
                ++next;
            if (next < source.size() && source[next] != (character == '{' ? '}' : ']')) {
                ++depth;
                if (!appendJson(output, QStringLiteral("\n"), limit) || !indent(depth))
                    return false;
            }
            ++i;
            continue;
        }
        if (character == '}' || character == ']') {
            qsizetype previous = i;
            while (previous > 0 && source[previous - 1].isSpace())
                --previous;
            if (previous > 0 && source[previous - 1] != (character == '}' ? '{' : '[')) {
                --depth;
                if (!appendJson(output, QStringLiteral("\n"), limit) || !indent(depth))
                    return false;
            }
            if (!appendJson(output, source.mid(i, 1), limit))
                return false;
            ++i;
            continue;
        }
        if (character == ',' || character == ':') {
            if (!appendJson(output, character == ',' ? QStringLiteral(",\n") : QStringLiteral(": "),
                            limit) ||
                (character == ',' && !indent(depth)))
                return false;
            ++i;
            continue;
        }
        const auto start = i;
        while (i < source.size() && !source[i].isSpace() && source[i] != ',' && source[i] != '}' &&
               source[i] != ']')
            ++i;
        if (!appendJson(output, source.mid(start, i - start), limit))
            return false;
    }
    return true;
}

bool appendJsonCell(QString& output, const Cell& cell, const ResultColumn& column, qsizetype limit,
                    int depth, QString* error) {
    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };
    if (std::holds_alternative<std::monostate>(cell))
        return appendJson(output, QStringLiteral("null"), limit);
    if (const auto* value = std::get_if<bool>(&cell))
        return appendJson(output, *value ? QStringLiteral("true") : QStringLiteral("false"), limit);
    if (const auto* value = std::get_if<qint64>(&cell))
        return appendJson(output, QString::number(*value), limit);
    if (const auto* value = std::get_if<double>(&cell)) {
        if (!std::isfinite(*value))
            return fail(
                QObject::tr("This row contains a non-finite number that JSON cannot represent."));
        return appendJson(output, QString::number(*value, 'g', 17), limit);
    }
    if (const auto* value = std::get_if<QString>(&cell)) {
        if (isJsonType(column.databaseType)) {
            if (!validJsonDocument(*value))
                return fail(QObject::tr(
                    "This JSON/JSONB value is invalid or exceeds the 16 MiB JSON limit."));
            if (!appendPrettyJson(output, *value, limit, depth))
                return fail(outputLimitMessage(limit));
            return true;
        }
        if (!appendJsonString(output, *value, limit))
            return fail(QObject::tr("This row contains invalid text or exceeds %1 bytes of JSON "
                                    "display storage (16 MiB maximum).")
                            .arg(limit * sizeof(QChar)));
        return true;
    }
    if (const auto* value = std::get_if<QByteArray>(&cell)) {
        if (value->size() > limit * 3 / 4)
            return fail(outputLimitMessage(limit));
        return appendJson(output, QStringLiteral("{\"$binary\": \""), limit) &&
               appendJson(output, QString::fromLatin1(value->toBase64()), limit) &&
               appendJson(output, QStringLiteral("\"}"), limit);
    }
    return fail(QObject::tr("Load every deferred value before viewing this row as JSON."));
}
} // namespace

bool ResultTableModel::rowJson(int row, QString* json, QString* error,
                               const std::map<int, Cell>& resolved) const {
    return rowJsonImpl(row, json, error, resolved, false, nullptr);
}

ResultTableModel::RowJsonReadiness ResultTableModel::rowJsonReadiness(int row,
                                                                      QString* error) const {
    QString json;
    bool unresolved = false;
    if (!rowJsonImpl(row, &json, error, {}, true, &unresolved))
        return RowJsonReadiness::Invalid;
    return unresolved ? RowJsonReadiness::NeedsDeferred : RowJsonReadiness::Ready;
}

bool ResultTableModel::rowJsonImpl(int row, QString* json, QString* error,
                                   const std::map<int, Cell>& resolved, bool allowDeferred,
                                   bool* unresolved) const {
    if (json)
        json->clear();
    if (error)
        error->clear();
    if (unresolved)
        *unresolved = false;
    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };
    if (!json || row < 0 || row >= rowCount())
        return fail(tr("This result row is no longer available."));
    const qsizetype limit =
        static_cast<qsizetype>(std::min(byteBudget_ / sizeof(QChar), std::size_t{JsonViewLimit}));
    for (const auto& [column, value] : resolved) {
        if (column < 0 || column >= columnCount() ||
            !std::holds_alternative<DeferredValue>(rows_[row][column]))
            return fail(tr("A loaded row value is invalid."));
        if (!validResolvedValue(rows_[row][column], value, columns_[column]))
            return fail(tr("A loaded row value is invalid or incomplete."));
    }
    QSet<QString> originalNames;
    for (const auto& column : columns_)
        if (!column.name.isEmpty())
            originalNames.insert(column.name);
    QSet<QString> usedNames;
    QString output;
    output.reserve(std::min<qsizetype>(limit, 4096));
    if (!appendJson(output, QStringLiteral("{"), limit))
        return fail(outputLimitMessage(limit));
    for (int column = 0; column < columnCount(); ++column) {
        if (!appendJson(output, column ? QStringLiteral(",\n  ") : QStringLiteral("\n  "), limit))
            return fail(outputLimitMessage(limit));
        const auto& originalName = columns_[column].name;
        QString key =
            originalName.isEmpty() ? QStringLiteral("column %1").arg(column + 1) : originalName;
        if (usedNames.contains(key) || (originalName.isEmpty() && originalNames.contains(key))) {
            const QString base = key;
            int suffix = 2;
            do {
                key = base + QStringLiteral(" (%1)").arg(suffix++);
            } while (usedNames.contains(key) || originalNames.contains(key));
        }
        usedNames.insert(key);
        if (!appendJsonString(output, key, limit) ||
            !appendJson(output, QStringLiteral(": "), limit))
            return fail(tr("This row has invalid column text or exceeds %1 bytes of JSON display "
                           "storage (16 MiB maximum).")
                            .arg(limit * sizeof(QChar)));
        if (inserted_[row] && !touched_[row][column]) {
            if (!appendJson(output, QStringLiteral("{\"$omitted\": true}"), limit))
                return fail(outputLimitMessage(limit));
            continue;
        }
        const auto found = resolved.find(column);
        const Cell& value = found == resolved.end() ? rows_[row][column] : found->second;
        if (allowDeferred && std::holds_alternative<DeferredValue>(value)) {
            if (unresolved)
                *unresolved = true;
            if (!appendJson(output, QStringLiteral("null"), limit))
                return fail(outputLimitMessage(limit));
            continue;
        }
        if (!appendJsonCell(output, value, columns_[column], limit, 1, error))
            return error && !error->isEmpty() ? false : fail(outputLimitMessage(limit));
    }
    if (!appendJson(output, columnCount() ? QStringLiteral("\n}") : QStringLiteral("}"), limit))
        return fail(outputLimitMessage(limit));
    if (output.toUtf8().size() > JsonViewBytes)
        return fail(encodedOutputLimitMessage());
    *json = std::move(output);
    return true;
}
ResultTableModel::CellJsonReadiness ResultTableModel::cellJsonReadiness(const QModelIndex& index,
                                                                        QString* error) const {
    if (error)
        error->clear();
    const auto value = cellValue(index);
    if (!value || std::holds_alternative<std::monostate>(*value))
        return CellJsonReadiness::Unavailable;
    const auto& column = columns_[index.column()];
    const bool jsonType = isJsonType(column.databaseType);
    const bool textType = isTextType(column.databaseType);
    if (!jsonType && !textType)
        return CellJsonReadiness::Unavailable;
    if (std::holds_alternative<DeferredValue>(*value))
        return CellJsonReadiness::NeedsDeferred;
    const auto* text = std::get_if<QString>(&*value);
    if (!text)
        return jsonType ? CellJsonReadiness::Invalid : CellJsonReadiness::Unavailable;
    if (validJsonDocument(*text))
        return CellJsonReadiness::Ready;
    if (jsonType) {
        if (error)
            *error = tr("This JSON/JSONB value is invalid or exceeds the 16 MiB JSON limit.");
        return CellJsonReadiness::Invalid;
    }
    return CellJsonReadiness::Unavailable;
}
bool ResultTableModel::cellJson(const QModelIndex& index, QString* json, QString* error,
                                const std::optional<Cell>& resolved) const {
    if (json)
        json->clear();
    if (error)
        error->clear();
    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };
    if (!json)
        return fail(tr("The JSON view is unavailable."));
    const auto original = cellValue(index);
    if (!original || std::holds_alternative<std::monostate>(*original))
        return fail(tr("This result cell is no longer available."));
    const auto& column = columns_[index.column()];
    if (!isJsonType(column.databaseType) && !isTextType(column.databaseType))
        return fail(tr("This cell is not a JSON or text value."));
    if (resolved && !validResolvedValue(*original, *resolved, column))
        return fail(tr("A loaded cell value is invalid or incomplete."));
    const Cell& value = resolved ? *resolved : *original;
    if (std::holds_alternative<DeferredValue>(value))
        return fail(tr("Load the complete deferred value before viewing this cell as JSON."));
    const auto* source = std::get_if<QString>(&value);
    if (!source || !validJsonDocument(*source))
        return fail(isJsonType(column.databaseType)
                        ? tr("This JSON/JSONB value is invalid or exceeds the 16 MiB JSON limit.")
                        : tr("This text is not valid JSON or exceeds the 16 MiB JSON limit."));
    QString output;
    const qsizetype limit =
        static_cast<qsizetype>(std::min(byteBudget_ / sizeof(QChar), std::size_t{JsonViewLimit}));
    if (!appendPrettyJson(output, *source, limit, 0))
        return fail(outputLimitMessage(limit));
    if (output.toUtf8().size() > JsonViewBytes)
        return fail(encodedOutputLimitMessage());
    *json = std::move(output);
    return true;
}
bool ResultTableModel::pageJson(QString* json, QString* error,
                                const std::map<std::pair<int, int>, Cell>& resolved) const {
    if (json)
        json->clear();
    if (error)
        error->clear();
    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };
    if (!json || rowCount() == 0)
        return fail(tr("This result page is empty or no longer available."));
    const qsizetype limit =
        static_cast<qsizetype>(std::min(byteBudget_ / sizeof(QChar), std::size_t{JsonViewLimit}));
    for (const auto& [position, value] : resolved) {
        Q_UNUSED(value);
        if (position.first < 0 || position.first >= rowCount() || position.second < 0 ||
            position.second >= columnCount())
            return fail(tr("A loaded page value is invalid."));
    }
    QString output;
    output.reserve(std::min<qsizetype>(limit, 4096));
    if (!appendJson(output, QStringLiteral("[\n"), limit))
        return fail(outputLimitMessage(limit));
    for (int row = 0; row < rowCount(); ++row) {
        std::map<int, Cell> rowResolved;
        auto loaded = resolved.lower_bound({row, 0});
        while (loaded != resolved.end() && loaded->first.first == row) {
            rowResolved.emplace(loaded->first.second, loaded->second);
            ++loaded;
        }
        QString rowOutput;
        if (!rowJson(row, &rowOutput, error, rowResolved))
            return error && !error->isEmpty()
                       ? false
                       : fail(tr("This page contains a row that cannot be shown as JSON."));
        if (row && !appendJson(output, QStringLiteral(",\n"), limit))
            return fail(outputLimitMessage(limit));
        if (!appendJson(output, QStringLiteral("  "), limit))
            return fail(outputLimitMessage(limit));
        qsizetype start = 0;
        for (qsizetype i = 0; i < rowOutput.size(); ++i) {
            if (rowOutput[i] != '\n')
                continue;
            if (!appendJson(output, QStringView(rowOutput).mid(start, i - start), limit) ||
                !appendJson(output, QStringLiteral("\n  "), limit))
                return fail(outputLimitMessage(limit));
            start = i + 1;
        }
        if (!appendJson(output, QStringView(rowOutput).mid(start), limit))
            return fail(outputLimitMessage(limit));
    }
    if (!appendJson(output, QStringLiteral("\n]"), limit))
        return fail(outputLimitMessage(limit));
    if (output.toUtf8().size() > JsonViewBytes)
        return fail(encodedOutputLimitMessage());
    *json = std::move(output);
    return true;
}
ResultTableModel::RowJsonReadiness ResultTableModel::pageJsonReadiness(QString* error) const {
    if (error)
        error->clear();
    if (rowCount() == 0) {
        if (error)
            *error = tr("This result page is empty or no longer available.");
        return RowJsonReadiness::Invalid;
    }
    bool needsDeferred = false;
    for (int row = 0; row < rowCount(); ++row) {
        const auto readiness = rowJsonReadiness(row, error);
        if (readiness == RowJsonReadiness::Invalid)
            return RowJsonReadiness::Invalid;
        needsDeferred |= readiness == RowJsonReadiness::NeedsDeferred;
    }
    return needsDeferred ? RowJsonReadiness::NeedsDeferred : RowJsonReadiness::Ready;
}
} // namespace choscordb
