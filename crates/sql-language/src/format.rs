use crate::{ExecutionScope, MysqlSqlMode, editor::execution_range_with_mode};
use std::ops::Range;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct FormatOptions {
    pub indent: u8,
    pub uppercase_keywords: bool,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum FormatStyle {
    Pretty,
    Minify,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TextEdit {
    pub range: Range<usize>,
    pub text: String,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum EditError {
    NoStatement,
    InvalidRange,
    Unterminated,
    NestedBlockComment,
    InvalidOptions,
}

impl std::fmt::Display for EditError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter.write_str(match self {
            Self::NoStatement => "No SQL statement at the cursor.",
            Self::InvalidRange => "The selection is not a valid text range.",
            Self::Unterminated => {
                "Cannot format SQL that has an unterminated string, identifier or comment."
            }
            Self::InvalidOptions => "The SQL format options are not valid.",
            Self::NestedBlockComment => {
                "Cannot put a block comment around text that contains \"*/\"."
            }
        })
    }
}

impl std::error::Error for EditError {}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum Kind {
    Word,
    Literal,
    Number,
    LineComment,
    BlockComment,
    Open,
    Close,
    Comma,
    Semicolon,
    Dot,
    Operator,
}

#[derive(Clone, Debug)]
pub(crate) struct Token<'a> {
    kind: Kind,
    text: &'a str,
    start: usize,
    newline_before: bool,
}

const RESERVED: &[&str] = &[
    "ADD",
    "ALL",
    "ALTER",
    "AND",
    "ANY",
    "AS",
    "ASC",
    "BETWEEN",
    "BY",
    "CASE",
    "CHECK",
    "COLUMN",
    "CONSTRAINT",
    "CREATE",
    "CROSS",
    "DEFAULT",
    "DELETE",
    "DESC",
    "DISTINCT",
    "DROP",
    "ELSE",
    "END",
    "EXCEPT",
    "EXISTS",
    "FALSE",
    "FOREIGN",
    "FROM",
    "FULL",
    "GROUP",
    "HAVING",
    "IN",
    "INDEX",
    "INNER",
    "INSERT",
    "INTERSECT",
    "INTO",
    "IS",
    "JOIN",
    "KEY",
    "LEFT",
    "LIKE",
    "LIMIT",
    "NATURAL",
    "NOT",
    "NULL",
    "OFFSET",
    "ON",
    "OR",
    "ORDER",
    "OUTER",
    "PRIMARY",
    "REFERENCES",
    "RETURNING",
    "RIGHT",
    "SELECT",
    "SET",
    "TABLE",
    "THEN",
    "TRUE",
    "UNION",
    "UNIQUE",
    "UPDATE",
    "USING",
    "VALUES",
    "VIEW",
    "WHEN",
    "WHERE",
    "WITH",
];

const CLAUSES: &[&[&str]] = &[
    &["SELECT"],
    &["FROM"],
    &["WHERE"],
    &["GROUP", "BY"],
    &["ORDER", "BY"],
    &["HAVING"],
    &["LIMIT"],
    &["OFFSET"],
    &["UNION", "ALL"],
    &["UNION"],
    &["INTERSECT"],
    &["EXCEPT"],
    &["VALUES"],
    &["SET"],
    &["RETURNING"],
    &["INSERT", "INTO"],
    &["UPDATE"],
    &["DELETE", "FROM"],
    &["WITH"],
];

const JOINS: &[&[&str]] = &[
    &["LEFT", "OUTER", "JOIN"],
    &["RIGHT", "OUTER", "JOIN"],
    &["FULL", "OUTER", "JOIN"],
    &["LEFT", "JOIN"],
    &["RIGHT", "JOIN"],
    &["FULL", "JOIN"],
    &["INNER", "JOIN"],
    &["CROSS", "JOIN"],
    &["NATURAL", "JOIN"],
    &["JOIN"],
];

const LIST_CLAUSES: &[&str] = &["SELECT", "GROUP", "ORDER", "SET", "RETURNING"];
const CONDITION_CLAUSES: &[&str] = &["WHERE", "HAVING"];

