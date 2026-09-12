#include "completion_service.h"
#include "choscordb-bridge/src/lib.rs.h"
#include <algorithm>
namespace choscordb {
namespace {
// Count before allocation, rejecting malformed surrogate pairs as well as size.
bool chargeUtf8(const QString& text, quint64& remaining) {
    if (quint64(text.size()) > remaining)
        return false;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const auto value = text[i];
        quint64 bytes = value.unicode() < 0x80 ? 1 : value.unicode() < 0x800 ? 2 : 3;
        if (value.isHighSurrogate()) {
            if (i + 1 == text.size() || !text[i + 1].isLowSurrogate())
                return false;
            bytes = 4;
            ++i;
        } else if (value.isLowSurrogate())
            return false;
        if (bytes > remaining)
            return false;
        remaining -= bytes;
    }
    return true;
}
rust::String rustString(const QString& text) {
    const auto bytes = text.toUtf8();
    return rust::String(bytes.constData(), static_cast<size_t>(bytes.size()));
}
QString string(const rust::String& text) {
    auto result = QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
    result.squeeze();
    return result;
}
} // namespace
struct CompletionService::Private {
    explicit Private(rust::Box<CompletionCatalog> value) : catalog(std::move(value)) {}
    rust::Box<CompletionCatalog> catalog;
};
CompletionService::CompletionService(QList<CompletionCandidate> items, bool partial) {
    const auto maximum = limits();
    quint64 remaining = maximum.maxMetadataBytes;
    rust::Vec<SqlCompletionDto> metadata;
    metadata.reserve(
        static_cast<size_t>(std::min<quint64>(items.size(), maximum.maxMetadataEntries)));
    quint64 visited = 0;
    for (const auto& item : items) {
        if (visited++ == maximum.maxMetadataEntries) {
            partial = true;
            break;
        }
        auto budget = remaining;
        if (!chargeUtf8(item.label, budget) || !chargeUtf8(item.insertText, budget) ||
            !chargeUtf8(item.kind, budget)) {
            partial = true;
            break;
        }
        remaining = budget;
        SqlCompletionDto candidate;
        candidate.label = rustString(item.label);
        candidate.insert_text = rustString(item.insertText);
        candidate.kind = rustString(item.kind);
        metadata.push_back(std::move(candidate));
    }
    d_ = std::make_shared<const Private>(completion_catalog(std::move(metadata), partial));
}
CompletionLimits CompletionService::limits() {
    const auto value = completion_limits();
    return {value.max_results, value.max_prefix_bytes, value.max_metadata_entries,
            value.max_metadata_bytes, value.max_source_bytes};
}
CompletionPage CompletionService::complete(const QString& source, quint64 cursor,
                                           bool requested) const {
    if (!d_)
        return {};
    quint64 remaining = limits().maxSourceBytes;
    if (!chargeUtf8(source, remaining))
        return {};
    const auto bytes = source.toUtf8();
    if (cursor > quint64(bytes.size()))
        return {};
    const auto result =
        complete_sql(*d_->catalog, rust::Str(bytes.constData(), static_cast<size_t>(bytes.size())),
                     cursor, requested);
    CompletionPage page;
    page.valid = result.valid;
    page.partial = result.partial;
    page.start = result.start;
    page.end = result.end;
    page.items.reserve(static_cast<qsizetype>(result.items.size()));
    for (const auto& item : result.items)
        page.items.append({string(item.label), string(item.insert_text), string(item.kind)});
    return page;
}
} // namespace choscordb
