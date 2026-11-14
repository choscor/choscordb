#pragma once

#include <QList>
#include <QPair>
#include <QString>

namespace choscordb {
struct ResultColumnWidths {
    QList<QPair<QString, quint32>> user;
};
}
