use choscordb_sql_language::history_sql_preview;

fn preview(sql: &str) -> String {
    history_sql_preview(sql, usize::MAX).text
}

#[test]
fn clauses_start_lines_and_keywords_are_uppercased() {
    assert_eq!(
        preview("select name,   'from here'\n from customers where id=6;"),
        "SELECT name, 'from here'\nFROM customers\nWHERE id=6;"
    );
    assert_eq!(
        preview("  delete   from t  where a = 1 and b = 2  "),
        "DELETE\nFROM t\nWHERE a = 1 AND b = 2"
    );
}

#[test]
fn quoted_text_comments_and_dollar_strings_are_kept_verbatim() {
    assert_eq!(
        preview("select '-- from' as note /* where */ from logs;"),
        "SELECT '-- from' AS note /* where */\nFROM logs;"
    );
    assert_eq!(
        preview("select $tag$from -- where$tag$ as note from logs;"),
        "SELECT $tag$from -- where$tag$ AS note\nFROM logs;"
    );
    assert_eq!(
        preview("select $$where from$$ as note from logs;"),
        "SELECT $$where from$$ AS note\nFROM logs;"
    );
    assert_eq!(
        preview("select 'it''s', \"a\\\"b\" -- trailing from\nfrom t"),
        "SELECT 'it''s', \"a\\\"b\" -- trailing from\nFROM t"
    );
}

#[test]
fn previews_truncate_by_character() {
    let preview = history_sql_preview("select 'é' from t", 10);
    assert_eq!(preview.text, "SELECT 'é'");
    assert!(preview.truncated);
    let whole = history_sql_preview("select 1", 8);
    assert_eq!(whole.text, "SELECT 1");
    assert!(!whole.truncated);
}