fn word_byte(byte: u8) -> bool {
    byte.is_ascii_alphanumeric() || byte == b'_' || byte == b'$' || byte >= 0x80
}

pub(crate) fn lex(sql: &str, mode: Option<MysqlSqlMode>) -> Result<Vec<Token<'_>>, EditError> {
    let mysql = mode.is_some();
    let b = sql.as_bytes();
    let mut tokens = Vec::new();
    let mut i = 0;
    let mut newline = false;
    while i < b.len() {
        let start = i;
        let byte = b[i];
        let kind = if byte.is_ascii_whitespace() {
            newline |= byte == b'\n';
            i += 1;
            continue;
        } else if (b[i..].starts_with(b"--")
            && (!mysql || b.get(i + 2).is_none_or(|c| c.is_ascii_whitespace())))
            || (mysql && byte == b'#')
        {
            while i < b.len() && b[i] != b'\n' {
                i += 1;
            }
            Kind::LineComment
        } else if b[i..].starts_with(b"/*") {
            i += 2;
            let mut nesting = 1;
            while i < b.len() && nesting > 0 {
                if !mysql && b[i..].starts_with(b"/*") {
                    nesting += 1;
                    i += 2;
                } else if b[i..].starts_with(b"*/") {
                    nesting -= 1;
                    i += 2;
                } else {
                    i += 1;
                }
            }
            if nesting > 0 {
                return Err(EditError::Unterminated);
            }
            Kind::BlockComment
        } else if matches!(byte, b'\'' | b'"' | b'`') || (!mysql && byte == b'[') {
            i = quoted_end(sql, i, mode)?;
            Kind::Literal
        } else if !mysql && byte == b'$' && dollar_tag(b, i).is_some() {
            let tag_end = dollar_tag(b, i).unwrap_or(i);
            let delimiter = &sql[i..=tag_end];
            let Some(offset) = sql[tag_end + 1..].find(delimiter) else {
                return Err(EditError::Unterminated);
            };
            i = tag_end + 1 + offset + delimiter.len();
            Kind::Literal
        } else if byte.is_ascii_digit()
            || (byte == b'.' && b.get(i + 1).is_some_and(u8::is_ascii_digit))
        {
            while i < b.len() && (word_byte(b[i]) || b[i] == b'.') {
                if matches!(b[i], b'e' | b'E') && matches!(b.get(i + 1), Some(b'+' | b'-')) {
                    i += 1;
                }
                i += 1;
            }
            Kind::Number
        } else if word_byte(byte) || matches!(byte, b'@' | b':' | b'?') && word_follows(b, i) {
            i += 1;
            while i < b.len() && (word_byte(b[i]) || (b[i] == b'@' && i == start + 1)) {
                i += 1;
            }
            if i < b.len() && b[i] == b'\'' && i > start {
                i = quoted_end(sql, i, mode)?;
                Kind::Literal
            } else {
                Kind::Word
            }
        } else {
            i += 1;
            match byte {
                b'(' => Kind::Open,
                b')' => Kind::Close,
                b',' => Kind::Comma,
                b';' => Kind::Semicolon,
                b'.' => Kind::Dot,
                _ => {
                    while i < b.len()
                        && b"+-*/<>=~!@#%^&|:?".contains(&b[i])
                        && !b[i..].starts_with(b"--")
                        && !b[i..].starts_with(b"/*")
                        && !(mysql && b[i] == b'#')
                    {
                        i += 1;
                    }
                    if !sql.is_char_boundary(i) {
                        while !sql.is_char_boundary(i) {
                            i += 1;
                        }
                    }
                    Kind::Operator
                }
            }
        };
        tokens.push(Token {
            kind,
            text: &sql[start..i],
            start,
            newline_before: std::mem::take(&mut newline),
        });
    }
    Ok(tokens)
}

fn word_follows(b: &[u8], i: usize) -> bool {
    b.get(i + 1)
        .is_some_and(|next| word_byte(*next) || (b[i] == b'@' && *next == b'@'))
        || b[i] == b'?'
}

fn dollar_tag(b: &[u8], i: usize) -> Option<usize> {
    let mut j = i + 1;
    if j < b.len() && (b[j].is_ascii_alphabetic() || b[j] == b'_') {
        while j < b.len() && (b[j].is_ascii_alphanumeric() || b[j] == b'_') {
            j += 1;
        }
    }
    (j < b.len() && b[j] == b'$').then_some(j)
}

