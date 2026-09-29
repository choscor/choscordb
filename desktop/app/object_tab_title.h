#pragma once

#include "choscordb-bridge/src/lib.rs.h"
#include <QString>

namespace choscordb {
inline QString objectTabTitle(const QString& objectId, const QString& qualifiedLabel) {
    const auto id = objectId.toUtf8();
    const auto label = qualifiedLabel.toUtf8();
    const auto display =
        object_display_identity_policy(rust::Str(id.constData(), size_t(id.size())),
                                       rust::Str(label.constData(), size_t(label.size())));
    return QString::fromUtf8(display.name.data(), qsizetype(display.name.size()));
}
} // namespace choscordb
