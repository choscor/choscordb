#pragma once

#include "choscordb-bridge/src/lib.rs.h"
#include "models/result_table_model.h"

namespace choscordb::bridge_detail {
// Conversion only: database rules and value validation live on the Rust side.
inline CellDto cellDto(const Cell& value, bool eligibilityOnly = false,
                       bool* validUnicode = nullptr) {
    CellDto result;
    bool valid = true;
    const auto string = [&valid](const QString& text) {
        valid &= text.isValidUtf16();
        const auto bytes = text.toUtf8();
        return rust::String(bytes.constData(), static_cast<size_t>(bytes.size()));
    };
    if (std::holds_alternative<std::monostate>(value))
        result.kind = "null";
    else if (const auto* typed = std::get_if<bool>(&value)) {
        result.kind = "boolean";
        result.boolean = *typed;
    } else if (const auto* typed = std::get_if<qint64>(&value)) {
        result.kind = "integer";
        result.integer = *typed;
    } else if (const auto* typed = std::get_if<double>(&value)) {
        result.kind = "real";
        result.real = *typed;
    } else if (const auto* typed = std::get_if<DecimalValue>(&value)) {
        result.kind = "decimal";
        if (!eligibilityOnly)
            result.text = string(typed->text);
    } else if (const auto* typed = std::get_if<QString>(&value)) {
        result.kind = "text";
        if (!eligibilityOnly)
            result.text = string(*typed);
    } else if (const auto* typed = std::get_if<QByteArray>(&value)) {
        result.kind = "binary";
        if (!eligibilityOnly)
            for (const auto byte : *typed)
                result.bytes.push_back(static_cast<uint8_t>(byte));
    } else if (const auto* typed = std::get_if<DeferredValue>(&value)) {
        result.kind = typed->fallback ? "deferred_fallback" : "deferred";
        result.handle = typed->handle;
        result.byte_length = typed->bytes;
        if (!eligibilityOnly)
            result.database_type = string(typed->type);
    } else if (const auto* typed = std::get_if<FallbackText>(&value)) {
        result.kind = "fallback_text";
        if (!eligibilityOnly) {
            result.text = string(typed->text);
            result.database_type = string(typed->databaseType);
        }
    } else if (const auto* typed = std::get_if<UnavailableValue>(&value)) {
        result.kind = "unavailable";
        if (!eligibilityOnly) {
            result.text = string(typed->reason);
            result.database_type = string(typed->databaseType);
        }
    }
    if (validUnicode)
        *validUnicode &= valid;
    else if (!valid)
        result.kind = "invalid_unicode";
    return result;
}
} // namespace choscordb::bridge_detail
