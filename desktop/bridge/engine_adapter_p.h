#pragma once

#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include <QByteArray>
#include <QHash>
#include <QQueue>
#include <QSet>

namespace choscordb {
namespace engine_adapter_detail {
inline rust::Str utf8View(const QByteArray& bytes) {
    return rust::Str(bytes.constData(), static_cast<size_t>(bytes.size()));
}
inline QString string(const rust::String& value) {
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}
inline rust::String rustString(const QString& value) {
    const auto bytes = value.toUtf8();
    return rust::String(bytes.constData(), static_cast<size_t>(bytes.size()));
}
} // namespace engine_adapter_detail
struct EngineAdapter::Private {
    explicit Private(const QString& path);
    rust::Box<BridgeEngine> engine;
    struct RecoveryRequest {
        quint64 token;
        std::function<Submit()> command;
        quint64 bytes;
    };
    QQueue<RecoveryRequest> recoveryQueue;
    std::optional<quint64> activeRecovery;
    quint64 queuedRecoveryBytes = 0;
    QSet<quint64> connections;
    struct QueryPaging {
        quint64 connection;
        quint32 pageSize;
    };
    QHash<quint64, QueryPaging> queryPaging;
    struct InspectionRequest {
        quint64 connection, token;
        QString object;
        ObjectInspectionPane pane;
    };
    QHash<quint64, InspectionRequest> inspections;
    quint64 nextInspectionToken = quint64(1) << 63;
    bool closing = false, stopping = false;
    std::optional<quint64> shutdownToken;
    QString shutdownHistoryError;
    struct Transfer {
        quint64 reserved;
        std::optional<quint64> retained;
        bool released = false;
    };
    QHash<quint64, Transfer> transfers;
};
} // namespace choscordb
