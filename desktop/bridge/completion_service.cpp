#include "completion_service.h"
#include "bridge/rust_text.h"
namespace choscordb {
namespace {
using bridge_detail::toRust;
using bridge_detail::utf8View;
QString compactText(const rust::String& text) {
    auto result = bridge_detail::fromRust(text);
    result.squeeze();
    return result;
}
} // namespace
struct CompletionService::Private {
    explicit Private(rust::Box<CompletionCatalog> value) : catalog(std::move(value)) {}
    rust::Box<CompletionCatalog> catalog;
};
CompletionService::CompletionService(QList<CompletionCandidate> items, bool partial) {
    // Rust bounds the catalog; conversion skips text that is not well-formed UTF-16.
    rust::Vec<SqlCompletionDto> metadata;
    metadata.reserve(static_cast<size_t>(items.size()));
    for (const auto& item : items) {
        if (!item.label.isValidUtf16() || !item.insertText.isValidUtf16() ||
            !item.kind.isValidUtf16()) {
            partial = true;
            continue;
        }
        SqlCompletionDto candidate;
        candidate.label = toRust(item.label);
        candidate.insert_text = toRust(item.insertText);
        candidate.kind = toRust(item.kind);
        metadata.push_back(std::move(candidate));
    }
    d_ = std::make_shared<const Private>(completion_catalog(std::move(metadata), partial));
}
bool CompletionService::sourceSupported(quint64 bytes) {
    return completion_source_supported(bytes);
}
CompletionLimits CompletionService::limits() {
    const auto value = completion_limits();
    return {value.max_metadata_entries, value.max_metadata_bytes, value.max_metadata_visits};
}
CompletionPage CompletionService::complete(const QString& source, quint64 cursor,
                                           bool requested) const {
    if (!d_)
        return {};
    if (!source.isValidUtf16())
        return {};
    const auto bytes = source.toUtf8();
    if (cursor > quint64(bytes.size()))
        return {};
    const auto result = complete_sql(*d_->catalog, utf8View(bytes), cursor, requested);
    CompletionPage page;
    page.valid = result.valid;
    page.partial = result.partial;
    page.start = result.start;
    page.end = result.end;
    page.items.reserve(static_cast<qsizetype>(result.items.size()));
    for (const auto& item : result.items)
        page.items.append(
            {compactText(item.label), compactText(item.insert_text), compactText(item.kind)});
    return page;
}
} // namespace choscordb
