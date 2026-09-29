#include "app/update_readiness.h"

#include "choscordb-bridge/src/lib.rs.h"

namespace choscordb {
bool writeUpdateReadinessFile(const QString& path) {
    const auto utf8 = path.toUtf8();
    return update_write_readiness(rust::Str(utf8.constData(), size_t(utf8.size())));
}
} // namespace choscordb
