//! Bounded literal UTF-8 search. Returned offsets always index the original source.
use regex::{Regex, RegexBuilder};
use std::fmt;
pub const MAX_SEARCH_SOURCE_BYTES: usize = 16 * 1024 * 1024;
pub const MAX_SEARCH_PATTERN_BYTES: usize = 16 * 1024;
const REGEX_BYTES: usize = 2 * 1024 * 1024;
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct SearchOptions {
    pub case_sensitive: bool,
    pub whole_word: bool,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct SearchMatch {
    pub start: usize,
    pub end: usize,
    pub wrapped: bool,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SearchError {
    EmptyPattern,
    InvalidOffset,
    ResourceLimit,
}
impl fmt::Display for SearchError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(match self {
            Self::EmptyPattern => "Search text is empty",
            Self::InvalidOffset => "Search offset is invalid",
            Self::ResourceLimit => "Search operation exceeds resource limits",
        })
    }
}
impl std::error::Error for SearchError {}
pub struct Replacement {
    pub text: String,
    pub count: u64,
}
impl fmt::Debug for Replacement {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("Replacement")
            .field("bytes", &self.text.len())
            .field("count", &self.count)
            .finish()
    }
}
fn matcher(source: &str, needle: &str, options: SearchOptions) -> Result<Regex, SearchError> {
    if source.len() > MAX_SEARCH_SOURCE_BYTES || needle.len() > MAX_SEARCH_PATTERN_BYTES {
        return Err(SearchError::ResourceLimit);
    }
    if needle.is_empty() {
        return Err(SearchError::EmptyPattern);
    }
    let literal = regex::escape(needle);
    let pattern = if options.whole_word {
        format!(r"\b(?:{literal})\b")
    } else {
        literal
    };
    RegexBuilder::new(&pattern)
        .case_insensitive(!options.case_sensitive)
        .unicode(true)
        .size_limit(REGEX_BYTES)
        .dfa_size_limit(REGEX_BYTES)
        .build()
        .map_err(|_| SearchError::ResourceLimit)
}
/// Forward starts at `start_byte` inclusively. Backward searches for a match
/// ending at or before it. Both directions wrap once; backward includes
/// overlapping occurrences, so the nearest preceding literal wins.
pub fn find(
    source: &str,
    needle: &str,
    start_byte: usize,
    backwards: bool,
    options: SearchOptions,
) -> Result<Option<SearchMatch>, SearchError> {
    if !source.is_char_boundary(start_byte) {
        return Err(SearchError::InvalidOffset);
    }
    let regex = matcher(source, needle, options)?;
    if !backwards {
        if let Some(m) = regex.find_at(source, start_byte) {
            return Ok(Some(SearchMatch {
                start: m.start(),
                end: m.end(),
                wrapped: false,
            }));
        }
        return Ok(regex.find(source).map(|m| SearchMatch {
            start: m.start(),
            end: m.end(),
            wrapped: true,
        }));
    }
    let mut previous = None;
    let mut last = None;
    for m in regex.find_iter(source) {
        let found = SearchMatch {
            start: m.start(),
            end: m.end(),
            wrapped: false,
        };
        if m.end() <= start_byte {
            previous = Some(found);
        }
        last = Some(found);
    }
    let Some(mut found) = previous.or(last) else {
        return Ok(None);
    };
    let limit = if previous.is_some() {
        start_byte
    } else {
        source.len()
    };
    found.wrapped = previous.is_none();
    // find_iter skips overlaps. Only overlaps after its final eligible match
    // can improve that answer, so refine this bounded tail rather than testing
    // every overlapping occurrence throughout a large repetitive document.
    let mut position = found.start + source[found.start..].chars().next().unwrap().len_utf8();
    while position < limit {
        let Some(m) = regex.find_at(source, position) else {
            break;
        };
        if m.end() > limit {
            break;
        }
        found.start = m.start();
        found.end = m.end();
        position = m.start() + source[m.start()..].chars().next().unwrap().len_utf8();
    }
    Ok(Some(found))
}
/// Replaces nonoverlapping matches in the immutable input. Preflights the exact
/// output length before allocation and never stores a match vector or rescans
/// replacement text. Replacement syntax is entirely literal (including `$`).
pub fn replace_all(
    source: &str,
    needle: &str,
    replacement: &str,
    options: SearchOptions,
) -> Result<Replacement, SearchError> {
    let regex = matcher(source, needle, options)?;
    let mut count = 0usize;
    let mut removed = 0usize;
    for m in regex.find_iter(source) {
        count += 1;
        removed += m.len();
    }
    let size = count
        .checked_mul(replacement.len())
        .and_then(|added| (source.len() - removed).checked_add(added))
        .filter(|size| *size <= MAX_SEARCH_SOURCE_BYTES)
        .ok_or(SearchError::ResourceLimit)?;
    let mut text = String::with_capacity(size);
    let mut previous = 0;
    for m in regex.find_iter(source) {
        text.push_str(&source[previous..m.start()]);
        text.push_str(replacement);
        previous = m.end();
    }
    text.push_str(&source[previous..]);
    Ok(Replacement {
        text,
        count: count as u64,
    })
}
