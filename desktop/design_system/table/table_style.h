#pragma once

#include <QString>
class QTableView;

namespace choscordb::design {
QString tableStyleSheet();
QString tableItemStyleSheet();
void configureResultTable(QTableView& table, bool showGrid = true);
} // namespace choscordb::design
