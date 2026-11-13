#pragma once

#include <QString>
#include <vector>

namespace choscordb {

enum class SqlHighlight { Keyword, String, Identifier, Comment, Number };

struct SqlHighlightSpan {
    int start = 0;
    int length = 0;
    SqlHighlight kind = SqlHighlight::Keyword;
};

std::vector<SqlHighlightSpan> sqlHighlightSpans(const QString& sql);

}
