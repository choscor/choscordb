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
    DocumentTooLarge,
    PatternTooLarge,
    OutputTooLarge,
}
impl fmt::Display for SearchError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(match self {
            Self::EmptyPattern => "Enter text to find.",
            Self::InvalidOffset => "Search offset is invalid",
            Self::ResourceLimit => "Search operation exceeds resource limits",
            Self::DocumentTooLarge => "Search supports documents up to 16 MiB.",
            Self::PatternTooLarge => "Search text exceeds 16 KiB.",
            Self::OutputTooLarge => "Replacement output exceeds 16 MiB.",
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
pub fn pattern_usable(needle: &str) -> bool {
    !needle.is_empty() && needle.len() <= MAX_SEARCH_PATTERN_BYTES
}
pub fn replacement_fits(document: usize, removed: usize, added: usize) -> bool {
    document
        .checked_sub(removed)
        .and_then(|kept| kept.checked_add(added))
        .is_some_and(|size| size <= MAX_SEARCH_SOURCE_BYTES)
}
#[derive(Clone, Debug)]
pub struct TextFinder {
    regex: Regex,
}

impl TextFinder {
    pub fn new(needle: &str, options: SearchOptions) -> Result<Self, SearchError> {
        if needle.len() > MAX_SEARCH_PATTERN_BYTES {
            return Err(SearchError::PatternTooLarge);
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
        let regex = RegexBuilder::new(&pattern)
            .case_insensitive(!options.case_sensitive)
            .unicode(true)
            .size_limit(REGEX_BYTES)
            .dfa_size_limit(REGEX_BYTES)
            .build()
            .map_err(|_| SearchError::ResourceLimit)?;
        Ok(Self { regex })
    }

    pub fn find_all(
        &self,
        source: &str,
        limit: usize,
    ) -> Result<Vec<std::ops::Range<usize>>, SearchError> {
        if source.len() > MAX_SEARCH_SOURCE_BYTES {
            return Err(SearchError::DocumentTooLarge);
        }
        Ok(self
            .regex
            .find_iter(source)
            .take(limit)
            .map(|found| found.range())
            .collect())
    }
}

fn matcher(source: &str, needle: &str, options: SearchOptions) -> Result<Regex, SearchError> {
    if source.len() > MAX_SEARCH_SOURCE_BYTES {
        return Err(SearchError::DocumentTooLarge);
    }
    TextFinder::new(needle, options).map(|finder| finder.regex)
}
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
pub fn find_all(
    source: &str,
    needle: &str,
    options: SearchOptions,
    limit: usize,
) -> Result<Vec<std::ops::Range<usize>>, SearchError> {
    if source.len() > MAX_SEARCH_SOURCE_BYTES {
        return Err(SearchError::DocumentTooLarge);
    }
    TextFinder::new(needle, options)?.find_all(source, limit)
}
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
        .ok_or(SearchError::OutputTooLarge)?;
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
