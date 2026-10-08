#pragma once

#include <QString>
#include <QStringList>
#include <optional>

namespace choscordb {

// A query folded once so it can be scored against many candidate names.
class QuickSearchNeedle final {
  public:
    explicit QuickSearchNeedle(const QString& query);
    // Lower scores rank first. Name matches are always below body-text scores (200+).
    // Returns no score for empty, invalid, oversized, or unmatched input.
    [[nodiscard]] std::optional<int> score(const QString& name) const;

  private:
    bool valid_ = false;
    QString folded_;
    QStringList words_;
};

std::optional<int> quickSearchNameScore(const QString& query, const QString& name);

} // namespace choscordb
