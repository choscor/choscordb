#pragma once
#include <QList>
#include <QString>
#include <memory>
namespace choscordb {
struct CompletionCandidate {
    QString label, insertText, kind;
};
struct CompletionPage {
    bool valid = false, partial = false;
    quint64 start = 0, end = 0;
    QList<CompletionCandidate> items;
};
struct CompletionLimits {
    quint64 maxResults, maxPrefixBytes, maxMetadataEntries, maxMetadataBytes, maxSourceBytes;
};
// Immutable catalog: copies share Rust-owned metadata and may complete on workers.
// No engine, connection, QObject, or widget is accessed by this service.
class CompletionService final {
  public:
    explicit CompletionService(QList<CompletionCandidate> items = {}, bool partial = false);
    static CompletionLimits limits();
    CompletionPage complete(const QString& source, quint64 cursor, bool requested) const;

  private:
    struct Private;
    std::shared_ptr<const Private> d_;
};
} // namespace choscordb
