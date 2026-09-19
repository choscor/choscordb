#pragma once

#include <QString>
#include <QStringView>

namespace choscordb::design {
// Load a bundled stylesheet using a path relative to desktop/design_system.
QString loadStyleSheet(QStringView relativePath);
} // namespace choscordb::design
