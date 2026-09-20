use crate::scanner::statement_ranges_with_mode;
use std::ops::Range;
/// A nonempty selection wins. Invalid UTF-8 boundaries are rejected.
pub fn execution_range(
    sql: &str,
    cursor: usize,
    selection: Option<Range<usize>>,
) -> Option<Range<usize>> {
    execution_range_dialect(sql, cursor, selection, false)
}
pub(crate) fn execution_range_dialect(
    sql: &str,
    cursor: usize,
    selection: Option<Range<usize>>,
    mysql: bool,
) -> Option<Range<usize>> {
    execution_range_with_mode(
        sql,
        cursor,
        selection,
        mysql.then_some(crate::MysqlSqlMode::default()),
    )
}
pub(crate) fn execution_range_with_mode(
    sql: &str,
    cursor: usize,
    selection: Option<Range<usize>>,
    mode: Option<crate::MysqlSqlMode>,
) -> Option<Range<usize>> {
    if let Some(range) = selection.filter(|r| !r.is_empty()) {
        return sql
            .get(range.clone())
            .filter(|s| !s.trim().is_empty())
            .map(|_| range);
    }
    if cursor > sql.len() || !sql.is_char_boundary(cursor) {
        return None;
    }
    let ranges = statement_ranges_with_mode(sql, mode);
    ranges
        .iter()
        .find(|r| r.contains(&cursor))
        .cloned()
        .or_else(|| ranges.iter().find(|r| r.start >= cursor).cloned())
        .or_else(|| ranges.last().filter(|r| cursor >= r.end).cloned())
}
