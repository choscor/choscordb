#include "bridge/sql_highlight.h"
#include "bridge/rust_text.h"

namespace choscordb {

std::vector<SqlHighlightSpan> sqlHighlightSpans(const QString& sql) {
    const auto bytes = sql.toUtf8();
    std::vector<SqlHighlightSpan> spans;
    for (const auto& span : sql_highlight_spans(bridge_detail::utf8View(bytes))) {
        SqlHighlight kind = SqlHighlight::Keyword;
        switch (span.kind) {
        case SqlHighlightKind::Keyword:
            kind = SqlHighlight::Keyword;
            break;
        case SqlHighlightKind::String:
            kind = SqlHighlight::String;
            break;
        case SqlHighlightKind::Identifier:
            kind = SqlHighlight::Identifier;
            break;
        case SqlHighlightKind::Comment:
            kind = SqlHighlight::Comment;
            break;
        case SqlHighlightKind::Number:
            kind = SqlHighlight::Number;
            break;
        }
        spans.push_back({static_cast<int>(span.start), static_cast<int>(span.length), kind});
    }
    return spans;
}

} // namespace choscordb
