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
class DocumentIo final : public QObject {
    Q_OBJECT
  public:
    using QObject::QObject;
    QFuture<DocumentIoResult> read(QString path) const;
    QFuture<DocumentIoResult> readSaved(QString root, QString path) const;
    QFuture<DocumentIoResult> write(QString path, QByteArray bytes) const;
};
}
