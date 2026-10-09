#pragma once

#include <QList>
#include <QString>
#include <memory>
#include <optional>

namespace choscordb {

// One palette destination Rust ranks; the kind orders destinations for a blank query.
struct QuickSearchDestination {
    enum class Kind : quint8 { OpenTab, Command, Object };
    Kind kind = Kind::Command;
    QString group, title, context, id;
};
// Destination indices in display order and how many editor and history rows follow.
struct QuickSearchPlan {
    QList<qsizetype> destinations;
    qsizetype editorRows = 0, historyRows = 0;
    bool more = false;
};
struct QuickSearchLimits {
    qsizetype editorRows = 0, recentObjects = 0, historyRows = 0;
};

// A query folded once in Rust so it can be scored against many candidate names.
class QuickSearchNeedle final {
  public:
    explicit QuickSearchNeedle(const QString& query);
    ~QuickSearchNeedle();
    QuickSearchNeedle(const QuickSearchNeedle&) = delete;
    QuickSearchNeedle& operator=(const QuickSearchNeedle&) = delete;
    // Lower scores rank first; name matches stay below body-text scores (200+).
    [[nodiscard]] std::optional<int> score(const QString& name) const;
    // Rust ranks the destinations and bounds the rows the palette shows.
    [[nodiscard]] QuickSearchPlan plan(const QList<QuickSearchDestination>& destinations,
                                       qsizetype editorRows, qsizetype historyRows) const;
    // Rows the palette collects: editor matches, recent objects and the history page.
    static const QuickSearchLimits& limits();

  private:
    struct Rust;
    std::unique_ptr<Rust> rust_;
};

} // namespace choscordb
