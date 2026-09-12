#include "widgets/document_io.h"
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QtConcurrentRun>
namespace choscordb {
QFuture<DocumentIoResult> DocumentIo::read(QString path) const {
    return QtConcurrent::run([path = std::move(path)]() -> DocumentIoResult {
        if (!QFileInfo(path).isFile())
            return {{}, tr("Path is not a regular file.")};
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return {{}, file.errorString()};
        if (file.size() > MaximumBytes)
            return {{}, tr("SQL files are limited to 16 MiB.")};
        auto bytes = file.read(MaximumBytes + 1);
        if (file.error() != QFileDevice::NoError)
            return {{}, file.errorString()};
        if (bytes.size() > MaximumBytes)
            return {{}, tr("SQL files are limited to 16 MiB.")};
        return {std::move(bytes), {}};
    });
}
QFuture<DocumentIoResult> DocumentIo::write(QString path, QByteArray bytes) const {
    return QtConcurrent::run(
        [path = std::move(path), bytes = std::move(bytes)]() -> DocumentIoResult {
            if (bytes.size() > MaximumBytes)
                return {{}, tr("SQL files are limited to 16 MiB.")};
            QSaveFile file(path);
            if (!file.open(QIODevice::WriteOnly))
                return {{}, file.errorString()};
            if (file.write(bytes) != bytes.size())
                return {{}, file.errorString()};
            if (!file.commit())
                return {{}, file.errorString()};
            return {};
        });
}
} // namespace choscordb
