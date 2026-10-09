use crate::ffi;
use choscordb_core::quick_search::{
    Destination, DestinationKind, MAX_QUICK_SEARCH_EDITOR_ROWS, MAX_RECENT_OBJECTS,
    QUICK_SEARCH_HISTORY_ROWS,
};

/// A folded quick-search query kept across one ranking pass.
pub struct QuickSearchQuery(choscordb_core::quick_search::QuickSearchNeedle);

pub fn quick_search_query(query: &str) -> Box<QuickSearchQuery> {
    Box::new(QuickSearchQuery(
        choscordb_core::quick_search::QuickSearchNeedle::new(query),
    ))
}

impl QuickSearchQuery {
    /// The rank of `name`, lower first, or -1 when it does not match.
    pub fn score(&self, name: &str) -> i32 {
        self.0.score(name).map_or(-1, |score| score as i32)
    }

    pub fn plan(
        &self,
        destinations: &[ffi::QuickSearchDestinationDto],
        editor_rows: u32,
        history_rows: u32,
    ) -> ffi::QuickSearchPlanDto {
        let destinations: Vec<_> = destinations
            .iter()
            .map(|row| Destination {
                kind: match row.kind {
                    ffi::QuickSearchDestinationKind::OpenTab => DestinationKind::OpenTab,
                    ffi::QuickSearchDestinationKind::Command => DestinationKind::Command,
                    _ => DestinationKind::Object,
                },
                group: &row.group,
                title: &row.title,
                context: &row.context,
                id: &row.id,
            })
            .collect();
        let plan = self
            .0
            .plan(&destinations, editor_rows as usize, history_rows as usize);
        ffi::QuickSearchPlanDto {
            destinations: plan.destinations.into_iter().map(|i| i as u32).collect(),
            editor_rows: plan.editor_rows as u32,
            history_rows: plan.history_rows as u32,
            more: plan.more,
        }
    }
}

pub fn quick_search_limits() -> ffi::QuickSearchLimitsDto {
    ffi::QuickSearchLimitsDto {
        editor_rows: MAX_QUICK_SEARCH_EDITOR_ROWS as u32,
        recent_objects: MAX_RECENT_OBJECTS as u32,
        history_rows: QUICK_SEARCH_HISTORY_ROWS,
    }
}

/// A list filter query folded once in Rust and tested against many labels.
pub struct TextFilterQuery(choscordb_core::text_filter::TextFilter);

pub fn text_filter_query(query: &str) -> Box<TextFilterQuery> {
    Box::new(TextFilterQuery(
        choscordb_core::text_filter::TextFilter::new(query),
    ))
}

impl TextFilterQuery {
    pub fn blank(&self) -> bool {
        self.0.is_empty()
    }

    pub fn accepts(&self, text: &str) -> bool {
        self.0.matches(text)
    }
}

/// A literal search compiled once in Rust; blank or invalid needles find nothing.
pub struct TextFinderQuery(Option<choscordb_sql_language::TextFinder>);

pub fn text_finder(needle: &str) -> Box<TextFinderQuery> {
    Box::new(TextFinderQuery(
        choscordb_sql_language::TextFinder::new(
            needle,
            choscordb_sql_language::SearchOptions::default(),
        )
        .ok(),
    ))
}

impl TextFinderQuery {
    pub fn find_all(&self, source: &str, limit: u32) -> Vec<ffi::TextRangeDto> {
        self.0
            .as_ref()
            .and_then(|finder| finder.find_all(source, limit as usize).ok())
            .unwrap_or_default()
            .into_iter()
            .map(|range| ffi::TextRangeDto {
                start: range.start as u64,
                end: range.end as u64,
            })
            .collect()
    }
}
