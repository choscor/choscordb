//! Name ranking for the quick-search palette and navigator object search.

const MAX_QUERY_CHARS: usize = 128;
const MAX_NAME_CHARS: usize = 1024;

/// A query folded once so it can be scored against many candidate names.
#[derive(Clone, Debug, Default)]
pub struct QuickSearchNeedle {
    /// The trimmed query is empty, so the palette lists destinations unranked.
    blank: bool,
    /// The query exceeds the searchable length, so nothing can match it.
    oversized: bool,
    folded: Vec<char>,
    words: Vec<Vec<char>>,
}

fn fold(text: &str) -> Vec<char> {
    text.chars().flat_map(char::to_lowercase).collect()
}

fn find(haystack: &[char], needle: &[char], from: usize) -> Option<usize> {
    if needle.is_empty() || from > haystack.len() {
        return None;
    }
    haystack[from..]
        .windows(needle.len())
        .position(|window| window == needle)
        .map(|found| found + from)
}

impl QuickSearchNeedle {
    pub fn new(query: &str) -> Self {
        if query.chars().count() > MAX_QUERY_CHARS {
            return Self {
                oversized: true,
                ..Self::default()
            };
        }
        let blank = query.trim().is_empty();
        let folded = fold(query.trim());
        let words = folded
            .split(|character| !character.is_alphanumeric())
            .filter(|word| !word.is_empty())
            .map(<[char]>::to_vec)
            .collect();
        Self {
            blank,
            oversized: false,
            folded,
            words,
        }
    }

    /// Lower scores rank first; body-text results rank from 200. Returns no
    /// score for empty, oversized, or unmatched input.
    pub fn score(&self, name: &str) -> Option<u32> {
        if self.folded.is_empty() || name.chars().count() > MAX_NAME_CHARS {
            return None;
        }
        let haystack = fold(name);
        let needle = &self.folded;
        if haystack.is_empty() {
            return None;
        }
        let rank = |base: usize, spread: usize, cap: usize| (base + spread.min(cap)) as u32;
        if haystack == *needle {
            return Some(0);
        }
        if haystack.starts_with(needle) {
            return Some(rank(10, (haystack.len() - needle.len()) / 8, 9));
        }
        if let Some(found) = find(&haystack, needle, 0) {
            return Some(rank(25, found / 4, 14));
        }
        if self.words.len() > 1 {
            let mut position = 0;
            let mut gaps = 0;
            let mut first = None;
            let matched = self.words.iter().all(|word| {
                let Some(found) = find(&haystack, word, position) else {
                    return false;
                };
                first.get_or_insert(found);
                gaps += found - position;
                position = found + word.len();
                true
            });
            if matched {
                return Some(rank(45, first.unwrap_or(0) / 4 + gaps / 4, 19));
            }
        }
        let mut position = 0;
        let mut first = None;
        let mut last = 0;
        for &character in needle.iter().filter(|c| c.is_alphanumeric()) {
            let found = haystack[position..].iter().position(|&c| c == character)? + position;
            first.get_or_insert(found);
            last = found;
            position = found + 1;
        }
        let first = first?;
        Some(rank(70, first / 4 + (last - first) / 4, 29))
    }
}

/// Rows the palette shows at once across destinations, editor text and history.
pub const MAX_QUICK_SEARCH_RESULTS: usize = 80;
/// SQL-text matches one quick search collects from open editors.
pub const MAX_QUICK_SEARCH_EDITOR_ROWS: usize = 40;
/// Recently opened objects the palette remembers.
pub const MAX_RECENT_OBJECTS: usize = 12;
/// History rows one quick search requests.
pub const QUICK_SEARCH_HISTORY_ROWS: u32 = 30;

/// What a palette destination opens, which orders it when the query is blank.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DestinationKind {
    OpenTab,
    Command,
    Object,
}

/// One palette destination: an open tab, a command or a database object.
#[derive(Clone, Copy, Debug)]
pub struct Destination<'a> {
    pub kind: DestinationKind,
    pub group: &'a str,
    pub title: &'a str,
    pub context: &'a str,
    pub id: &'a str,
}

/// The rows the palette shows, in order, and whether more were left out.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct QuickSearchPlan {
    pub destinations: Vec<usize>,
    pub editor_rows: usize,
    pub history_rows: usize,
    pub more: bool,
}

impl QuickSearchNeedle {
    /// Order destinations, then fill the remaining rows with editor and history matches.
    pub fn plan(
        &self,
        destinations: &[Destination<'_>],
        editor_rows: usize,
        history_rows: usize,
    ) -> QuickSearchPlan {
        // Objects arrive already found by the navigator search, so they stay listed
        // when only their path matched; tabs and commands must match by title.
        const UNMATCHED_OBJECT: u32 = 99;
        let score = |destination: &Destination<'_>| match (self.blank, destination.kind) {
            (true, DestinationKind::Command) => Some(0),
            (true, DestinationKind::OpenTab) => Some(50),
            (true, DestinationKind::Object) => Some(80),
            (false, DestinationKind::Object) => Some(
                self.score(destination.title)
                    .or_else(|| self.score(destination.context))
                    .unwrap_or(UNMATCHED_OBJECT),
            ),
            (false, _) => self.score(destination.title),
        };
        let mut ranked: Vec<_> = destinations
            .iter()
            .enumerate()
            .filter(|_| !self.oversized)
            .filter_map(|(index, destination)| Some((score(destination)?, destination, index)))
            .collect();
        ranked.sort_by(|(left_score, left, _), (right_score, right, _)| {
            (left_score, left.group, left.title, left.id).cmp(&(
                right_score,
                right.group,
                right.title,
                right.id,
            ))
        });
        let mut remaining = MAX_QUICK_SEARCH_RESULTS;
        let mut take = |available: usize| {
            let taken = available.min(remaining);
            remaining -= taken;
            taken
        };
        let matched = ranked.len();
        let shown = take(matched);
        let (editor, history) = (take(editor_rows), take(history_rows));
        QuickSearchPlan {
            destinations: ranked
                .into_iter()
                .take(shown)
                .map(|(.., index)| index)
                .collect(),
            editor_rows: editor,
            history_rows: history,
            more: matched + editor_rows + history_rows > MAX_QUICK_SEARCH_RESULTS,
        }
    }
}
