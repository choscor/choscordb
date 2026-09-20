#pragma once

#include "choscordb-bridge/src/lib.rs.h"
#include <QString>
#include <atomic>

namespace choscordb::query_workspace_detail {
inline quint64 nextEditRequestToken() {
    static std::atomic<quint64> token{1};
    return token.fetch_add(1, std::memory_order_relaxed);
}
inline QString text(const rust::String& value) {
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}
} // namespace choscordb::query_workspace_detail
