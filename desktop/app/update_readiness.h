#pragma once

#include <QString>

namespace choscordb {
// Creates one update startup acknowledgment without overwriting an existing file.
bool writeUpdateReadinessFile(const QString& path);
} // namespace choscordb
