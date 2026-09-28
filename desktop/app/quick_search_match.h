#pragma once

#include <QString>
#include <optional>

namespace choscordb {

// Lower scores rank first. Name matches are always below body-text scores (200+).
// Returns no score for empty, invalid, oversized, or unmatched input.
std::optional<int> quickSearchNameScore(const QString& query, const QString& name);

} // namespace choscordb
