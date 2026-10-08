#pragma once
#include "bridge/rust_text.h"
#include <QByteArray>
#include <QString>

namespace choscordb {
struct DeferredAssemblyOutcome {
    bool complete = false;
    quint64 receivedBytes = 0;
    QString kind;
    QString text;
    QByteArray bytes;
    QString databaseType;
    QString error;
};

// Created and called only on a QtConcurrent worker; the UI owns the shared pointer
// but never reads or mutates the Rust assembler state directly.
class DeferredAssemblerJob final {
  public:
    DeferredAssemblerJob(const QString& databaseType, bool fallback, quint64 declaredBytes,
                         quint64 resolvedBytes, bool json)
        : assembler_(start(databaseType, fallback, declaredBytes, resolvedBytes, json)) {}

    QString initialError() const { return text(deferred_assembler_initial_error(*assembler_)); }

    DeferredAssemblyOutcome append(const QString& kind, quint64 offset, quint64 totalBytes,
                                   const QByteArray& chunk, bool hasLease) {
        const auto encodedKind = kind.toUtf8();
        const auto dto =
            deferred_assembler_push(*assembler_, bridge_detail::utf8View(encodedKind), offset,
                                    totalBytes, bridge_detail::byteView(chunk), hasLease);
        return {dto.complete,
                dto.received_bytes,
                text(dto.kind),
                text(dto.text),
                QByteArray(reinterpret_cast<const char*>(dto.bytes.data()),
                           static_cast<qsizetype>(dto.bytes.size())),
                text(dto.database_type),
                text(dto.error)};
    }

  private:
    static QString text(const rust::String& value) { return bridge_detail::fromRust(value); }
    static rust::Box<RustDeferredAssembler> start(const QString& databaseType, bool fallback,
                                                  quint64 declaredBytes, quint64 resolvedBytes,
                                                  bool json) {
        const auto encoded = databaseType.toUtf8();
        return deferred_assembler_new(bridge_detail::utf8View(encoded), fallback, declaredBytes,
                                      resolvedBytes, json);
    }
    rust::Box<RustDeferredAssembler> assembler_;
};
} // namespace choscordb
