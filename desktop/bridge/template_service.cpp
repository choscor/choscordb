#include "template_service.h"
#include "bridge/rust_text.h"
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
using bridge_detail::fromRust;
using bridge_detail::toRust;
using bridge_detail::utf8View;
} // namespace
SqlTemplateLimits SqlTemplateService::limits() {
    auto value = sql_template_limits();
    return {value.max_bytes, value.max_columns};
}
SqlTemplateResult SqlTemplateService::generate(const QString& kind, const QString& qualified,
                                               const QStringList& columns) {
    const auto maximum = limits();
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
    for (const auto& column : columns)
        names.push_back(toRust(column));
    const auto result =
        generate_sql_template(utf8View(kindBytes), utf8View(nameBytes), std::move(names));
    if (!result.valid)
        return {false, {}, fromRust(result.error)};
    return {true, fromRust(result.sql), {}};
}
} // namespace choscordb
