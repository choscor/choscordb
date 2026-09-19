#pragma once

#include <QString>
class QTableView;

namespace choscordb::design {
QString tableStyleSheet();
QString tableItemStyleSheet();
void configureResultTable(QTableView& table);
} // namespace choscordb::design
