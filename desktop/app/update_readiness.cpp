#include "app/update_readiness.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>

namespace choscordb {
bool writeUpdateReadinessFile(const QString& path) {
    const QFileInfo target(path);
    static const QRegularExpression namePattern(
        QStringLiteral("^ChoscorDB-update-ready-[A-Za-z0-9]{6,64}\\.txt$"));
    const QDir temp(QStandardPaths::writableLocation(QStandardPaths::TempLocation));
    const auto parentDirectory = target.dir().canonicalPath();
    const auto tempDirectory = temp.canonicalPath();
#ifdef Q_OS_WIN
    const bool trustedDirectory = !tempDirectory.isEmpty() &&
                                  parentDirectory.compare(tempDirectory, Qt::CaseInsensitive) == 0;
#else
    const bool trustedDirectory = !tempDirectory.isEmpty() && parentDirectory == tempDirectory;
#endif
    if (!target.isAbsolute() || target.exists() || target.isSymLink() ||
        !namePattern.match(target.fileName()).hasMatch() || !trustedDirectory)
        return false;
    QFile marker(path);
    if (!marker.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        return false;
    const bool written = marker.write("ready\n", 6) == 6 && marker.flush();
    marker.close();
    if (!written)
        QFile::remove(path);
    return written;
}
bool waitForUpdateReadiness(QProcess& process, const QString& path, int timeoutMs, int stableMs) {
    return waitForUpdateReadiness(path, timeoutMs, stableMs, [&process] {
        (void)process.waitForFinished(0);
        return process.state() == QProcess::Running;
    });
}

bool waitForUpdateReadiness(const QString& path, int timeoutMs, int stableMs,
                            const std::function<bool()>& processAlive) {
    if (!processAlive || timeoutMs <= 0 || stableMs < 0 || !processAlive())
        return false;
    QElapsedTimer elapsed;
    elapsed.start();
    qint64 acknowledgedAt = -1;
    while (elapsed.elapsed() < timeoutMs) {
        if (!processAlive())
            return false;
        QFile marker(path);
        const bool readable = marker.open(QIODevice::ReadOnly);
        const bool acknowledged = readable && marker.size() == 6 && marker.readAll() == "ready\n";
        marker.close();
        if (!acknowledged)
            acknowledgedAt = -1;
        else if (acknowledgedAt < 0)
            acknowledgedAt = elapsed.elapsed();
        else if (elapsed.elapsed() - acknowledgedAt >= stableMs)
            return processAlive();
        const auto remaining = qint64(timeoutMs) - elapsed.elapsed();
        if (remaining <= 0)
            break;
        QThread::msleep(static_cast<unsigned long>(qMin<qint64>(100, remaining)));
    }
    return false;
}
} // namespace choscordb
