//! Substring filtering shared by the desktop's list and tree filters.

/// A query folded once so it can be tested against many visible labels.
#[derive(Clone, Debug, Default)]
pub struct TextFilter {
    folded: String,
}

impl TextFilter {
    pub fn new(query: &str) -> Self {
        Self {
            folded: query.trim().to_lowercase(),
        }
    }

    /// Whether the filter accepts every label because its query is blank.
    pub fn is_empty(&self) -> bool {
        self.folded.is_empty()
    }

    /// Whether `text` contains the query, ignoring case.
    pub fn matches(&self, text: &str) -> bool {
        self.is_empty() || text.to_lowercase().contains(&self.folded)
    }
}
