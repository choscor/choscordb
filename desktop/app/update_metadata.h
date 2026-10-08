#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>

namespace choscordb {
// Presentation copy of a Rust-verified signed update record.
struct UpdateRecord {
    QString version;
    QUrl url;
    qint64 size = 0;
    QByteArray sha256;
    QString notes;
};
} // namespace choscordb
