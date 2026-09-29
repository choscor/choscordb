#include "app/update_metadata.h"

#include "choscordb-bridge/src/lib.rs.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace choscordb {
namespace {
bool fail(QString* error, const QString& message) {
    if (error)
        *error = message;
    return false;
}

QByteArray decodeBase64(const QString& value) {
    const auto encoded = value.toLatin1();
    const auto decoded =
        QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded || decoded.decoded.toBase64() != encoded)
        return {};
    return decoded.decoded;
}

std::optional<QStringList> versionParts(const QString& value) {
    static const QRegularExpression pattern(
        QStringLiteral("^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$"));
    const auto match = pattern.match(value);
    if (!match.hasMatch())
        return std::nullopt;
    return QStringList{match.captured(1), match.captured(2), match.captured(3)};
}

bool newerVersion(const QStringList& candidate, const QStringList& current) {
    for (int index = 0; index < 3; ++index) {
        if (candidate.at(index).size() != current.at(index).size())
            return candidate.at(index).size() > current.at(index).size();
        if (candidate.at(index) != current.at(index))
            return candidate.at(index) > current.at(index);
    }
    return false;
}

bool signedBytesValid(const QByteArray& bytes, const QByteArray& signature,
                      const QByteArray& publicKey) {
    if (publicKey.size() != 32 || signature.size() != 64)
        return false;
    return verify_update_signature(
        rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t*>(publicKey.constData()),
                                   size_t(publicKey.size())),
        rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t*>(bytes.constData()),
                                   size_t(bytes.size())),
        rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t*>(signature.constData()),
                                   size_t(signature.size())));
}
} // namespace

std::optional<UpdateRecord>
parseSignedUpdateMetadata(const QByteArray& envelope, const QByteArray& publicKey,
                          const QString& currentVersion, const QString& expectedPlatform,
                          const QString& expectedArch, const QString& repository, QString* error) {
    if (error)
        error->clear();
    if (envelope.size() > 32768) {
        fail(error, QStringLiteral("Update feed is too large"));
        return std::nullopt;
    }
    QJsonParseError parseError;
    auto outer = QJsonDocument::fromJson(envelope, &parseError);
    if (parseError.error != QJsonParseError::NoError || !outer.isObject()) {
        fail(error, QStringLiteral("Invalid update feed"));
        return std::nullopt;
    }
    const auto wrapper = outer.object();
    if (wrapper.value(QStringLiteral("key_id")).toString() != QStringLiteral("windows-linux-v1")) {
        fail(error, QStringLiteral("Unsupported update signing key"));
        return std::nullopt;
    }
    const auto payload = decodeBase64(wrapper.value(QStringLiteral("payload")).toString());
    const auto signature = decodeBase64(wrapper.value(QStringLiteral("signature")).toString());
    if (payload.isEmpty() || !signedBytesValid(payload, signature, publicKey)) {
        fail(error, QStringLiteral("Update signature is invalid"));
        return std::nullopt;
    }
    auto inner = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !inner.isObject()) {
        fail(error, QStringLiteral("Invalid signed update record"));
        return std::nullopt;
    }
    const auto record = inner.object();
    const auto version = record.value(QStringLiteral("version")).toString();
    const auto candidate = versionParts(version);
    const auto current = versionParts(currentVersion);
    if (!candidate || !current ||
        record.value(QStringLiteral("platform")).toString() != expectedPlatform ||
        record.value(QStringLiteral("arch")).toString() != expectedArch) {
        fail(error, QStringLiteral("Update version or platform does not match"));
        return std::nullopt;
    }
    const auto sizeValue = record.value(QStringLiteral("size"));
    const auto sizeDouble = sizeValue.toDouble(-1);
    const auto digest = record.value(QStringLiteral("sha256")).toString();
    static const QRegularExpression digestPattern(QStringLiteral("^[0-9a-f]{64}$"));
    const auto notes = record.value(QStringLiteral("notes"));
    if (!sizeValue.isDouble() || sizeDouble < 1 || sizeDouble > 9007199254740991.0 ||
        double(qint64(sizeDouble)) != sizeDouble || !digestPattern.match(digest).hasMatch() ||
        !notes.isString() || notes.toString().size() > 8192) {
        fail(error, QStringLiteral("Invalid signed package details"));
        return std::nullopt;
    }
    const auto asset = expectedPlatform == QStringLiteral("windows")
                           ? QStringLiteral("ChoscorDB-%1-windows-x64-setup.exe").arg(version)
                           : QStringLiteral("ChoscorDB-%1-linux-x86_64.AppImage").arg(version);
    const auto expectedUrl = QStringLiteral("https://github.com/%1/releases/download/v%2/%3")
                                 .arg(repository, version, asset);
    const auto urlText = record.value(QStringLiteral("url")).toString();
    if (urlText != expectedUrl || !QUrl(urlText).isValid()) {
        fail(error, QStringLiteral("Unexpected update package URL"));
        return std::nullopt;
    }
    if (!newerVersion(*candidate, *current))
        return std::nullopt;
    return UpdateRecord{version, QUrl(urlText), qint64(sizeDouble),
                        QByteArray::fromHex(digest.toLatin1()), notes.toString()};
}

bool verifyUpdateFile(const QString& path, const UpdateRecord& record, QString* error) {
    if (error)
        error->clear();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() != record.size)
        return fail(error, QStringLiteral("Downloaded package has the wrong size"));
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const auto chunk = file.read(1024 * 1024);
        if (chunk.isEmpty())
            return fail(error, QStringLiteral("Could not verify downloaded package"));
        hash.addData(chunk);
    }
    if (hash.result() != record.sha256)
        return fail(error, QStringLiteral("Downloaded package failed integrity verification"));
    return true;
}
} // namespace choscordb
