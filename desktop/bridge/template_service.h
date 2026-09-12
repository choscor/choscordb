#pragma once
#include <QString>
#include <QStringList>
namespace choscordb {
struct SqlTemplateResult {
    bool valid = false;
    QString sql, error;
};
struct SqlTemplateLimits {
    quint64 maxBytes, maxColumns;
};
class SqlTemplateService final {
  public:
    static SqlTemplateLimits limits();
    static SqlTemplateResult generate(const QString& kind, const QString& qualified,
                                      const QStringList& columns = {});
};
} // namespace choscordb
