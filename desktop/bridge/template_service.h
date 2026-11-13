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
    static SqlTemplateResult generate(const QString& kind, const QString& qualified,
                                      const QStringList& columns = {}, bool columnsLoaded = true);
    static QString unavailableReason(const QString& kind, bool columnsLoaded, bool hasColumn);
};
}
