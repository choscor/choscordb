#include "widgets/document_io/document_io.h"
#include "choscordb-bridge/src/lib.rs.h"
#include <QtConcurrentRun>

namespace choscordb {
namespace {
DocumentIoResult fromDto(const DocumentIoResultDto& value) {
    return {QByteArray(reinterpret_cast<const char*>(value.bytes.data()),
                       static_cast<qsizetype>(value.bytes.size())),
            QString::fromUtf8(value.error.data(), static_cast<qsizetype>(value.error.size()))};
}
rust::Str pathView(const QByteArray& path) {
    return {path.constData(), static_cast<size_t>(path.size())};
}
} // namespace

QFuture<DocumentIoResult> DocumentIo::read(QString path) const {
    return QtConcurrent::run([path = std::move(path)]() {
        if (!path.isValidUtf16())
            return DocumentIoResult{{}, QObject::tr("Path is not valid Unicode.")};
        const auto encoded = path.toUtf8();
        return fromDto(read_sql_document_file(pathView(encoded)));
    });
}
QFuture<DocumentIoResult> DocumentIo::readSaved(QString root, QString path) const {
    return QtConcurrent::run([root = std::move(root), path = std::move(path)]() {
        if (!root.isValidUtf16() || !path.isValidUtf16())
            return DocumentIoResult{{}, QObject::tr("Path is not valid Unicode.")};
        const auto encodedRoot = root.toUtf8();
        const auto encodedPath = path.toUtf8();
        return fromDto(saved_sql_read_file(pathView(encodedRoot), pathView(encodedPath)));
    });
}
QFuture<DocumentIoResult> DocumentIo::write(QString path, QByteArray bytes) const {
    return QtConcurrent::run([path = std::move(path), bytes = std::move(bytes)]() {
        if (!path.isValidUtf16())
            return DocumentIoResult{{}, QObject::tr("Path is not valid Unicode.")};
        const auto encoded = path.toUtf8();
        const auto data = rust::Slice<const uint8_t>(
            reinterpret_cast<const uint8_t*>(bytes.constData()), static_cast<size_t>(bytes.size()));
        return fromDto(write_sql_document_file(pathView(encoded), data));
    });
}
} // namespace choscordb
