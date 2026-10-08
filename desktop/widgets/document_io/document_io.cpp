#include "widgets/document_io/document_io.h"
#include "bridge/rust_text.h"
#include <QtConcurrentRun>

namespace choscordb {
namespace {
DocumentIoResult fromDto(const DocumentIoResultDto& value) {
    return {QByteArray(reinterpret_cast<const char*>(value.bytes.data()),
                       static_cast<qsizetype>(value.bytes.size())),
            bridge_detail::fromRust(value.error)};
}
using bridge_detail::utf8View;
} // namespace

QFuture<DocumentIoResult> DocumentIo::read(QString path) const {
    return QtConcurrent::run([path = std::move(path)]() {
        if (!path.isValidUtf16())
            return DocumentIoResult{{}, QObject::tr("Path is not valid Unicode.")};
        const auto encoded = path.toUtf8();
        return fromDto(read_sql_document_file(utf8View(encoded)));
    });
}
QFuture<DocumentIoResult> DocumentIo::readSaved(QString root, QString path) const {
    return QtConcurrent::run([root = std::move(root), path = std::move(path)]() {
        if (!root.isValidUtf16() || !path.isValidUtf16())
            return DocumentIoResult{{}, QObject::tr("Path is not valid Unicode.")};
        const auto encodedRoot = root.toUtf8();
        const auto encodedPath = path.toUtf8();
        return fromDto(saved_sql_read_file(utf8View(encodedRoot), utf8View(encodedPath)));
    });
}
QFuture<DocumentIoResult> DocumentIo::write(QString path, QByteArray bytes) const {
    return QtConcurrent::run([path = std::move(path), bytes = std::move(bytes)]() {
        if (!path.isValidUtf16())
            return DocumentIoResult{{}, QObject::tr("Path is not valid Unicode.")};
        const auto encoded = path.toUtf8();
        return fromDto(write_sql_document_file(utf8View(encoded), bridge_detail::byteView(bytes)));
    });
}
} // namespace choscordb
