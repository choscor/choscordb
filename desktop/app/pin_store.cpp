#include "app/pin_store.h"

#include "app/application_data.h"
#include "bridge/rust_text.h"
#include <utility>

namespace choscordb {
namespace {
// Unlike bridge_detail::toRust, rejects lone surrogates instead of replacing them.
rust::String toRust(const QString& value) {
    const auto utf8 = value.toUtf8();
    // A lone UTF-16 surrogate cannot cross a UTF-8 bridge losslessly. Send a
    // sentinel that Rust policy rejects instead of silently replacing it.
    if (QString::fromUtf8(utf8) != value)
        return rust::String("\0", 1);
    return rust::String(utf8.constData(), size_t(utf8.size()));
}

using bridge_detail::fromRust;
using bridge_detail::utf8View;

PinRecordDto toDto(const PinRecord& pin) {
    PinRecordDto dto;
    dto.profile_id = toRust(pin.profileId);
    dto.profile_name = toRust(pin.profileName);
    dto.object_id = toRust(pin.objectId);
    dto.name = toRust(pin.name);
    dto.qualified_name = toRust(pin.qualifiedName);
    dto.kind = toRust(pin.kind);
    dto.parent_object_id = toRust(pin.parentObjectId);
    dto.relation_subtype = toRust(pin.relationSubtype);
    dto.ancestry_ids.reserve(size_t(pin.ancestryIds.size()));
    for (const auto& value : pin.ancestryIds)
        dto.ancestry_ids.push_back(toRust(value));
    dto.ancestry_names.reserve(size_t(pin.ancestryNames.size()));
    for (const auto& value : pin.ancestryNames)
        dto.ancestry_names.push_back(toRust(value));
    dto.unavailable = pin.unavailable;
    return dto;
}

PinRecord fromDto(const PinRecordDto& dto) {
    PinRecord pin;
    pin.profileId = fromRust(dto.profile_id);
    pin.profileName = fromRust(dto.profile_name);
    pin.objectId = fromRust(dto.object_id);
    pin.name = fromRust(dto.name);
    pin.qualifiedName = fromRust(dto.qualified_name);
    pin.kind = fromRust(dto.kind);
    pin.parentObjectId = fromRust(dto.parent_object_id);
    pin.relationSubtype = fromRust(dto.relation_subtype);
    for (const auto& value : dto.ancestry_ids)
        pin.ancestryIds.append(fromRust(value));
    for (const auto& value : dto.ancestry_names)
        pin.ancestryNames.append(fromRust(value));
    pin.unavailable = dto.unavailable;
    return pin;
}

void setError(QString* destination, const rust::String& error) {
    if (destination)
        *destination = fromRust(error);
}
} // namespace

PinStore::PinStore(QString storagePath) {
    profileStorage_ = !storagePath.isEmpty();
    storageLocation_ = profileStorage_ ? std::move(storagePath) : applicationDataDirectory();
}

bool PinStore::valid(const PinRecord& pin) {
    return pin_valid(toDto(pin));
}

QString PinStore::identityKey(const PinRecord& pin) {
    return fromRust(pin_identity_key(toDto(pin)));
}

QList<PinRecord> PinStore::load(QString* error) const {
    const auto path = storageLocation_.toUtf8();
    const auto result = pin_load(utf8View(path), profileStorage_);
    setError(error, result.error);
    QList<PinRecord> pins;
    pins.reserve(qsizetype(result.pins.size()));
    for (const auto& pin : result.pins)
        pins.append(fromDto(pin));
    return pins;
}

bool PinStore::save(const QList<PinRecord>& pins, QString* error) const {
    rust::Vec<PinRecordDto> records;
    records.reserve(size_t(pins.size()));
    for (const auto& pin : pins)
        records.push_back(toDto(pin));
    const auto path = storageLocation_.toUtf8();
    const auto result = pin_save(utf8View(path), profileStorage_, std::move(records));
    setError(error, result.error);
    return result.success;
}
} // namespace choscordb
