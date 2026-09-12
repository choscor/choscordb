use std::ops::Range;
#[derive(Debug)]
pub(crate) struct Token {
    pub(crate) word: String,
    pub(crate) start: usize,
    pub(crate) depth: usize,
}

// A lexical pass complements the grammar for incomplete editor buffers and
// PostgreSQL dollar strings, including nested block comments.
pub(crate) fn scan(sql: &str) -> (Vec<Token>, Vec<usize>, bool) {
    let b = sql.as_bytes();
    let (mut i, mut depth) = (0, 0usize);
    let (mut tokens, mut ends) = (Vec::new(), Vec::new());
    let mut valid = true;
    while i < b.len() {
        let start = i;
        if b[i..].starts_with(b"--") {
            while i < b.len() && b[i] != b'\n' {
                i += 1;
            }
        } else if b[i..].starts_with(b"/*") {
            i += 2;
            let mut nesting = 1;
            while i < b.len() && nesting > 0 {
                if b[i..].starts_with(b"/*") {
                    nesting += 1;
                    i += 2;
                } else if b[i..].starts_with(b"*/") {
                    nesting -= 1;
                    i += 2;
                } else {
                    i += 1;
                }
            }
            valid &= nesting == 0;
        } else if matches!(b[i], b'\'' | b'"' | b'`' | b'[') {
            let close = if b[i] == b'[' { b']' } else { b[i] };
            let escape = b[i] == b'\''
                && start > 0
                && matches!(b[start - 1], b'e' | b'E')
                && (start < 2 || !b[start - 2].is_ascii_alphanumeric());
            i += 1;
            let mut closed = false;
            while i < b.len() {
                if escape && b[i] == b'\\' {
                    i = (i + 2).min(b.len());
                } else if b[i] == close {
                    i += 1;
                    if i < b.len() && b[i] == close {
                        i += 1;
                    } else {
                        closed = true;
                        break;
                    }
                } else {
                    i += 1;
                }
            }
            valid &= closed;
        } else if b[i] == b'$' && {
            let mut j = i + 1;
            if j < b.len() && (b[j].is_ascii_alphabetic() || b[j] == b'_') {
                while j < b.len() && (b[j].is_ascii_alphanumeric() || b[j] == b'_') {
                    j += 1;
                }
            }
            j < b.len() && b[j] == b'$'
        } {
            let mut j = i + 1;
            while b[j] != b'$' {
                j += 1;
            }
            let delimiter = &sql[i..=j];
            if let Some(offset) = sql[j + 1..].find(delimiter) {
                i = j + 1 + offset + delimiter.len();
            } else {
                valid = false;
                i = b.len();
            }
        } else if b[i].is_ascii_alphabetic() || b[i] == b'_' {
            i += 1;
            while i < b.len()
                && (b[i].is_ascii_alphanumeric() || b[i] == b'_' || b[i] == b'$' || b[i] >= 128)
            {
                i += 1;
            }
            tokens.push(Token {
                word: sql[start..i].to_ascii_uppercase(),
                start,
                depth,
            });
        } else {
            match b[i] {
                b'(' => depth += 1,
                b')' => {
                    tokens.push(Token {
                        word: ")".into(),
                        start,
                        depth,
                    });
                    valid &= depth > 0;
                    depth = depth.saturating_sub(1);
                }
                b';' if depth == 0 => ends.push(i + 1),
                _ => {}
            }
            i += 1;
        }
    }
    (tokens, ends, valid && depth == 0)
}

pub fn statement_ranges(sql: &str) -> Vec<Range<usize>> {
    let (tokens, mut ends, _) = scan(sql);
    if ends.last().copied() != Some(sql.len()) {
        ends.push(sql.len());
    }
    let mut start = 0;
    let mut ranges = Vec::new();
    for end in ends {
        let text = &sql[start..end];
        let left = start + text.len() - text.trim_start().len();
        let right = end - (text.len() - text.trim_end().len());
        if tokens.iter().any(|t| t.start >= left && t.start < right) {
            ranges.push(left..right);
        }
        start = end;
    }
    ranges
}
