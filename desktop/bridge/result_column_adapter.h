#pragma once

#include "models/result_table_model.h"

namespace choscordb {
struct ColumnDto;

ResultColumn resultColumn(const ColumnDto& column);
} // namespace choscordb
