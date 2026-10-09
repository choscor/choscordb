#pragma once

#include <QString>
#include <vector>

namespace choscordb {

enum class SqlHighlight { Keyword, String, Identifier, Comment, Number };

// A highlighted range in QString (UTF-16) positions.
struct SqlHighlightSpan {
    int start = 0;
    int length = 0;
    SqlHighlight kind = SqlHighlight::Keyword;
};

// Spans for read-only SQL such as inspected DDL, in source order.
std::vector<SqlHighlightSpan> sqlHighlightSpans(const QString& sql);

} // namespace choscordb
