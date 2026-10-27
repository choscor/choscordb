//! SQL editing services. All positions are UTF-8 byte offsets.
mod completion;
mod completion_context;
pub use completion_context::{completion_context, completion_source_supported};
mod editor;
mod highlight;
mod history_preview;
pub use highlight::{HighlightKind, HighlightSpan, highlight};
pub use history_preview::{HistoryPreview, history_sql_preview};
mod safety;
mod scanner;
mod search;
pub use search::{
    MAX_SEARCH_PATTERN_BYTES, MAX_SEARCH_SOURCE_BYTES, Replacement, SearchError, SearchMatch,
    SearchOptions, TextFinder, find, find_all, pattern_usable, replace_all, replacement_fits,
};
mod templates;
pub use completion::{
    Completion, CompletionError, CompletionKind, CompletionPage, MAX_COMPLETION_METADATA_BYTES,
    MAX_COMPLETION_METADATA_ENTRIES, MAX_COMPLETION_METADATA_VISITS, MAX_COMPLETION_PREFIX_BYTES,
    MAX_COMPLETION_RESULTS, bounded_completions, completions,
};
pub use editor::{ExecutionScope, execution_range};
pub use safety::{Safety, classify, parse};
pub use scanner::statement_ranges;
pub use templates::{
    MAX_TEMPLATE_BYTES, MAX_TEMPLATE_COLUMNS, NameError, TemplateKind, navigator_template,
    qualified_name, quote_identifier, template, template_from_qualified,
    template_unavailable_reason,
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
    #[test]
    fn transaction_control_needs_no_confirmation() {
        for sql in [
            "BEGIN",
            "begin;",
            "BEGIN TRANSACTION",
            "START TRANSACTION;",
            "COMMIT",
            "COMMIT WORK;",
            "ROLLBACK",
            "rollback work",
            "END",
        ] {
            assert_eq!(classify(sql), Safety::Ordinary, "{sql}");
            assert_eq!(classify_mysql(sql), Safety::Ordinary, "{sql}");
        }
        for sql in [
            "BEGIN; DROP TABLE users",
            "ROLLBACK TO SAVEPOINT x; DELETE FROM users",
            "COMMIT garbage ???",
        ] {
            assert_eq!(classify(sql), Safety::ConfirmationRequired, "{sql}");
        }
    }
}

#[cfg(test)]
mod editor_tests {
    use super::*;
    #[test]
    fn selection_and_utf8_cursor() {
        let sql = "SELECT 'é'; SELECT 2;";
        let current = ExecutionScope::Current;
        assert_eq!(execution_range(sql, 0, Some(13..21), current), Some(13..21));
        assert_eq!(execution_range(sql, 9, None, current), None);
        assert_eq!(execution_range(sql, 15, None, current), Some(13..22));
        assert_eq!(execution_range(sql, 0, Some(500..600), current), None);
    }
    #[test]
    fn all_scope_runs_the_whole_document_without_a_selection() {
        let all = ExecutionScope::All;
        let sql = "-- lead\nSELECT 1;\n  SELECT 2;\n-- trail\n";
        let range = execution_range(sql, 0, None, all).unwrap();
        assert_eq!(&sql[range], "-- lead\nSELECT 1;\n  SELECT 2;");
        assert_eq!(
            execution_range("SELECT 1; SELECT 2;", 0, None, ExecutionScope::Current),
            Some(0..9)
        );
        assert_eq!(
            execution_range("SELECT 1; SELECT 2;", 0, None, all),
            Some(0..19)
        );
        assert_eq!(execution_range("", 0, None, all), None);
        assert_eq!(execution_range("  -- only a comment\n", 0, None, all), None);
    }
    #[test]
    fn selection_overrides_both_scopes() {
        let sql = "SELECT 1; SELECT 2; SELECT 3;";
        for scope in [ExecutionScope::Current, ExecutionScope::All] {
            assert_eq!(execution_range(sql, 0, Some(10..19), scope), Some(10..19));
        }
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
            "-- Replace numbered placeholders with values before running.\nINSERT INTO \"t\" (\"a\", \"b\") VALUES ($1, $2);"
        );
        assert_eq!(
            classify(&template(TemplateKind::Delete, &["t"], &[]).unwrap()),
            Safety::ConfirmationRequired
        );
        assert_eq!(completions("sel", &[], 5)[0].label, "SELECT");
        assert!(quote_identifier("a\0b").is_err());
    }
}

