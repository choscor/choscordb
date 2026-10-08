#pragma once

#include "choscordb-bridge/src/lib.rs.h"
#include <QByteArray>
#include <QString>

namespace choscordb::bridge_detail {
// Conversion only between Qt UTF-16 and Rust UTF-8 at the CXX boundary.
inline QString fromRust(const rust::String& value) {
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}
inline rust::String toRust(const QString& value) {
    const auto bytes = value.toUtf8();
    return rust::String(bytes.constData(), static_cast<size_t>(bytes.size()));
}
// The view borrows the caller's encoded bytes, which must outlive the call.
inline rust::Str utf8View(const QByteArray& bytes) {
    return rust::Str(bytes.constData(), static_cast<size_t>(bytes.size()));
}
} // namespace choscordb::bridge_detail
