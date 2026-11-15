#pragma once
#include <QList>
#include <QString>
#include <QStringList>
namespace choscordb {
struct CreateTemplateKind {
    QString code, label;
};
struct SqlTemplateResult {
    bool valid = false;
    QString sql, error;
};
class SqlTemplateService final {
  public:
    static SqlTemplateResult generate(const QString& kind, const QString& qualified,
                                      const QStringList& columns = {}, bool columnsLoaded = true);
    static QString unavailableReason(const QString& kind, bool columnsLoaded, bool hasColumn);
    static QList<CreateTemplateKind> createTemplates();
    static bool createParent(const QString& parentKind);
    static QString createUnavailableReason(const QString& object, const QString& driver);
    static SqlTemplateResult create(const QString& object, const QString& driver,
                                    const QString& parentKind, const QString& parentQualified);
};
}
