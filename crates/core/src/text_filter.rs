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

    pub fn is_empty(&self) -> bool {
        self.folded.is_empty()
    }

    pub fn matches(&self, text: &str) -> bool {
        self.is_empty() || text.to_lowercase().contains(&self.folded)
    }
}
