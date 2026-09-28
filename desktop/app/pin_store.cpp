#include "app/pin_store.h"

#include "app/application_data.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <utility>

namespace choscordb {
namespace {

constexpr int kVersion = 1;
constexpr qsizetype kMaximumFieldLength = 1024 * 1024;
constexpr qint64 kMaximumFileSize = 64 * 1024 * 1024;

bool safeText(const QString& text, bool required = true) {
    if (text.size() > kMaximumFieldLength || (required && text.isEmpty()))
        return false;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const auto character = text.at(i);
        if (character == QChar::Null)
            return false;
        if (character.isHighSurrogate()) {
            if (++i == text.size() || !text.at(i).isLowSurrogate())
                return false;
        } else if (character.isLowSurrogate()) {
            return false;
        }
    }
    return true;
}

bool validAncestry(const QStringList& ids, const QStringList& names) {
    if (!names.isEmpty() && names.size() != ids.size())
        return false;
    for (const auto& id : ids)
        if (!safeText(id))
            return false;
    for (const auto& name : names)
        if (!safeText(name))
            return false;
    return true;
}

QJsonArray jsonStrings(const QStringList& strings) {
    QJsonArray array;
    for (const auto& value : strings)
        array.append(value);
    return array;
}

bool readStrings(const QJsonValue& value, QStringList* strings) {
    if (!value.isArray())
        return false;
    for (const auto& entry : value.toArray()) {
        if (!entry.isString())
            return false;
        strings->append(entry.toString());
    }
    return true;
}

QJsonObject jsonPin(const PinRecord& pin) {
    return {{QStringLiteral("profileId"), pin.profileId},
            {QStringLiteral("profileName"), pin.profileName},
            {QStringLiteral("objectId"), pin.objectId},
            {QStringLiteral("name"), pin.name},
            {QStringLiteral("qualifiedName"), pin.qualifiedName},
            {QStringLiteral("kind"), pin.kind},
            {QStringLiteral("parentObjectId"), pin.parentObjectId},
            {QStringLiteral("relationSubtype"), pin.relationSubtype},
            {QStringLiteral("ancestryIds"), jsonStrings(pin.ancestryIds)},
            {QStringLiteral("ancestryNames"), jsonStrings(pin.ancestryNames)},
            {QStringLiteral("unavailable"), pin.unavailable}};
}

bool readPin(const QJsonValue& value, PinRecord* pin) {
    if (!value.isObject())
        return false;
    const auto object = value.toObject();
    for (const auto& key : {"profileId", "profileName", "objectId", "name", "qualifiedName", "kind",
                            "parentObjectId", "relationSubtype"})
        if (!object.value(QLatin1StringView(key)).isString())
            return false;
    if (!object.value(QStringLiteral("unavailable")).isBool() ||
        !readStrings(object.value(QStringLiteral("ancestryIds")), &pin->ancestryIds) ||
        !readStrings(object.value(QStringLiteral("ancestryNames")), &pin->ancestryNames))
        return false;
    pin->profileId = object.value(QStringLiteral("profileId")).toString();
    pin->profileName = object.value(QStringLiteral("profileName")).toString();
    pin->objectId = object.value(QStringLiteral("objectId")).toString();
    pin->name = object.value(QStringLiteral("name")).toString();
    pin->qualifiedName = object.value(QStringLiteral("qualifiedName")).toString();
    pin->kind = object.value(QStringLiteral("kind")).toString();
    pin->parentObjectId = object.value(QStringLiteral("parentObjectId")).toString();
    pin->relationSubtype = object.value(QStringLiteral("relationSubtype")).toString();
    pin->unavailable = object.value(QStringLiteral("unavailable")).toBool();
    return PinStore::valid(*pin);
}

void setError(QString* error, const QString& message) {
    if (error)
        *error = message;
}

} // namespace

PinStore::PinStore(QString storagePath)
    : path_(storagePath.isEmpty() ? QDir(applicationDataDirectory()).filePath("pins.json")
                                  : std::move(storagePath) + QStringLiteral(".pins.json")) {}

