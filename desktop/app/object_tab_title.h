#pragma once

#include "bridge/rust_text.h"
#include <QString>

namespace choscordb {
inline QString objectTabTitle(const QString& objectId, const QString& qualifiedLabel) {
    const auto id = objectId.toUtf8();
    const auto label = qualifiedLabel.toUtf8();
    const auto display =
        object_display_identity_policy(bridge_detail::utf8View(id), bridge_detail::utf8View(label));
    return bridge_detail::fromRust(display.name);
}
} // namespace choscordb