// Dialect-specific entry points retain the existing editor behavior by default.
pub fn statement_ranges_mysql(sql: &str) -> Vec<std::ops::Range<usize>> {
    scanner::statement_ranges_dialect(sql, true)
}
pub fn execution_range_mysql(
    sql: &str,
    cursor: usize,
    selection: Option<std::ops::Range<usize>>,
) -> Option<std::ops::Range<usize>> {
    editor::execution_range_dialect(sql, cursor, selection, true)
}
pub fn classify_mysql(sql: &str) -> Safety {
    safety::classify_dialect(sql, true)
}

#[cfg(test)]
mod mysql_tests {
    use super::*;
    #[test]
    fn mysql_executable_comments_require_confirmation() {
        assert_eq!(
            classify_mysql("/*!50000 DROP TABLE users */"),
            Safety::ConfirmationRequired
        );
        assert_eq!(
            classify_mysql("SELECT 1 /*!50000 INTO OUTFILE '/tmp/data' */"),
            Safety::ConfirmationRequired
        );
        assert_eq!(classify_mysql("SELECT 1 /* harmless */"), Safety::Ordinary);
    }
    #[test]
    fn mariadb_executable_comments_require_confirmation_only_in_mariadb() {
        let mariadb = MysqlSqlMode {
            mariadb: true,
            ..MysqlSqlMode::default()
        };
        let sql = "/*M!100000 DROP TABLE t */";
        assert_eq!(
            classify_mysql_with_mode(sql, mariadb),
            Safety::ConfirmationRequired
        );
        assert_eq!(
            classify_mysql_with_mode("SELECT 1 /*M!100000 INTO OUTFILE '/tmp/data' */", mariadb),
            Safety::ConfirmationRequired
        );
        assert_eq!(
            statement_ranges_mysql_with_mode(sql, mariadb),
            vec![0..sql.len()]
        );
        assert_eq!(classify_mysql(sql), Safety::Ordinary);
        assert!(statement_ranges_mysql(sql).is_empty());
        assert_eq!(
            classify_mysql_with_mode("SELECT 1 /* harmless */", mariadb),
            Safety::Ordinary
        );
    }
    #[test]
    fn mysql_statement_selection_understands_escaped_quotes_and_hash_comments() {
        let sql = "SELECT 'a\\';b'; # ignored ;\nSELECT 2;";
        let ranges = statement_ranges_mysql(sql);
        assert_eq!(ranges.len(), 2);
        assert_eq!(&sql[ranges[0].clone()], "SELECT 'a\\';b';");
        assert_eq!(
            &sql[execution_range_mysql(sql, 12, None).unwrap()],
            "SELECT 'a\\';b';"
        );
        assert_eq!(statement_ranges_mysql("SELECT 1--2; SELECT 3;").len(), 2);
        assert_eq!(statement_ranges_mysql("SELECT '$tag$'; SELECT 2;").len(), 2);
    }
}

/// Session modes that affect MySQL lexical boundaries.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct MysqlSqlMode {
    pub no_backslash_escapes: bool,
    pub ansi_quotes: bool,
    /// MariaDB also executes `/*M!` comments, which MySQL ignores.
    pub mariadb: bool,
}
impl MysqlSqlMode {
    pub fn from_sql_mode(value: &str) -> Self {
        let modes: Vec<_> = value.split(',').map(str::trim).collect();
        Self {
            no_backslash_escapes: modes
                .iter()
                .any(|m| m.eq_ignore_ascii_case("NO_BACKSLASH_ESCAPES")),
            ansi_quotes: modes
                .iter()
                .any(|m| m.eq_ignore_ascii_case("ANSI_QUOTES") || m.eq_ignore_ascii_case("ANSI")),
            mariadb: false,
        }
    }
}
pub fn statement_ranges_mysql_with_mode(
    sql: &str,
    mode: MysqlSqlMode,
) -> Vec<std::ops::Range<usize>> {
    scanner::statement_ranges_with_mode(sql, Some(mode))
}
pub fn execution_range_mysql_with_mode(
    sql: &str,
    cursor: usize,
    selection: Option<std::ops::Range<usize>>,
    scope: ExecutionScope,
    mode: MysqlSqlMode,
) -> Option<std::ops::Range<usize>> {
    editor::execution_range_with_mode(sql, cursor, selection, scope, Some(mode))
}
pub fn classify_mysql_with_mode(sql: &str, mode: MysqlSqlMode) -> Safety {
    safety::classify_with_mode(sql, Some(mode))
}
