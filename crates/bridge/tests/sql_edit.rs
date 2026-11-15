use choscordb_bridge::*;

fn request(sql: &str, cursor: u64, driver: &str) -> ffi::SqlFormatRequestDto {
    ffi::SqlFormatRequestDto {
        sql: sql.into(),
        cursor,
        selection_start: cursor,
        selection_end: cursor,
        driver: driver.into(),
        indent: 2,
        uppercase_keywords: true,
        ..Default::default()
    }
}

#[test]
fn format_edit_returns_byte_range_and_text_for_the_statement() {
    let sql = "select 1; select `a` from t";
    let edit = sql_format_edit(ffi::SqlFormatRequestDto {
        indent: 4,
        uppercase_keywords: false,
        ..request(sql, 12, "mysql")
    });
    assert!(edit.valid, "{}", edit.error);
    assert_eq!((edit.start, edit.end), (10, 27));
    assert_eq!(edit.text, "select\n    `a`\nfrom\n    t");
    let minified = sql_format_edit(ffi::SqlFormatRequestDto {
        minify: true,
        ..request("select\n  1", 0, "sqlite")
    });
    assert_eq!(minified.text, "SELECT 1");
}

#[test]
fn format_edit_errors_are_typed_text_not_silent_defaults() {
    let edit = sql_format_edit(request("select 'x", 0, "postgres"));
    assert!(!edit.valid);
    assert!(edit.error.contains("unterminated"));
    let edit = sql_format_edit(ffi::SqlFormatRequestDto {
        indent: 0,
        ..request("select 1", 0, "postgres")
    });
    assert!(!edit.valid);
    assert!(!edit.error.is_empty());
}

#[test]
fn comment_toggles_cover_lines_and_blocks() {
    let edit = sql_toggle_comment("a\nb", 0, 3, false);
    assert_eq!(
        (edit.start, edit.end, edit.text.as_str()),
        (0, 3, "-- a\n-- b")
    );
    let edit = sql_toggle_comment("x y", 2, 3, true);
    assert_eq!(
        (edit.start, edit.end, edit.text.as_str()),
        (2, 3, "/* y */")
    );
}

#[test]
fn row_limit_uses_the_driver_lexer() {
    let limited = sql_apply_row_limit("select `a;b` from t", 100, "mysql", "");
    assert!(limited.applied);
    assert_eq!(limited.sql, "select `a;b` from t LIMIT 100");
    assert_eq!(limited.limit, 100);
    let unchanged = sql_apply_row_limit("delete from t", 100, "postgres", "");
    assert!(!unchanged.applied);
    assert_eq!(unchanged.sql, "delete from t");
    assert_eq!(row_limit_choices()[0], 0);
}
