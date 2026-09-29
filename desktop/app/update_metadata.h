#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>
#include <optional>

namespace choscordb {
struct UpdateRecord {
    QString version;
    QUrl url;
    qint64 size = 0;
    QByteArray sha256;
    QString notes;
};

// Empty result and empty error means that a valid feed offers no newer release.
std::optional<UpdateRecord>
parseSignedUpdateMetadata(const QByteArray& envelope, const QByteArray& publicKey,
                          const QString& currentVersion, const QString& expectedPlatform,
                          const QString& expectedArch, const QString& repository, QString* error);
bool verifyUpdateFile(const QString& path, const UpdateRecord& record, QString* error);
} // namespace choscordb
