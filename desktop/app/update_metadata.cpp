#include "app/update_metadata.h"

#include "choscordb-bridge/src/lib.rs.h"

namespace choscordb {
namespace {
QString fromRust(const rust::String& value) {
    return QString::fromUtf8(value.data(), qsizetype(value.size()));
}

rust::Slice<const uint8_t> bytes(const QByteArray& value) {
    return {reinterpret_cast<const uint8_t*>(value.constData()), size_t(value.size())};
}

UpdateRecordDto toDto(const UpdateRecord& record) {
    UpdateRecordDto dto;
    dto.version = record.version.toStdString();
    dto.url = record.url.toString(QUrl::FullyEncoded).toStdString();
    dto.size = record.size < 0 ? 0 : uint64_t(record.size);
    dto.sha256.reserve(size_t(record.sha256.size()));
    for (const auto byte : record.sha256)
        dto.sha256.push_back(uint8_t(byte));
    dto.notes = record.notes.toStdString();
    return dto;
}
} // namespace

std::optional<UpdateRecord>
parseSignedUpdateMetadata(const QByteArray& envelope, const QByteArray& publicKey,
                          const QString& currentVersion, const QString& expectedPlatform,
                          const QString& expectedArch, const QString& repository, QString* error) {
    const auto version = currentVersion.toUtf8();
    const auto platform = expectedPlatform.toUtf8();
    const auto arch = expectedArch.toUtf8();
    const auto repo = repository.toUtf8();
    const auto result = update_parse_signed_metadata(
        bytes(envelope), bytes(publicKey), rust::Str(version.constData(), size_t(version.size())),
        rust::Str(platform.constData(), size_t(platform.size())),
        rust::Str(arch.constData(), size_t(arch.size())),
        rust::Str(repo.constData(), size_t(repo.size())));
    if (error)
        *error = fromRust(result.error);
    if (!result.found)
        return std::nullopt;
    return UpdateRecord{fromRust(result.record.version), QUrl(fromRust(result.record.url)),
                        qint64(result.record.size),
                        QByteArray(reinterpret_cast<const char*>(result.record.sha256.data()),
                                   qsizetype(result.record.sha256.size())),
                        fromRust(result.record.notes)};
}

bool verifyUpdateFile(const QString& path, const UpdateRecord& record, QString* error) {
    const auto utf8Path = path.toUtf8();
    const auto result =
        update_verify_file(rust::Str(utf8Path.constData(), size_t(utf8Path.size())), toDto(record));
    if (error)
        *error = fromRust(result.error);
    return result.success;
}
} // namespace choscordb
