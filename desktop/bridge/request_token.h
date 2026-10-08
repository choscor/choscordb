#pragma once

#include <QtGlobal>
#include <atomic>

namespace choscordb {
// One process-wide sequence for EngineAdapter request tokens. Several owners
// share reply signals such as recoveryFailed, so a token must never be reused
// by another owner. Zero remains reserved for "no pending request".
inline quint64 nextRequestToken() {
    static std::atomic<quint64> next{quint64(1) << 61};
    return next.fetch_add(1, std::memory_order_relaxed);
}
} // namespace choscordb
