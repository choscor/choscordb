use crate::scanner::scan_script;
use std::ops::Range;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ScriptChunk {
    pub statements: Vec<Range<usize>>,
    pub consumed: usize,
    pub delimiter: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ScriptError {
    pub offset: usize,
    pub message: String,
}

fn trimmed(sql: &str, range: Range<usize>) -> Range<usize> {
    let text = &sql[range.clone()];
    range.start..range.end - (text.len() - text.trim_end().len())
}

pub fn split_script(
    sql: &str,
    mode: Option<crate::MysqlSqlMode>,
    delimiter: &str,
    at_end: bool,
) -> Result<ScriptChunk, ScriptError> {
    let mut delimiter = delimiter.to_owned();
    let mut statements = Vec::new();
    let mut position = 0;
    loop {
        let rest = &sql[position..];
        let scan = scan_script(rest, mode, delimiter.as_bytes(), true);
        let mut tokens = scan.tokens.iter().peekable();
        let mut start = 0;
        let mut push = |start: usize, end: usize, statements: &mut Vec<Range<usize>>| {
            let mut first = None;
            while let Some(token) = tokens.next_if(|token| token.start < end) {
                if token.start >= start {
                    first.get_or_insert(token.start);
                }
            }
            if let Some(first) = first {
                statements.push(trimmed(sql, position + first..position + end));
            }
        };
        for &end in &scan.ends {
            push(start, end - delimiter.len(), &mut statements);
            start = end;
        }
        let Some(directive) = scan.directive else {
            if !at_end {
                return Ok(ScriptChunk {
                    statements,
                    consumed: position + start,
                    delimiter,
                });
            }
            push(start, rest.len(), &mut statements);
            return Ok(ScriptChunk {
                statements,
                consumed: sql.len(),
                delimiter,
            });
        };
        let line_end = match rest[directive..].find('\n') {
            Some(offset) => directive + offset,
            None if at_end => rest.len(),
            None => {
                return Ok(ScriptChunk {
                    statements,
                    consumed: position + start,
                    delimiter,
                });
            }
        };
        let argument = rest[directive + "DELIMITER".len()..line_end]
            .split_whitespace()
            .next();
        delimiter = match argument {
            Some(text) => text.to_owned(),
            None => {
                return Err(ScriptError {
                    offset: position + directive,
                    message: "DELIMITER needs a delimiter text.".into(),
                });
            }
        };
        position += (line_end + 1).min(rest.len());
    }
}
