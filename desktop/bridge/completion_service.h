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
    quint64 maxMetadataEntries = 0, maxMetadataBytes = 0, maxMetadataVisits = 0;
};
class CompletionService final {
  public:
    explicit CompletionService(QList<CompletionCandidate> items = {}, bool partial = false);
    static CompletionLimits limits();
    static bool sourceSupported(quint64 bytes);
    CompletionPage complete(const QString& source, quint64 cursor, bool requested) const;

  private:
    struct Private;
    std::shared_ptr<const Private> d_;
};
}
