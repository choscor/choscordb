use crate::statement_ranges;
use std::ops::Range;
/// A nonempty selection wins. Invalid UTF-8 boundaries are rejected.
pub fn execution_range(
    sql: &str,
    cursor: usize,
    selection: Option<Range<usize>>,
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
    let ranges = statement_ranges(sql);
    ranges
        .iter()
        .find(|r| r.contains(&cursor))
        .cloned()
        .or_else(|| ranges.iter().find(|r| r.start >= cursor).cloned())
        .or_else(|| ranges.last().filter(|r| cursor >= r.end).cloned())
}
