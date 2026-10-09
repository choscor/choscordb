#include "bridge/quick_search.h"
#include "bridge/rust_text.h"

namespace choscordb {
using bridge_detail::toRust;
using bridge_detail::utf8View;

namespace {
QuickSearchDestinationKind destinationKind(QuickSearchDestination::Kind kind) {
    switch (kind) {
    case QuickSearchDestination::Kind::OpenTab:
        return QuickSearchDestinationKind::OpenTab;
    case QuickSearchDestination::Kind::Command:
        return QuickSearchDestinationKind::Command;
    case QuickSearchDestination::Kind::Object:
        break;
    }
    return QuickSearchDestinationKind::Object;
}
} // namespace

struct QuickSearchNeedle::Rust {
    rust::Box<QuickSearchQuery> needle;
};

QuickSearchNeedle::QuickSearchNeedle(const QString& query)
    : rust_(std::make_unique<Rust>(Rust{quick_search_query(utf8View(query.toUtf8()))})) {}

QuickSearchNeedle::~QuickSearchNeedle() = default;

std::optional<int> QuickSearchNeedle::score(const QString& name) const {
    const auto bytes = name.toUtf8();
    const int score = rust_->needle->score(utf8View(bytes));
    return score < 0 ? std::nullopt : std::optional<int>(score);
}

QuickSearchPlan QuickSearchNeedle::plan(const QList<QuickSearchDestination>& destinations,
                                        qsizetype editorRows, qsizetype historyRows) const {
    std::vector<QuickSearchDestinationDto> rows;
    rows.reserve(static_cast<size_t>(destinations.size()));
    for (const auto& destination : destinations)
        rows.push_back({destinationKind(destination.kind), toRust(destination.group),
                        toRust(destination.title), toRust(destination.context),
                        toRust(destination.id)});
    const auto result = rust_->needle->plan(
        rust::Slice<const QuickSearchDestinationDto>(rows.data(), rows.size()),
        static_cast<std::uint32_t>(editorRows), static_cast<std::uint32_t>(historyRows));
    QuickSearchPlan plan;
    plan.destinations.reserve(static_cast<qsizetype>(result.destinations.size()));
    for (const auto index : result.destinations)
        plan.destinations.append(static_cast<qsizetype>(index));
    plan.editorRows = result.editor_rows;
    plan.historyRows = result.history_rows;
    plan.more = result.more;
    return plan;
}

const QuickSearchLimits& QuickSearchNeedle::limits() {
    static const QuickSearchLimits cached = [] {
        const auto limits = quick_search_limits();
        return QuickSearchLimits{limits.editor_rows, limits.recent_objects, limits.history_rows};
    }();
    return cached;
}

} // namespace choscordb
