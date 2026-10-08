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
// Borrows raw bytes for a Rust byte slice; the array must outlive the call.
inline rust::Slice<const uint8_t> byteView(const QByteArray& bytes) {
    return {reinterpret_cast<const uint8_t*>(bytes.constData()), static_cast<size_t>(bytes.size())};
}
} // namespace choscordb::bridge_detail
