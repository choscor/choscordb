#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace choscordb {

struct PinRecord {
    QString profileId{};
    QString profileName{};
    QString objectId{};
    QString name{};
    QString qualifiedName{};
    QString kind{};
    QString parentObjectId{};
    QString relationSubtype{};
    QStringList ancestryIds{};
    QStringList ancestryNames{};
    bool unavailable = false;
};

class PinStore {
  public:
    explicit PinStore(QString storagePath = {});
    QList<PinRecord> load(QString* error = nullptr) const;
    bool save(const QList<PinRecord>& pins, QString* error = nullptr) const;
    static bool valid(const PinRecord& pin);
    static QString identityKey(const PinRecord& pin);

  private:
    QString path_;
};

} // namespace choscordb
