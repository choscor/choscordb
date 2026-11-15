use crate::scanner::statement_ranges_with_mode;
use std::ops::Range;

pub const MAX_RESULT_TABS: usize = 64;
pub const MAX_RESULT_TAB_LABEL_CHARS: usize = 64;
const NAME_PREFIX: &str = "-- name:";

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PlannedStatement {
    pub range: Range<usize>,
    pub label: String,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum StatementPlanError {
    Empty,
    TooMany,
}

impl std::fmt::Display for StatementPlanError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Empty => f.write_str("No executable statement in the document."),
            Self::TooMany => write!(
                f,
                "Run all can show up to {MAX_RESULT_TABS} results. Run fewer statements at a time."
            ),
        }
    }
}

fn statement_name(statement: &str) -> Option<String> {
    for line in statement.lines() {
        let line = line.trim();
        if line.is_empty() {
            continue;
        }
        if !line.starts_with("--") {
            return None;
        }
        if let Some(name) = line.strip_prefix(NAME_PREFIX).map(str::trim)
            && !name.is_empty()
        {
            return Some(name.chars().take(MAX_RESULT_TAB_LABEL_CHARS).collect());
        }
    }
    None
}

pub(crate) fn statement_plan_with_mode(
    sql: &str,
    within: Range<usize>,
    mode: Option<crate::MysqlSqlMode>,
) -> Result<Vec<PlannedStatement>, StatementPlanError> {
    let Some(text) = sql.get(within.clone()) else {
        return Err(StatementPlanError::Empty);
    };
    let ranges: Vec<_> = statement_ranges_with_mode(text, mode)
        .into_iter()
        .map(|range| range.start + within.start..range.end + within.start)
        .collect();
    if ranges.is_empty() {
        return Err(StatementPlanError::Empty);
    }
    if ranges.len() > MAX_RESULT_TABS {
        return Err(StatementPlanError::TooMany);
    }
    Ok(ranges
        .into_iter()
        .enumerate()
        .map(|(index, range)| PlannedStatement {
            label: statement_name(&sql[range.clone()])
                .unwrap_or_else(|| format!("Query {}", index + 1)),
            range,
        })
        .collect())
}

pub fn statement_plan(
    sql: &str,
    within: Range<usize>,
) -> Result<Vec<PlannedStatement>, StatementPlanError> {
    statement_plan_with_mode(sql, within, None)
}

pub fn statement_plan_mysql(
    sql: &str,
    within: Range<usize>,
    mode: crate::MysqlSqlMode,
) -> Result<Vec<PlannedStatement>, StatementPlanError> {
    statement_plan_with_mode(sql, within, Some(mode))
}
