//! Read-only SQL highlighting spans, such as for inspected object DDL.
use std::ops::Range;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum HighlightKind {
    Keyword,
    String,
    Identifier,
    Comment,
    Number,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct HighlightSpan {
    pub range: Range<usize>,
    pub kind: HighlightKind,
}

const KEYWORDS: &[&str] = &[
    "ALTER",
    "AS",
    "BIGINT",
    "BOOLEAN",
    "CHECK",
    "CONSTRAINT",
    "CREATE",
    "DEFAULT",
    "DELETE",
    "DROP",
    "FALSE",
    "FOREIGN",
    "FROM",
    "INDEX",
    "INSERT",
    "INTEGER",
    "INTO",
    "KEY",
    "NOT",
    "NULL",
    "ON",
    "PRIMARY",
    "REFERENCES",
    "SELECT",
    "TABLE",
    "TEXT",
    "TIMESTAMP",
    "TRUE",
    "UNIQUE",
    "UPDATE",
    "VIEW",
    "WHERE",
];

fn word_byte(byte: u8) -> bool {
    byte.is_ascii_alphanumeric() || byte == b'_' || byte >= 0x80
}

/// Spans in UTF-8 byte offsets, in source order. Quoted text and comments hide
/// the keywords and numbers inside them; an unterminated segment runs to the end.
pub fn highlight(sql: &str) -> Vec<HighlightSpan> {
    let bytes = sql.as_bytes();
    let mut spans = Vec::new();
    let mut push = |range: Range<usize>, kind| spans.push(HighlightSpan { range, kind });
    let mut i = 0;
    while i < bytes.len() {
        let start = i;
        let rest = &bytes[i..];
        if rest.starts_with(b"--") {
            i = sql[i..].find('\n').map_or(bytes.len(), |end| i + end);
            push(start..i, HighlightKind::Comment);
        } else if rest.starts_with(b"/*") {
            i = sql[i + 2..]
                .find("*/")
                .map_or(bytes.len(), |end| i + 2 + end + 2);
            push(start..i, HighlightKind::Comment);
        } else if let quote @ (b'\'' | b'"' | b'`') = bytes[i] {
            i += 1;
            loop {
                match bytes[i..].iter().position(|&byte| byte == quote) {
                    None => {
                        i = bytes.len();
                        break;
                    }
                    Some(offset) => {
                        i += offset + 1;
                        // A doubled quote is an escaped quote inside the segment.
                        if bytes.get(i) != Some(&quote) {
                            break;
                        }
                        i += 1;
                    }
                }
            }
            let kind = if quote == b'\'' {
                HighlightKind::String
            } else {
                HighlightKind::Identifier
            };
            push(start..i, kind);
        } else if word_byte(bytes[i]) {
            while i < bytes.len() && word_byte(bytes[i]) {
                i += 1;
            }
            let word = &sql[start..i];
            if word.bytes().all(|byte| byte.is_ascii_digit()) {
                let fraction =
                    bytes.get(i) == Some(&b'.') && bytes.get(i + 1).is_some_and(u8::is_ascii_digit);
                if fraction {
                    let end = i + 1 + bytes[i + 1..].iter().take_while(|b| word_byte(**b)).count();
                    if sql[i + 1..end].bytes().all(|byte| byte.is_ascii_digit()) {
                        i = end;
                    }
                }
                push(start..i, HighlightKind::Number);
            } else if KEYWORDS
                .iter()
                .any(|keyword| keyword.eq_ignore_ascii_case(word))
            {
                push(start..i, HighlightKind::Keyword);
            }
        } else {
            i += sql[i..].chars().next().map_or(1, char::len_utf8);
        }
    }
    spans
}