bool PinStore::valid(const PinRecord& pin) {
    static const QSet<QString> kinds = {QStringLiteral("schema"),     QStringLiteral("table"),
                                        QStringLiteral("view"),       QStringLiteral("index"),
                                        QStringLiteral("sequence"),   QStringLiteral("function"),
                                        QStringLiteral("column"),     QStringLiteral("primarykey"),
                                        QStringLiteral("foreignkey"), QStringLiteral("uniquekey")};
    const bool directRootSchema = pin.kind == QStringLiteral("schema") &&
                                  pin.parentObjectId.isEmpty() && pin.ancestryIds.isEmpty();
    return safeText(pin.profileId) && safeText(pin.profileName, false) && safeText(pin.objectId) &&
           safeText(pin.name) && safeText(pin.qualifiedName) && kinds.contains(pin.kind) &&
           (directRootSchema || safeText(pin.parentObjectId)) &&
           safeText(pin.relationSubtype, false) &&
           validAncestry(pin.ancestryIds, pin.ancestryNames);
}

QString PinStore::identityKey(const PinRecord& pin) {
    if (!valid(pin))
        return {};
    return QString::fromUtf8(QJsonDocument(QJsonArray{pin.profileId, pin.kind, pin.objectId,
                                                      pin.qualifiedName, pin.relationSubtype})
                                 .toJson(QJsonDocument::Compact));
}

QList<PinRecord> PinStore::load(QString* error) const {
    setError(error, {});
    QFile file(path_);
    if (!file.exists())
        return {};
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("Could not read pins: ") + file.errorString());
        return {};
    }
    if (file.size() > kMaximumFileSize) {
        setError(error, QStringLiteral("The pin file is too large to read."));
        return {};
    }
    const auto bytes = file.read(kMaximumFileSize + 1);
    if (file.error() != QFileDevice::NoError || bytes.size() > kMaximumFileSize) {
        setError(error, QStringLiteral("Could not read the complete pin file."));
        return {};
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(error, QStringLiteral("The pin file is malformed."));
        return {};
    }
    const auto root = document.object();
    if (!root.value(QStringLiteral("version")).isDouble() ||
        root.value(QStringLiteral("version")).toInt() != kVersion ||
        !root.value(QStringLiteral("pins")).isArray()) {
        setError(error, QStringLiteral("The pin file version or structure is unsupported."));
        return {};
    }
    QList<PinRecord> pins;
    QSet<QString> identities;
    int rejected = 0;
    for (const auto& value : root.value(QStringLiteral("pins")).toArray()) {
        PinRecord pin;
        if (!readPin(value, &pin)) {
            ++rejected;
            continue;
        }
        const auto key = identityKey(pin);
        if (identities.contains(key)) {
            ++rejected;
            continue;
        }
        identities.insert(key);
        pins.append(std::move(pin));
    }
    if (rejected)
        setError(error,
                 QStringLiteral("Skipped %1 invalid or duplicate pin record(s).").arg(rejected));
    return pins;
}

bool PinStore::save(const QList<PinRecord>& pins, QString* error) const {
    setError(error, {});
    QSet<QString> identities;
    QJsonArray array;
    for (const auto& pin : pins) {
        if (!valid(pin)) {
            setError(error, QStringLiteral("A pin has an invalid object identity or context."));
            return false;
        }
        const auto key = identityKey(pin);
        if (identities.contains(key)) {
            setError(error, QStringLiteral("The same object is pinned more than once."));
            return false;
        }
        identities.insert(key);
        array.append(jsonPin(pin));
    }
    const auto bytes = QJsonDocument(QJsonObject{{QStringLiteral("version"), kVersion},
                                                 {QStringLiteral("pins"), array}})
                           .toJson(QJsonDocument::Compact);
    if (bytes.size() > kMaximumFileSize) {
        setError(error, QStringLiteral("The pin file is too large to save."));
        return false;
    }
    if (!QDir().mkpath(QFileInfo(path_).absolutePath())) {
        setError(error, QStringLiteral("Could not create the pin storage directory."));
        return false;
    }
    QSaveFile file(path_);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, QStringLiteral("Could not save pins: ") + file.errorString());
        return false;
    }
    if (file.write(bytes) != bytes.size()) {
        setError(error, QStringLiteral("Could not save pins: ") + file.errorString());
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        setError(error, QStringLiteral("Could not finish saving pins: ") + file.errorString());
        return false;
    }
    return true;
}

} // namespace choscordb
