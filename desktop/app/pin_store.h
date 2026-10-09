#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

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
    // Rust's pin list edits; nullopt when the list does not change.
    static std::optional<QList<PinRecord>> toggled(const QList<PinRecord>& pins,
                                                   const PinRecord& candidate, bool unpin);
    static std::optional<QList<PinRecord>> withoutPin(const QList<PinRecord>& pins,
                                                      const QString& key);
    static std::optional<QList<PinRecord>> withoutProfile(const QList<PinRecord>& pins,
                                                          const QString& profileId);

  private:
    QString storageLocation_;
    bool profileStorage_ = false;
};

} // namespace choscordb
