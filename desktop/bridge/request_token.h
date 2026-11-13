#pragma once

#include <QtGlobal>
#include <atomic>

namespace choscordb {
inline quint64 nextRequestToken() {
    static std::atomic<quint64> next{quint64(1) << 61};
    return next.fetch_add(1, std::memory_order_relaxed);
}
}
