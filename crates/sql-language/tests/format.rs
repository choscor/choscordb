use choscordb_sql_language::*;

const OPTIONS: FormatOptions = FormatOptions {
    indent: 2,
    uppercase_keywords: true,
};

fn pretty(sql: &str) -> String {
    format_sql(sql, None, OPTIONS, FormatStyle::Pretty).unwrap()
}

#[test]
fn formats_clauses_lists_and_conditions() {
    assert_eq!(
        pretty(
            "select a, b as c from t join u on t.id = u.id where x = 1 and y in (1, 2) order by a desc"
        ),
        "SELECT\n  a,\n  b AS c\nFROM\n  t\nJOIN u ON t.id = u.id\nWHERE\n  x = 1\n  AND y IN (1, 2)\nORDER BY\n  a DESC"
    );
}

#[test]
fn keeps_literals_identifiers_and_comments_byte_exact() {
    let sql = "select 'it''s  select', \"Mixed  Case\", count(*) -- note\nfrom t";
    assert_eq!(
        pretty(sql),
        "SELECT\n  'it''s  select',\n  \"Mixed  Case\",\n  count(*) -- note\nFROM\n  t"
    );
    assert_eq!(
        pretty("select E'a\\'b', $$x  y$$, x::int"),
        "SELECT\n  E'a\\'b',\n  $$x  y$$,\n  x::int"
    );
}

#[test]
fn nests_subqueries_and_separates_statements() {
    assert_eq!(
        pretty("select * from (select id from t) s; delete from t where id=1;"),
        "SELECT\n  *\nFROM\n  (\n    SELECT\n      id\n    FROM\n      t\n  ) s;\n\nDELETE FROM\n  t\nWHERE\n  id = 1;"
    );
}

#[test]
fn keyword_case_follows_the_option() {
    let options = FormatOptions {
        indent: 4,
        uppercase_keywords: false,
    };
    assert_eq!(
        format_sql("select a from t", None, options, FormatStyle::Pretty).unwrap(),
        "select\n    a\nfrom\n    t"
    );
}

#[test]
fn minify_puts_a_statement_on_one_line_and_keeps_line_comments_safe() {
    assert_eq!(
        format_sql(
            "SELECT\n  a,\n  b\nFROM t -- c\nWHERE x = 1",
            None,
            OPTIONS,
            FormatStyle::Minify
        )
        .unwrap(),
        "SELECT a, b FROM t -- c\nWHERE x = 1"
    );
}

#[test]
fn mysql_dialect_uses_backticks_hash_comments_and_escapes() {
    let mode = Some(MysqlSqlMode::default());
    assert_eq!(
        format_sql(
            "select `a b`, 'x\\'y' # c\nfrom t",
            mode,
            OPTIONS,
            FormatStyle::Pretty
        )
        .unwrap(),
        "SELECT\n  `a b`,\n  'x\\'y' # c\nFROM\n  t"
    );
}

#[test]
fn rejects_unterminated_text_instead_of_guessing() {
    assert_eq!(
        format_sql("select 'open", None, OPTIONS, FormatStyle::Pretty),
        Err(EditError::Unterminated)
    );
}

#[test]
fn format_edit_targets_the_selection_or_the_statement_at_the_cursor() {
    let sql = "select 1; select a from t;";
    let edit = format_edit(sql, 12, 12..12, None, OPTIONS, FormatStyle::Pretty).unwrap();
    assert_eq!(edit.range, 10..26);
    assert_eq!(edit.text, "SELECT\n  a\nFROM\n  t;");
    let edit = format_edit(sql, 0, 0..8, None, OPTIONS, FormatStyle::Pretty).unwrap();
    assert_eq!(
        edit,
        TextEdit {
            range: 0..8,
            text: "SELECT\n  1".into()
        }
    );
    assert_eq!(
        format_edit("", 0, 0..0, None, OPTIONS, FormatStyle::Pretty),
        Err(EditError::NoStatement)
    );
    assert_eq!(
        format_edit("é", 1, 1..1, None, OPTIONS, FormatStyle::Pretty),
        Err(EditError::InvalidRange)
    );
}

#[test]
fn line_comment_toggles_on_and_off_at_the_common_indent() {
    let sql = "  select 1\n\n    from t\nx";
    let on = toggle_line_comment(sql, 3..15).unwrap();
    assert_eq!(on.range, 0..22);
    assert_eq!(on.text, "  -- select 1\n\n  --   from t");
    let applied = format!("{}{}", on.text, &sql[22..]);
    let off = toggle_line_comment(&applied, 0..5).unwrap();
    assert_eq!(off.text, "  select 1");
    let off = toggle_line_comment(&applied, 0..28).unwrap();
    assert_eq!(off.text, "  select 1\n\n    from t");
}

#[test]
fn block_comment_wraps_and_unwraps_and_refuses_nested_close() {
    let edit = toggle_block_comment("select a, b", 7..11).unwrap();
    assert_eq!(
        edit,
        TextEdit {
            range: 7..11,
            text: "/* a, b */".into()
        }
    );
    let edit = toggle_block_comment("x /* a, b */", 2..12).unwrap();
    assert_eq!(edit.text, "a, b");
    let edit = toggle_block_comment("  select 1  \nnext", 4..4).unwrap();
    assert_eq!(
        edit,
        TextEdit {
            range: 2..10,
            text: "/* select 1 */".into()
        }
    );
    assert_eq!(
        toggle_block_comment("a */ b", 0..6),
        Err(EditError::NestedBlockComment)
    );
}