fn quoted_end(sql: &str, open: usize, mode: Option<MysqlSqlMode>) -> Result<usize, EditError> {
    let b = sql.as_bytes();
    let close = if b[open] == b'[' { b']' } else { b[open] };
    let escape = mode.is_some_and(|mode| {
        !mode.no_backslash_escapes && (b[open] == b'\'' || (b[open] == b'"' && !mode.ansi_quotes))
    }) || (mode.is_none()
        && b[open] == b'\''
        && open > 0
        && matches!(b[open - 1], b'e' | b'E')
        && (open < 2 || !b[open - 2].is_ascii_alphanumeric()));
    let mut i = open + 1;
    while i < b.len() {
        if escape && b[i] == b'\\' {
            i = (i + 2).min(b.len());
        } else if b[i] == close {
            i += 1;
            if i < b.len() && b[i] == close && close != b']' {
                i += 1;
            } else {
                return Ok(i);
            }
        } else {
            i += 1;
        }
    }
    Err(EditError::Unterminated)
}

fn upper(token: &Token<'_>) -> Option<String> {
    (token.kind == Kind::Word)
        .then(|| token.text.to_ascii_uppercase())
        .filter(|word| RESERVED.contains(&word.as_str()))
}

fn matches_words(tokens: &[Token<'_>], at: usize, words: &[&str]) -> bool {
    words
        .iter()
        .enumerate()
        .all(|(offset, word)| tokens.get(at + offset).and_then(upper).as_deref() == Some(*word))
}

struct Writer<'a> {
    output: String,
    options: FormatOptions,
    style: FormatStyle,
    previous: Option<Kind>,
    previous_text: &'a str,
    line_start: bool,
    depth: usize,
}

impl<'a> Writer<'a> {
    fn newline(&mut self, depth: usize) {
        if self.style == FormatStyle::Minify {
            return;
        }
        while self.output.ends_with(' ') {
            self.output.pop();
        }
        if !self.output.is_empty() && !self.line_start {
            self.output.push('\n');
        }
        self.depth = depth;
        self.output
            .push_str(&" ".repeat(depth * usize::from(self.options.indent)));
        self.line_start = true;
    }

    fn word_text(&self, token: &Token<'a>) -> String {
        match upper(token) {
            Some(word) if self.options.uppercase_keywords => word,
            _ => token.text.to_string(),
        }
    }

    fn push(&mut self, token: &Token<'a>) {
        let tight = match (self.previous, token.kind) {
            (None, _) => true,
            _ if self.line_start => true,
            (_, Kind::Comma | Kind::Semicolon | Kind::Close | Kind::Dot) => true,
            (Some(Kind::Open | Kind::Dot), _) => true,
            (Some(Kind::Word), Kind::Open) => upper_text(self.previous_text).is_none(),
            (Some(Kind::Operator), _) if self.previous_text == "::" => true,
            (_, Kind::Operator) if token.text == "::" => true,
            _ => false,
        };
        if !tight {
            self.output.push(' ');
        }
        let text = if token.kind == Kind::Word {
            self.word_text(token)
        } else {
            token.text.to_string()
        };
        self.output.push_str(&text);
        self.previous = Some(token.kind);
        self.previous_text = token.text;
        self.line_start = false;
        if token.kind == Kind::LineComment {
            self.output.push('\n');
            self.line_start = true;
            self.previous = None;
            if self.style == FormatStyle::Pretty {
                self.newline(self.depth);
            }
        }
    }
}

fn upper_text(text: &str) -> Option<&'static str> {
    RESERVED
        .iter()
        .find(|word| word.eq_ignore_ascii_case(text))
        .copied()
}

struct Frame {
    base: usize,
    clause: Option<&'static str>,
    subquery: bool,
}

