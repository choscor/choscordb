use crate::scanner::statement_ranges_with_mode;
use std::ops::Range;
/// What an execution without a selection runs.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ExecutionScope {
    /// The statement at the cursor.
    Current,
    /// All statements of the document, from the start of the first to the end of the last.
    All,
}
/// A nonempty selection wins in each scope. Invalid UTF-8 boundaries are rejected.
pub fn execution_range(
    sql: &str,
    cursor: usize,
    selection: Option<Range<usize>>,
    scope: ExecutionScope,
) -> Option<Range<usize>> {
    execution_range_with_mode(sql, cursor, selection, scope, None)
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
        ExecutionScope::Current,
        mysql.then_some(crate::MysqlSqlMode::default()),
    )
}
pub(crate) fn execution_range_with_mode(
    sql: &str,
    cursor: usize,
    selection: Option<Range<usize>>,
    scope: ExecutionScope,
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
    if scope == ExecutionScope::All {
        let (first, last) = (ranges.first()?, ranges.last()?);
        return Some(first.start..last.end);
    }
    ranges
        .iter()
        .find(|r| r.contains(&cursor))
        .cloned()
        .or_else(|| ranges.iter().find(|r| r.start >= cursor).cloned())
        .or_else(|| ranges.last().filter(|r| cursor >= r.end).cloned())
}
