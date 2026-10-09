//! One-line-per-clause previews of SQL history entries.

/// A history preview of the first `max_chars` characters of `sql`.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct HistoryPreview {
    pub text: String,
    /// Whether `sql` continues past the previewed characters.
    pub truncated: bool,
}

pub fn history_sql_preview(sql: &str, max_chars: usize) -> HistoryPreview {
    let chars: Vec<char> = sql.chars().take(max_chars).collect();
    let truncated = sql.chars().nth(chars.len()).is_some();
    HistoryPreview {
        text: format(&chars),
        truncated,
    }
}

const CLAUSES: [&str; 7] = ["FROM", "WHERE", "JOIN", "GROUP", "ORDER", "LIMIT", "HAVING"];
const KEYWORDS: [&str; 12] = [
    "SELECT", "INSERT", "UPDATE", "DELETE", "INTO", "VALUES", "SET", "AS", "AND", "OR", "BY", "ON",
];

struct Writer {
    text: String,
    pending_space: bool,
}

impl Writer {
    /// Separates the next token from the previous one by one space when the
    /// source had whitespace between them, except at the start of a line.
    fn separate(&mut self) {
        if self.pending_space && !self.text.is_empty() && !self.text.ends_with('\n') {
            self.text.push(' ');
        }
        self.pending_space = false;
    }
    fn push(&mut self, chars: &[char]) {
        self.text.extend(chars);
    }
}

fn identifier_start(ch: char) -> bool {
    ch.is_alphabetic() || ch == '_'
}

fn identifier_part(ch: char) -> bool {
    ch.is_alphanumeric() || ch == '_'
}

fn find(chars: &[char], needle: &[char], from: usize) -> Option<usize> {
    (from..=chars.len().checked_sub(needle.len())?).find(|&i| chars[i..].starts_with(needle))
}

/// The end of a dollar-quoted string starting at `start`, when `start` opens one.
fn dollar_string_end(chars: &[char], start: usize) -> Option<usize> {
    let tag_end = (start + 1..chars.len()).find(|&i| chars[i] == '$')?;
    let tag = &chars[start + 1..tag_end];
    if tag.first().is_some_and(|&first| !identifier_start(first))
        || !tag.iter().all(|&ch| identifier_part(ch))
    {
        return None;
    }
    let delimiter = &chars[start..=tag_end];
    Some(
        find(chars, delimiter, tag_end + 1)
            .map_or(chars.len(), |closing| closing + delimiter.len()),
    )
}

fn format(chars: &[char]) -> String {
    let mut out = Writer {
        text: String::new(),
        pending_space: false,
    };
    let mut i = 0;
    while i < chars.len() {
        let ch = chars[i];
        if ch.is_whitespace() {
            out.pending_space = true;
            i += 1;
            continue;
        }
        if ch == '$'
            && let Some(end) = dollar_string_end(chars, i)
        {
            out.separate();
            out.push(&chars[i..end]);
            i = end;
            continue;
        }
        if chars[i..].starts_with(&['-', '-']) {
            out.separate();
            let end = (i..chars.len())
                .find(|&j| chars[j] == '\n')
                .map_or(chars.len(), |newline| newline + 1);
            out.push(&chars[i..end]);
            i = end;
            continue;
        }
        if chars[i..].starts_with(&['/', '*']) {
            out.separate();
            let end = find(chars, &['*', '/'], i + 2).map_or(chars.len(), |close| close + 2);
            out.push(&chars[i..end]);
            i = end;
            continue;
        }
        if matches!(ch, '\'' | '"' | '`') {
            out.separate();
            let mut j = i + 1;
            while j < chars.len() {
                let current = chars[j];
                j += 1;
                if current == ch {
                    if chars.get(j) == Some(&ch) {
                        j += 1;
                    } else {
                        break;
                    }
                } else if current == '\\' && j < chars.len() {
                    j += 1;
                }
            }
            out.push(&chars[i..j]);
            i = j;
            continue;
        }
        if identifier_start(ch) {
            let start = i;
            i += 1;
            while i < chars.len() && identifier_part(chars[i]) {
                i += 1;
            }
            let word: String = chars[start..i].iter().collect();
            let upper = word.to_uppercase();
            let clause = CLAUSES.contains(&upper.as_str());
            let keyword = clause || KEYWORDS.contains(&upper.as_str());
            if clause && !out.text.is_empty() && !out.text.ends_with('\n') {
                out.text.push('\n');
                out.pending_space = false;
            } else {
                out.separate();
            }
            out.text.push_str(if keyword { &upper } else { &word });
            continue;
        }
        out.separate();
        out.text.push(ch);
        i += 1;
    }
    out.text.trim().to_owned()
}
