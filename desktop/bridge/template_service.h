#pragma once
#include <QString>
#include <QStringList>
namespace choscordb {
struct SqlTemplateResult {
    bool valid = false;
    QString sql, error;
};
class SqlTemplateService final {
  public:
    // Rust decides which templates list `columns` and whether they must be loaded.
    static SqlTemplateResult generate(const QString& kind, const QString& qualified,
                                      const QStringList& columns = {}, bool columnsLoaded = true);
    // Why a navigator template cannot be generated yet; empty when it can.
    static QString unavailableReason(const QString& kind, bool columnsLoaded, bool hasColumn);
};
} // namespace choscordb