fn render(tokens: &[Token<'_>], options: FormatOptions, style: FormatStyle) -> String {
    let mut writer = Writer {
        output: String::new(),
        options,
        style,
        previous: None,
        previous_text: "",
        line_start: true,
        depth: 0,
    };
    let mut frames = vec![Frame {
        base: 0,
        clause: None,
        subquery: true,
    }];
    let mut i = 0;
    while i < tokens.len() {
        let token = &tokens[i];
        let frame = frames.last_mut().expect("root frame");
        if frame.subquery
            && let Some(words) = CLAUSES.iter().find(|words| matches_words(tokens, i, words))
        {
            let base = frame.base;
            frame.clause = Some(words[0]);
            writer.newline(base);
            for offset in 0..words.len() {
                writer.push(&tokens[i + offset]);
            }
            i += words.len();
            writer.newline(base + 1);
            continue;
        }
        if frame.subquery
            && let Some(words) = JOINS.iter().find(|words| matches_words(tokens, i, words))
        {
            let base = frame.base;
            frame.clause = Some("JOIN");
            writer.newline(base);
            for offset in 0..words.len() {
                writer.push(&tokens[i + offset]);
            }
            i += words.len();
            continue;
        }
        match token.kind {
            Kind::Open => {
                let subquery = tokens
                    .get(i + 1)
                    .and_then(upper)
                    .is_some_and(|word| word == "SELECT" || word == "WITH");
                writer.push(token);
                let base = if subquery { frame.base + 2 } else { frame.base };
                frames.push(Frame {
                    base,
                    clause: None,
                    subquery,
                });
            }
            Kind::Close => {
                if frames.len() > 1 {
                    let closed = frames.pop().expect("nested frame");
                    if closed.subquery {
                        writer.newline(closed.base - 1);
                    }
                }
                writer.push(token);
            }
            Kind::Comma => {
                writer.push(token);
                let frame = frames.last().expect("frame");
                if frame.subquery
                    && frame
                        .clause
                        .is_some_and(|clause| LIST_CLAUSES.contains(&clause))
                {
                    writer.newline(frame.base + 1);
                }
            }
            Kind::Semicolon => {
                writer.push(token);
                frames.truncate(1);
                frames[0].clause = None;
                if i + 1 < tokens.len() && style == FormatStyle::Pretty {
                    writer.output.push_str("\n\n");
                    writer.line_start = true;
                }
            }
            Kind::LineComment | Kind::BlockComment if token.newline_before => {
                let base = frames.last().expect("frame").base;
                if style == FormatStyle::Pretty && !writer.line_start {
                    writer.newline(base);
                }
                writer.push(token);
            }
            _ => {
                let condition = upper(token).is_some_and(|word| word == "AND" || word == "OR")
                    && frame.subquery
                    && frame
                        .clause
                        .is_some_and(|clause| CONDITION_CLAUSES.contains(&clause));
                if condition {
                    let base = frame.base;
                    writer.newline(base + 1);
                }
                writer.push(token);
            }
        }
        i += 1;
    }
    let mut output = writer.output;
    while output.ends_with([' ', '\n']) {
        output.pop();
    }
    output
}

pub fn format_sql(
    sql: &str,
    mode: Option<MysqlSqlMode>,
    options: FormatOptions,
    style: FormatStyle,
) -> Result<String, EditError> {
    let tokens = lex(sql, mode)?;
    Ok(render(&tokens, options, style))
}

fn checked_range(sql: &str, start: usize, end: usize) -> Result<Range<usize>, EditError> {
    let (start, end) = (start.min(end), start.max(end));
    if end > sql.len() || !sql.is_char_boundary(start) || !sql.is_char_boundary(end) {
        return Err(EditError::InvalidRange);
    }
    Ok(start..end)
}

pub fn format_edit(
    sql: &str,
    cursor: usize,
    selection: Range<usize>,
    mode: Option<MysqlSqlMode>,
    options: FormatOptions,
    style: FormatStyle,
) -> Result<TextEdit, EditError> {
    let selection = checked_range(sql, selection.start, selection.end)?;
    checked_range(sql, cursor, cursor)?;
    let range = execution_range_with_mode(
        sql,
        cursor,
        Some(selection).filter(|range| !range.is_empty()),
        ExecutionScope::Current,
        mode,
    )
    .ok_or(EditError::NoStatement)?;
    let text = format_sql(&sql[range.clone()], mode, options, style)?;
    Ok(TextEdit { range, text })
}

fn line_range(sql: &str, selection: Range<usize>) -> Range<usize> {
    let start = sql[..selection.start]
        .rfind('\n')
        .map_or(0, |index| index + 1);
    let last = if selection.end > selection.start && sql[..selection.end].ends_with('\n') {
        selection.end - 1
    } else {
        selection.end
    };
    let end = sql[last..]
        .find('\n')
        .map_or(sql.len(), |index| last + index);
    start..end
}

pub fn toggle_line_comment(sql: &str, selection: Range<usize>) -> Result<TextEdit, EditError> {
    let selection = checked_range(sql, selection.start, selection.end)?;
    let range = line_range(sql, selection);
    let block = &sql[range.clone()];
    let lines = block.split('\n').collect::<Vec<_>>();
    let filled = lines
        .iter()
        .filter(|line| !line.trim().is_empty())
        .collect::<Vec<_>>();
    let commented = !filled.is_empty()
        && filled
            .iter()
            .all(|line| line.trim_start().starts_with("--"));
    let indent = filled
        .iter()
        .map(|line| line.len() - line.trim_start().len())
        .min()
        .unwrap_or(0);
    let text = lines
        .iter()
        .map(|line| {
            if line.trim().is_empty() {
                return (*line).to_string();
            }
            if commented {
                let lead = line.len() - line.trim_start().len();
                let rest = &line[lead + 2..];
                let rest = rest.strip_prefix(' ').unwrap_or(rest);
                format!("{}{rest}", &line[..lead])
            } else {
                format!("{}-- {}", &line[..indent], &line[indent..])
            }
        })
        .collect::<Vec<_>>()
        .join("\n");
    Ok(TextEdit { range, text })
}

pub fn toggle_block_comment(sql: &str, selection: Range<usize>) -> Result<TextEdit, EditError> {
    let selection = checked_range(sql, selection.start, selection.end)?;
    let range = if selection.is_empty() {
        let line = line_range(sql, selection);
        let text = &sql[line.clone()];
        let lead = text.len() - text.trim_start().len();
        let trail = text.len() - text.trim_end().len();
        line.start + lead..line.end - trail
    } else {
        selection
    };
    let text = &sql[range.clone()];
    if let Some(inner) = text
        .strip_prefix("/*")
        .and_then(|rest| rest.strip_suffix("*/"))
    {
        let inner = inner.strip_prefix(' ').unwrap_or(inner);
        let inner = inner.strip_suffix(' ').unwrap_or(inner);
        return Ok(TextEdit {
            range,
            text: inner.to_string(),
        });
    }
    if text.contains("*/") {
        return Err(EditError::NestedBlockComment);
    }
    Ok(TextEdit {
        range,
        text: format!("/* {text} */"),
    })
}

pub const ROW_LIMIT_CHOICES: [u32; 9] = [0, 100, 300, 500, 1_000, 5_000, 10_000, 50_000, 100_000];

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct RowLimit {
    pub sql: String,
    pub applied: Option<u32>,
}

fn significant(token: &Token<'_>) -> bool {
    !matches!(token.kind, Kind::LineComment | Kind::BlockComment)
}

pub fn apply_row_limit(sql: &str, limit: u32, mode: Option<MysqlSqlMode>) -> RowLimit {
    let unchanged = || RowLimit {
        sql: sql.to_string(),
        applied: None,
    };
    if limit == 0 {
        return unchanged();
    }
    let Ok(tokens) = lex(sql, mode) else {
        return unchanged();
    };
    let mut depth = 0usize;
    let mut top = Vec::new();
    for (index, token) in tokens.iter().enumerate() {
        match token.kind {
            Kind::Open => depth += 1,
            Kind::Close => depth = depth.saturating_sub(1),
            _ if depth == 0 && significant(token) => top.push(index),
            _ => {}
        }
    }
    let words = top
        .iter()
        .map(|index| (*index, upper_word(&tokens[*index])))
        .collect::<Vec<_>>();
    let terminator = words
        .iter()
        .position(|(index, _)| tokens[*index].kind == Kind::Semicolon);
    if terminator.is_some_and(|position| position + 1 < words.len()) {
        return unchanged();
    }
    let statement = &words[..terminator.unwrap_or(words.len())];
    let first = statement.first().map(|(_, word)| word.as_str());
    let words_anywhere = tokens
        .iter()
        .filter(|token| token.kind == Kind::Word)
        .map(|token| token.text.to_ascii_uppercase())
        .collect::<Vec<_>>();
    let writes = words_anywhere.iter().enumerate().any(|(index, word)| {
        matches!(word.as_str(), "INSERT" | "DELETE" | "MERGE")
            || (word == "UPDATE"
                && !index.checked_sub(1).is_some_and(|previous| {
                    matches!(words_anywhere[previous].as_str(), "FOR" | "KEY")
                }))
    });
    if !matches!(first, Some("SELECT" | "WITH"))
        || writes
        || statement
            .iter()
            .any(|(_, word)| matches!(word.as_str(), "INTO" | "FETCH"))
    {
        return unchanged();
    }
    if let Some(position) = statement.iter().position(|(_, word)| word == "LIMIT") {
        let mut count = statement.get(position + 1).map(|(index, _)| *index);
        if let (Some((_, comma)), Some((after, _))) =
            (statement.get(position + 2), statement.get(position + 3))
            && comma == ","
        {
            count = Some(*after);
        }
        let Some(token) = count.map(|index| &tokens[index]) else {
            return unchanged();
        };
        return match token.text.parse::<u64>() {
            Ok(value) if token.kind == Kind::Number && value > u64::from(limit) => RowLimit {
                sql: format!(
                    "{}{limit}{}",
                    &sql[..token.start],
                    &sql[token.start + token.text.len()..]
                ),
                applied: Some(limit),
            },
            _ => unchanged(),
        };
    }
    let before = statement
        .iter()
        .find(|(_, word)| matches!(word.as_str(), "OFFSET" | "FOR" | "LOCK"))
        .map(|(index, _)| tokens[*index].start);
    let sql = match before {
        Some(at) => format!("{}LIMIT {limit} {}", &sql[..at], &sql[at..]),
        None => {
            let Some((last, _)) = statement.last() else {
                return unchanged();
            };
            let end = tokens[*last].start + tokens[*last].text.len();
            format!("{} LIMIT {limit}{}", &sql[..end], &sql[end..])
        }
    };
    RowLimit {
        sql,
        applied: Some(limit),
    }
}

fn upper_word(token: &Token<'_>) -> String {
    match token.kind {
        Kind::Word => token.text.to_ascii_uppercase(),
        _ => token.text.to_string(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const OPTIONS: FormatOptions = FormatOptions {
        indent: 2,
        uppercase_keywords: true,
    };

    const SAMPLES: &[&str] = &[
        "select a,b from t where x=1 and (y=2 or z=-1) order by a",
        "insert into t (a, b) values (1, 'x'), (2, E'y\\'z') returning id",
        "update t set a = a+1, b = $1 where id in (select id from u where n like 'a%');",
        "with c as (select 1 as n) select n::text from c union all select 2 -- tail\n;",
        "select count(*), coalesce(a,0)/*x*/from t group by a having count(*)>1 limit 5 offset 2",
        "select @v := 1, @@global.x, ? , :name from dual",
    ];

    fn texts(sql: &str, mode: Option<MysqlSqlMode>) -> Vec<String> {
        lex(sql, mode)
            .unwrap()
            .into_iter()
            .map(|token| match token.kind {
                Kind::Word => token.text.to_ascii_uppercase(),
                _ => token.text.to_string(),
            })
            .collect()
    }

    #[test]
    fn formatting_changes_only_whitespace_and_keyword_case() {
        for mode in [None, Some(MysqlSqlMode::default())] {
            for sql in SAMPLES {
                for style in [FormatStyle::Pretty, FormatStyle::Minify] {
                    let formatted = format_sql(sql, mode, OPTIONS, style).unwrap();
                    assert_eq!(texts(&formatted, mode), texts(sql, mode), "{formatted}");
                    assert_eq!(
                        format_sql(&formatted, mode, OPTIONS, style).unwrap(),
                        formatted
                    );
                }
            }
        }
    }
}
