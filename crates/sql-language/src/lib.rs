//! SQL editing services. All positions are UTF-8 byte offsets.
mod completion;
mod completion_context;
pub use completion_context::completion_context;
mod editor;
mod safety;
mod scanner;
mod search;
pub use search::{
    MAX_SEARCH_PATTERN_BYTES, MAX_SEARCH_SOURCE_BYTES, Replacement, SearchError, SearchMatch,
    SearchOptions, find, replace_all,
};
mod templates;
pub use completion::{
    Completion, CompletionError, CompletionKind, CompletionPage, MAX_COMPLETION_METADATA_BYTES,
    MAX_COMPLETION_METADATA_ENTRIES, MAX_COMPLETION_PREFIX_BYTES, MAX_COMPLETION_RESULTS,
    bounded_completions, completions,
};
pub use editor::execution_range;
pub use safety::{Safety, classify, parse};
pub use scanner::statement_ranges;
pub use templates::{
    MAX_TEMPLATE_BYTES, MAX_TEMPLATE_COLUMNS, NameError, TemplateKind, qualified_name,
    quote_identifier, template, template_from_qualified,
};

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn splits_only_unquoted_semicolons() {
        let sql = "SELECT ';', $$a;b$$, \"semi;\"; /* ; */ SELECT 'é';";
        let ranges = statement_ranges(sql);
        assert_eq!(ranges.len(), 2);
        assert_eq!(&sql[ranges[0].clone()], "SELECT ';', $$a;b$$, \"semi;\";");
        assert_eq!(&sql[ranges[1].clone()], "/* ; */ SELECT 'é';");
    }
}

#[cfg(test)]
mod safety_tests {
    use super::*;
    #[test]
    fn protects_destructive_queries_and_nested_writes() {
        for sql in [
            "DROP TABLE users",
            "TRUNCATE users",
            "DELETE FROM users",
            "UPDATE users SET x = (SELECT x FROM t WHERE id=1)",
            "WITH removed AS (DELETE FROM users RETURNING *) SELECT * FROM removed",
            "garbage ???",
            "SELECT 1; DELETE FROM users",
        ] {
            assert_eq!(classify(sql), Safety::ConfirmationRequired, "{sql}");
        }
        for sql in [
            "SELECT 'DROP TABLE users'",
            "DELETE FROM users WHERE id = 1",
            "UPDATE users SET x = 1 WHERE id = 2",
        ] {
            assert_eq!(classify(sql), Safety::Ordinary, "{sql}");
        }
    }
}

#[cfg(test)]
mod editor_tests {
    use super::*;
    #[test]
    fn selection_and_utf8_cursor() {
        let sql = "SELECT 'é'; SELECT 2;";
        assert_eq!(execution_range(sql, 0, Some(13..21)), Some(13..21));
        assert_eq!(execution_range(sql, 9, None), None);
        assert_eq!(execution_range(sql, 15, None), Some(13..22));
        assert_eq!(execution_range(sql, 0, Some(500..600)), None);
    }
    #[test]
    fn nested_comments_and_dollar_tags() {
        assert_eq!(
            statement_ranges("/* /* ; */ ; */ SELECT $tag$a;b$tag$; SELECT 2;").len(),
            2
        );
        assert_eq!(
            classify("SELECT 'unterminated"),
            Safety::ConfirmationRequired
        );
        assert_eq!(
            classify(
                "WITH a AS (DELETE FROM t RETURNING *), b AS (SELECT * FROM t WHERE id=1) SELECT * FROM b"
            ),
            Safety::ConfirmationRequired
        );
    }
    #[test]
    fn quoted_templates_and_completion() {
        assert_eq!(
            qualified_name(&["odd.schema", "a\"b"]).unwrap(),
            "\"odd.schema\".\"a\"\"b\""
        );
        assert_eq!(
            template(TemplateKind::Insert, &["t"], &["a", "b"]).unwrap(),
            "INSERT INTO \"t\" (\"a\", \"b\") VALUES ($1, $2);"
        );
        assert_eq!(
            classify(&template(TemplateKind::Delete, &["t"], &[]).unwrap()),
            Safety::ConfirmationRequired
        );
        assert_eq!(completions("sel", &[], 5)[0].label, "SELECT");
        assert!(quote_identifier("a\0b").is_err());
    }
}
