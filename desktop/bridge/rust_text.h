#pragma once

#include "choscordb-bridge/src/lib.rs.h"
#include <QByteArray>
#include <QString>

namespace choscordb::bridge_detail {
inline QString fromRust(const rust::String& value) {
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}
inline rust::String toRust(const QString& value) {
    const auto bytes = value.toUtf8();
    return rust::String(bytes.constData(), static_cast<size_t>(bytes.size()));
}
inline rust::Str utf8View(const QByteArray& bytes) {
    return rust::Str(bytes.constData(), static_cast<size_t>(bytes.size()));
}
inline rust::Slice<const uint8_t> byteView(const QByteArray& bytes) {
    return {reinterpret_cast<const uint8_t*>(bytes.constData()), static_cast<size_t>(bytes.size())};
}
}
