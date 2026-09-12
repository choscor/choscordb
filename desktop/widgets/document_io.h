#pragma once
#include <QByteArray>
#include <QFuture>
#include <QObject>
#include <QString>
namespace choscordb {
struct DocumentIoResult {
    QByteArray bytes;
    QString error;
};
// Workers own only value snapshots; destroying an editor never waits for I/O.
class DocumentIo final : public QObject {
    Q_OBJECT
  public:
    using QObject::QObject;
    static constexpr qint64 MaximumBytes = 16 * 1024 * 1024;
    QFuture<DocumentIoResult> read(QString path) const;
    QFuture<DocumentIoResult> write(QString path, QByteArray bytes) const;
};
} // namespace choscordb
