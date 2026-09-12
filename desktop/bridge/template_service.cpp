#include "template_service.h"
#include "choscordb-bridge/src/lib.rs.h"
namespace choscordb {
namespace {
bool charge(const QString& text, quint64& remaining) {
    if (quint64(text.size()) > remaining)
        return false;
    for (qsizetype i = 0; i < text.size(); ++i) {
        auto unit = text[i];
        quint64 bytes = unit.unicode() < 128 ? 1 : unit.unicode() < 2048 ? 2 : 3;
        if (unit.isHighSurrogate()) {
            if (i + 1 == text.size() || !text[i + 1].isLowSurrogate())
                return false;
            ++i;
            bytes = 4;
        } else if (unit.isLowSurrogate())
            return false;
        if (bytes > remaining)
            return false;
        remaining -= bytes;
    }
    return true;
}
QString text(const rust::String& value) {
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}
} // namespace
SqlTemplateLimits SqlTemplateService::limits() {
    auto value = sql_template_limits();
    return {value.max_bytes, value.max_columns};
}
SqlTemplateResult SqlTemplateService::generate(const QString& kind, const QString& qualified,
                                               const QStringList& columns) {
    const auto maximum = limits();
    if (kind != "select" && kind != "insert" && kind != "update" && kind != "delete")
        return {false, {}, QStringLiteral("Unknown SQL template.")};
    quint64 remaining = maximum.maxBytes;
    if (quint64(columns.size()) > maximum.maxColumns || !charge(qualified, remaining))
        return {false,
                {},
                QStringLiteral("SQL template input exceeds limits or is not valid Unicode.")};
    for (const auto& column : columns)
        if (!charge(column, remaining))
            return {false,
                    {},
                    QStringLiteral("SQL template input exceeds limits or is not valid Unicode.")};
    const auto kindBytes = kind.toUtf8(), nameBytes = qualified.toUtf8();
    rust::Vec<rust::String> names;
    names.reserve(static_cast<size_t>(columns.size()));
    for (const auto& column : columns) {
        const auto bytes = column.toUtf8();
        names.push_back(rust::String(bytes.constData(), static_cast<size_t>(bytes.size())));
    }
    const auto result = generate_sql_template(
        rust::Str(kindBytes.constData(), static_cast<size_t>(kindBytes.size())),
        rust::Str(nameBytes.constData(), static_cast<size_t>(nameBytes.size())), std::move(names));
    if (!result.valid)
        return {false, {}, text(result.error)};
    const QString explanation =
        (kind == "insert" || kind == "update") && !columns.isEmpty()
            ? QStringLiteral("-- Replace numbered placeholders with values before running.\n")
            : QString{};
    if (quint64(result.sql.size()) > maximum.maxBytes - quint64(explanation.size()))
        return {false, {}, QStringLiteral("Generated SQL exceeds the template size limit.")};
    return {true, explanation + text(result.sql), {}};
}
} // namespace choscordb
