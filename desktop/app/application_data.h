#pragma once
#include <QDir>
#include <QStandardPaths>
#include <QString>
#ifndef CHOSCORDB_APP_ID
#define CHOSCORDB_APP_ID "com.choscor.ChoscorDB"
#endif
namespace choscordb {
inline QString applicationDataDirectory() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
        .filePath(CHOSCORDB_APP_ID);
}
} // namespace choscordb
