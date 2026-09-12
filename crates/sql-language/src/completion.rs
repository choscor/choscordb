#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum CompletionKind {
    Keyword,
    Schema,
    Table,
    Column,
}
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Completion {
    pub label: String,
    pub insert_text: String,
    pub kind: CompletionKind,
}
/// Metadata is supplied by the caller from its asynchronous metadata cache.
/// This service never connects to a database.
pub const MAX_COMPLETION_RESULTS: usize = 100;
pub const MAX_COMPLETION_PREFIX_BYTES: usize = 256;
pub const MAX_COMPLETION_METADATA_ENTRIES: usize = 10_000;
pub const MAX_COMPLETION_METADATA_BYTES: usize = 8 * 1024 * 1024;
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CompletionError {
    PrefixTooLong,
    InvalidPrefix,
}
impl std::fmt::Display for CompletionError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(match self {
            Self::PrefixTooLong => "Completion prefix exceeds limits",
            Self::InvalidPrefix => "Completion prefix is invalid",
        })
    }
}
impl std::error::Error for CompletionError {}
pub struct CompletionPage {
    pub items: Vec<Completion>,
    pub partial: bool,
}
/// Compatibility wrapper. Invalid input produces no suggestions.
pub fn completions(prefix: &str, metadata: &[Completion], limit: usize) -> Vec<Completion> {
    bounded_completions(prefix, metadata, limit)
        .map(|page| page.items)
        .unwrap_or_default()
}
/// Metadata is borrowed, and only the best 100 candidates are cloned. Insertion
/// text is returned verbatim: callers must replace the entire qualified prefix.
/// `partial` means metadata exceeded cache bounds, not merely the visible limit.
/// Callers suppress completion in SQL strings/comments; no scope/alias inference
/// or database requests occur here.
pub fn bounded_completions(
    prefix: &str,
    metadata: &[Completion],
    limit: usize,
) -> Result<CompletionPage, CompletionError> {
    if prefix.len() > MAX_COMPLETION_PREFIX_BYTES {
        return Err(CompletionError::PrefixTooLong);
    }
    let prefix_parts = segments(prefix, true).ok_or(CompletionError::InvalidPrefix)?;
    let limit = limit.min(MAX_COMPLETION_RESULTS);
    let mut candidates = Vec::<Candidate<'_>>::with_capacity(limit);
    let mut partial = metadata.len() > MAX_COMPLETION_METADATA_ENTRIES;
    let mut used = 0usize;
    for entry in metadata.iter().take(MAX_COMPLETION_METADATA_ENTRIES) {
        let bytes = entry.label.len().saturating_add(entry.insert_text.len());
        if bytes > MAX_COMPLETION_METADATA_BYTES - used {
            partial = true;
            continue;
        }
        used += bytes;
        let matches = if prefix_parts.len() == 1 {
            starts_with_folded(entry.label.chars(), prefix_parts[0].chars())
        } else if let Some(path) = segments(&entry.insert_text, false) {
            path.len() >= prefix_parts.len()
                && path[path.len() - prefix_parts.len()..]
                    .iter()
                    .zip(&prefix_parts)
                    .enumerate()
                    .all(|(i, (value, prefix))| {
                        if i + 1 == prefix_parts.len() {
                            starts_with_folded(value.chars(), prefix.chars())
                        } else {
                            value
                                .chars()
                                .flat_map(char::to_lowercase)
                                .eq(prefix.chars().flat_map(char::to_lowercase))
                        }
                    })
        } else {
            false
        };
        if matches {
            retain(&mut candidates, Candidate::Metadata(entry), limit);
        }
    }
    const KEYWORDS: &[&str] = &[
        "SELECT",
        "FROM",
        "WHERE",
        "INSERT",
        "INTO",
        "VALUES",
        "UPDATE",
        "SET",
        "DELETE",
        "JOIN",
        "LEFT JOIN",
        "INNER JOIN",
        "ON",
        "GROUP BY",
        "ORDER BY",
        "HAVING",
        "LIMIT",
        "OFFSET",
        "WITH",
        "AS",
        "DISTINCT",
        "NULL",
        "IS",
        "NOT",
        "AND",
        "OR",
        "CREATE",
        "TABLE",
        "DROP",
        "ALTER",
        "BEGIN",
        "COMMIT",
        "ROLLBACK",
        "RETURNING",
    ];
    if prefix_parts.len() == 1 && !prefix_parts[0].quoted {
        for keyword in KEYWORDS {
            if starts_with_folded(keyword.chars(), prefix_parts[0].chars()) {
                retain(&mut candidates, Candidate::Keyword(keyword), limit);
            }
        }
    }
    Ok(CompletionPage {
        items: candidates
            .into_iter()
            .map(|candidate| match candidate {
                Candidate::Metadata(item) => item.clone(),
                Candidate::Keyword(word) => Completion {
                    label: word.into(),
                    insert_text: word.into(),
                    kind: CompletionKind::Keyword,
                },
            })
            .collect(),
        partial,
    })
}
#[derive(Clone, Copy)]
enum Candidate<'a> {
    Metadata(&'a Completion),
    Keyword(&'static str),
}
impl<'a> Candidate<'a> {
    fn key(self) -> (&'a str, CompletionKind, &'a str) {
        match self {
            Self::Metadata(item) => (&item.label, item.kind, &item.insert_text),
            Self::Keyword(word) => (word, CompletionKind::Keyword, word),
        }
    }
}
fn retain<'a>(selected: &mut Vec<Candidate<'a>>, candidate: Candidate<'a>, limit: usize) {
    if limit == 0 {
        return;
    }
    match selected.binary_search_by(|item| item.key().cmp(&candidate.key())) {
        Ok(_) => {}
        Err(index) if index < limit => {
            if selected.len() == limit {
                selected.pop();
            }
            selected.insert(index, candidate);
        }
        _ => {}
    }
}
fn starts_with_folded(
    value: impl Iterator<Item = char>,
    prefix: impl Iterator<Item = char>,
) -> bool {
    let mut value = value.flat_map(char::to_lowercase);
    prefix
        .flat_map(char::to_lowercase)
        .all(|ch| value.next() == Some(ch))
}
#[derive(Clone, Copy)]
struct Segment<'a> {
    text: &'a str,
    quoted: bool,
    escape: Option<char>,
}
impl Segment<'_> {
    fn chars(&self) -> impl Iterator<Item = char> + '_ {
        let mut chars = self.text.chars();
        std::iter::from_fn(move || {
            let ch = chars.next()?;
            if self.escape == Some(ch) {
                chars.next();
            }
            Some(ch)
        })
    }
}
// Driver paths contain at most schema.table.column. Borrow raw slices; even a
// large malformed metadata name cannot create a large temporary segment vector.
fn segments(source: &str, prefix: bool) -> Option<Vec<Segment<'_>>> {
    let mut result = Vec::with_capacity(3);
    let mut rest = source;
    loop {
        if result.len() == 3 {
            return None;
        }
        if let Some(open @ ('"' | '`' | '[')) = rest.chars().next() {
            let close = if open == '[' { ']' } else { open };
            let escape = (open != '[').then_some(close);
            let quoted = &rest[1..];
            let mut iter = quoted.char_indices().peekable();
            let mut closing = None;
            while let Some((index, ch)) = iter.next() {
                if ch != close {
                    continue;
                }
                if escape.is_some() && iter.peek().is_some_and(|(_, next)| *next == close) {
                    iter.next();
                    continue;
                }
                closing = Some(index);
                break;
            }
            if let Some(index) = closing {
                result.push(Segment {
                    text: &quoted[..index],
                    quoted: true,
                    escape,
                });
                rest = &quoted[index + 1..];
                if rest.is_empty() {
                    return Some(result);
                }
                rest = rest.strip_prefix('.')?;
            } else if prefix {
                result.push(Segment {
                    text: quoted,
                    quoted: true,
                    escape,
                });
                return Some(result);
            } else {
                return None;
            }
        } else {
            let end = rest.find('.').unwrap_or(rest.len());
            let value = &rest[..end];
            if value
                .chars()
                .any(|ch| ch.is_control() || matches!(ch, '"' | '\'' | '`' | '[' | ']'))
            {
                return None;
            }
            if value.is_empty() && (!prefix || end < rest.len()) {
                return None;
            }
            result.push(Segment {
                text: value,
                quoted: false,
                escape: None,
            });
            if end == rest.len() {
                return Some(result);
            }
            rest = &rest[end + 1..];
        }
    }
}

