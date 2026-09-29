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
    else if (const auto* booleanValue = std::get_if<bool>(&value)) {
        result.kind = "boolean";
        result.boolean = *booleanValue;
    } else if (const auto* integerValue = std::get_if<qint64>(&value)) {
        result.kind = "integer";
        result.integer = *integerValue;
    } else if (const auto* realValue = std::get_if<double>(&value)) {
        result.kind = "real";
        result.real = *realValue;
    } else if (const auto* decimalValue = std::get_if<DecimalValue>(&value)) {
        result.kind = "decimal";
        if (!eligibilityOnly)
            result.text = string(decimalValue->text);
    } else if (const auto* textValue = std::get_if<QString>(&value)) {
        result.kind = "text";
        if (!eligibilityOnly)
            result.text = string(*textValue);
    } else if (const auto* binaryValue = std::get_if<QByteArray>(&value)) {
        result.kind = "binary";
        if (!eligibilityOnly)
            for (const auto byte : *binaryValue)
                result.bytes.push_back(static_cast<uint8_t>(byte));
    } else if (const auto* deferredValue = std::get_if<DeferredValue>(&value)) {
        result.kind = deferredValue->fallback ? "deferred_fallback" : "deferred";
        result.handle = deferredValue->handle;
        result.byte_length = deferredValue->bytes;
        if (!eligibilityOnly)
            result.database_type = string(deferredValue->type);
    } else if (const auto* fallbackValue = std::get_if<FallbackText>(&value)) {
        result.kind = "fallback_text";
        if (!eligibilityOnly) {
            result.text = string(fallbackValue->text);
            result.database_type = string(fallbackValue->databaseType);
        }
    } else if (const auto* unavailableValue = std::get_if<UnavailableValue>(&value)) {
        result.kind = "unavailable";
        if (!eligibilityOnly) {
            result.text = string(unavailableValue->reason);
            result.database_type = string(unavailableValue->databaseType);
        }
    }
    if (validUnicode)
        *validUnicode &= valid;
    else if (!valid)
        result.kind = "invalid_unicode";
    return result;
}
} // namespace choscordb::bridge_detail
