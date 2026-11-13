#pragma once

#include <QList>
#include <QString>
#include <memory>
#include <optional>

namespace choscordb {

struct QuickSearchDestination {
    enum class Kind : quint8 { OpenTab, Command, Object };
    Kind kind = Kind::Command;
    QString group, title, context, id;
};
struct QuickSearchPlan {
    QList<qsizetype> destinations;
    qsizetype editorRows = 0, historyRows = 0;
    bool more = false;
};
struct QuickSearchLimits {
    qsizetype editorRows = 0, recentObjects = 0, historyRows = 0;
};
struct QuickSearchStatus {
    bool editorIncomplete = false, historyIncomplete = false, historyClearing = false;
    bool historyEmpty = false, filtered = false, recordingOff = false;
    QString policyError, objectStatus, recentStatus;
    bool more = false;
};
QString quickSearchStatus(const QuickSearchStatus& facts);

class QuickSearchNeedle final {
  public:
    explicit QuickSearchNeedle(const QString& query);
    ~QuickSearchNeedle();
    QuickSearchNeedle(const QuickSearchNeedle&) = delete;
    QuickSearchNeedle& operator=(const QuickSearchNeedle&) = delete;
    [[nodiscard]] std::optional<int> score(const QString& name) const;
    [[nodiscard]] QuickSearchPlan plan(const QList<QuickSearchDestination>& destinations,
                                       qsizetype editorRows, qsizetype historyRows) const;
    static const QuickSearchLimits& limits();

  private:
    struct Rust;
    std::unique_ptr<Rust> rust_;
};

}
