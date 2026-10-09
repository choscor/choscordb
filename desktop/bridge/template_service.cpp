#include "template_service.h"
#include "bridge/rust_text.h"
#include <algorithm>
namespace choscordb {
namespace {
using bridge_detail::fromRust;
using bridge_detail::toRust;
using bridge_detail::utf8View;
} // namespace
SqlTemplateResult SqlTemplateService::generate(const QString& kind, const QString& qualified,
                                               const QStringList& columns, bool columnsLoaded) {
    // Rust bounds the template input; conversion needs well-formed UTF-16.
    const auto valid = [](const QString& text) { return text.isValidUtf16(); };
    if (!valid(qualified) || !std::all_of(columns.begin(), columns.end(), valid))
        return {false, {}, QStringLiteral("SQL template input is not valid Unicode.")};
    const auto kindBytes = kind.toUtf8(), nameBytes = qualified.toUtf8();
    rust::Vec<rust::String> names;
    names.reserve(static_cast<size_t>(columns.size()));
    for (const auto& column : columns)
        names.push_back(toRust(column));
    const auto result = generate_sql_template(utf8View(kindBytes), utf8View(nameBytes),
                                              std::move(names), columnsLoaded);
    if (!result.valid)
        return {false, {}, fromRust(result.error)};
    return {true, fromRust(result.sql), {}};
}
QString SqlTemplateService::unavailableReason(const QString& kind, bool columnsLoaded,
                                              bool hasColumn) {
    const auto bytes = kind.toUtf8();
    return fromRust(sql_template_unavailable_reason(utf8View(bytes), columnsLoaded, hasColumn));
}
} // namespace choscordb
