#pragma once

#include <QString>
#include <functional>
class QProcess;

namespace choscordb {
// Creates one update startup acknowledgment without overwriting an existing file.
bool writeUpdateReadinessFile(const QString& path);
// Requires an already started child and exact readiness bytes while it remains alive.
bool waitForUpdateReadiness(QProcess& process, const QString& path, int timeoutMs, int stableMs);
bool waitForUpdateReadiness(const QString& path, int timeoutMs, int stableMs,
                            const std::function<bool()>& processAlive);
} // namespace choscordb
