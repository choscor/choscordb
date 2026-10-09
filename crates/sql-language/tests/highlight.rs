use choscordb_sql_language::{HighlightKind, HighlightSpan, highlight};

fn spans(sql: &str) -> Vec<(&str, HighlightKind)> {
    highlight(sql)
        .into_iter()
        .map(|HighlightSpan { range, kind }| (&sql[range], kind))
        .collect()
}

#[test]
fn keywords_literals_numbers_and_comments_are_classified() {
    use HighlightKind::*;
    assert_eq!(
        spans("SELECT 'x', 42; -- note"),
        [
            ("SELECT", Keyword),
            ("'x'", String),
            ("42", Number),
            ("-- note", Comment)
        ]
    );
    assert_eq!(
        spans("create table t (n numeric DEFAULT 1.5)"),
        [
            ("create", Keyword),
            ("table", Keyword),
            ("DEFAULT", Keyword),
            ("1.5", Number)
        ]
    );
}

#[test]
fn quoted_text_and_comments_hide_keywords_and_numbers() {
    use HighlightKind::*;
    assert_eq!(
        spans(r#"CREATE TABLE "SELECT" (name TEXT DEFAULT '-- it''s'); -- 99"#),
        [
            ("CREATE", Keyword),
            ("TABLE", Keyword),
            (r#""SELECT""#, Identifier),
            ("TEXT", Keyword),
            ("DEFAULT", Keyword),
            ("'-- it''s'", String),
            ("-- 99", Comment)
        ]
    );
    assert_eq!(
        spans("`from` /* SELECT\n 7 */ t1 x2y"),
        [("`from`", Identifier), ("/* SELECT\n 7 */", Comment)]
    );
}

#[test]
fn unterminated_segments_run_to_the_end() {
    use HighlightKind::*;
    assert_eq!(
        spans("SELECT 'open\nNULL"),
        [("SELECT", Keyword), ("'open\nNULL", String)]
    );
    assert_eq!(spans("/* open\nNULL"), [("/* open\nNULL", Comment)]);
    assert_eq!(spans("é SELECT"), [("SELECT", Keyword)]);
}
