#pragma once

#include "choscordb-bridge/src/lib.rs.h"
#include <QByteArray>
#include <QString>
#include <QStringList>

namespace choscordb::bridge_detail {
inline QString fromRust(const rust::String& value) {
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}
inline rust::String toRust(const QString& value) {
    const auto bytes = value.toUtf8();
    return rust::String(bytes.constData(), static_cast<size_t>(bytes.size()));
}
inline rust::Vec<rust::String> toRustStrings(const QStringList& values) {
    rust::Vec<rust::String> output;
    output.reserve(static_cast<size_t>(values.size()));
    for (const auto& value : values)
        output.push_back(toRust(value));
    return output;
}
inline QStringList fromRustStrings(const rust::Vec<rust::String>& values) {
    QStringList output;
    output.reserve(static_cast<qsizetype>(values.size()));
    for (const auto& value : values)
        output.append(fromRust(value));
    return output;
}
inline rust::Str utf8View(const QByteArray& bytes) {
    return rust::Str(bytes.constData(), static_cast<size_t>(bytes.size()));
}
inline rust::Slice<const uint8_t> byteView(const QByteArray& bytes) {
    return {reinterpret_cast<const uint8_t*>(bytes.constData()), static_cast<size_t>(bytes.size())};
}
}
