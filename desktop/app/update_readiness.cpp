#include "app/update_readiness.h"

#include "bridge/rust_text.h"

namespace choscordb {
bool writeUpdateReadinessFile(const QString& path) {
    const auto utf8 = path.toUtf8();
    return update_write_readiness(bridge_detail::utf8View(utf8));
}
} // namespace choscordb
