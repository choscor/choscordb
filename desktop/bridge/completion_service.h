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
// How much navigator metadata one completion catalog may copy (Rust-owned).
struct CompletionLimits {
    quint64 maxMetadataEntries = 0, maxMetadataBytes = 0, maxMetadataVisits = 0;
};
// Immutable catalog: copies share Rust-owned metadata and may complete on workers.
// No engine, connection, QObject, or widget is accessed by this service.
class CompletionService final {
  public:
    explicit CompletionService(QList<CompletionCandidate> items = {}, bool partial = false);
    static CompletionLimits limits();
    // Whether an editor document of `bytes` UTF-8 bytes can be offered completions.
    static bool sourceSupported(quint64 bytes);
    CompletionPage complete(const QString& source, quint64 cursor, bool requested) const;

  private:
    struct Private;
    std::shared_ptr<const Private> d_;
};
} // namespace choscordb