#[cfg(test)]
mod bounded_tests {
    use super::*;
    fn item(label: &str, path: &str, kind: CompletionKind) -> Completion {
        Completion {
            label: label.into(),
            insert_text: path.into(),
            kind,
        }
    }
    #[test]
    fn qualification_preserves_driver_quoting_and_filters_descendants() {
        let metadata = vec![
            item("Table", "\"My.Schema\".\"Table\"", CompletionKind::Table),
            item(
                "Col",
                "\"My.Schema\".\"Table\".\"Col\"",
                CompletionKind::Column,
            ),
            item("Table", "\"other\".\"Table\"", CompletionKind::Table),
        ];
        let page = bounded_completions("\"My.Schema\".ta", &metadata, 100).unwrap();
        assert_eq!(page.items.len(), 1);
        assert_eq!(page.items[0].insert_text, "\"My.Schema\".\"Table\"");
        let page = bounded_completions("\"My.Schema\".\"Table\".c", &metadata, 100).unwrap();
        assert_eq!(page.items[0].kind, CompletionKind::Column);
    }
    #[test]
    fn unicode_escaped_quotes_suffix_qualification_and_catalog_bound() {
        let metadata = vec![item(
            "列",
            "\"模式\".\"Ta\"\"ble\".\"列\"",
            CompletionKind::Column,
        )];
        let page = bounded_completions("\"Ta\"\"ble\".列", &metadata, 100).unwrap();
        assert_eq!(page.items.len(), 1);
        assert_eq!(page.items[0].insert_text, metadata[0].insert_text);
        assert!(
            bounded_completions("\"sel", &[], 100)
                .unwrap()
                .items
                .is_empty()
        );
        assert!(bounded_completions("a..b", &metadata, 100).is_err());
        assert_eq!(completions("sel", &[], usize::MAX)[0].label, "SELECT");
        let metadata = vec![
            item("duplicate", "\"duplicate\"", CompletionKind::Table);
            MAX_COMPLETION_METADATA_ENTRIES + 1
        ];
        let page = bounded_completions("dup", &metadata, 100).unwrap();
        assert!(page.partial);
        assert_eq!(page.items.len(), 1);
    }
    #[test]
    fn sqlite_bracket_and_backtick_prefixes_match_driver_double_quotes() {
        let metadata = vec![
            item("列", "\"odd name\".\"列\"", CompletionKind::Column),
            item("column", "\"a`b\".\"column\"", CompletionKind::Column),
        ];
        for prefix in ["[odd name].列", "`odd name`.列"] {
            let page = bounded_completions(prefix, &metadata, 100).unwrap();
            assert_eq!(page.items.len(), 1);
            assert_eq!(page.items[0].insert_text, metadata[0].insert_text);
        }
        assert_eq!(
            bounded_completions("`a``b`.co", &metadata, 100)
                .unwrap()
                .items
                .len(),
            1
        );
        assert_eq!(
            bounded_completions(
                "[odd name",
                &[item("odd name", "\"odd name\"", CompletionKind::Table)],
                100
            )
            .unwrap()
            .items
            .len(),
            1
        );
    }
    #[test]
    fn limits_omit_oversized_catalog_entries_and_cap_results() {
        let metadata = (0..200)
            .map(|i| {
                item(
                    &format!("name{i:03}"),
                    &format!("\"name{i:03}\""),
                    CompletionKind::Table,
                )
            })
            .collect::<Vec<_>>();
        let page = bounded_completions("name", &metadata, usize::MAX).unwrap();
        assert_eq!(page.items.len(), 100);
        assert_eq!(page.items[0].label, "name000");
        assert!(bounded_completions(&"x".repeat(257), &[], 100).is_err());
        let metadata = vec![
            item(
                "huge",
                &"x".repeat(MAX_COMPLETION_METADATA_BYTES + 1),
                CompletionKind::Table,
            ),
            item("ok", "\"ok\"", CompletionKind::Table),
        ];
        let page = bounded_completions("ok", &metadata, 100).unwrap();
        assert!(page.partial);
        assert_eq!(page.items.len(), 1);
    }
}
