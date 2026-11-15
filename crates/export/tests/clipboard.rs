use choscordb_driver_api::Value;
use choscordb_export::*;

fn names() -> Vec<String> {
    vec!["id".into(), "name".into()]
}

fn rows() -> Vec<Vec<Value>> {
    vec![
        vec![Value::Integer(1), Value::Text("a,\"b\"".into())],
        vec![Value::Integer(2), Value::Null],
    ]
}

#[test]
fn csv_without_and_with_header() {
    assert_eq!(
        render_clipboard(&ClipboardFormat::Csv { header: false }, &names(), &rows()).unwrap(),
        "\"1\",\"a,\"\"b\"\"\"\r\n\"2\",\r\n"
    );
    assert_eq!(
        render_clipboard(&ClipboardFormat::Csv { header: true }, &names(), &rows()).unwrap(),
        "\"id\",\"name\"\r\n\"1\",\"a,\"\"b\"\"\"\r\n\"2\",\r\n"
    );
}

#[test]
fn markdown_escapes_pipes_and_line_breaks() {
    let rows = vec![vec![
        Value::Text("x|y".into()),
        Value::Text("l1\nl2".into()),
    ]];
    assert_eq!(
        render_clipboard(&ClipboardFormat::Markdown, &names(), &rows).unwrap(),
        "| id | name |\n| --- | --- |\n| x\\|y | l1<br>l2 |\n"
    );
    assert_eq!(
        render_clipboard(&ClipboardFormat::Markdown, &names(), &self::rows()).unwrap(),
        "| id | name |\n| --- | --- |\n| 1 | a,\"b\" |\n| 2 | NULL |\n"
    );
}

#[test]
fn html_escapes_markup() {
    let rows = vec![vec![Value::Text("<b>&".into()), Value::Null]];
    assert_eq!(
        render_clipboard(&ClipboardFormat::Html, &names(), &rows).unwrap(),
        "<table>\n<thead><tr><th>id</th><th>name</th></tr></thead>\n<tbody>\n\
         <tr><td>&lt;b&gt;&amp;</td><td>NULL</td></tr>\n</tbody>\n</table>\n"
    );
}

#[test]
fn sql_insert_uses_quoted_source_table_in_dialect() {
    let format = ClipboardFormat::SqlInsert {
        table: "\"main\".\"people\"".into(),
        dialect: SqlDialect::Sqlite,
    };
    assert_eq!(
        render_clipboard(&format, &names(), &rows()).unwrap(),
        "INSERT INTO \"main\".\"people\" (\"id\", \"name\") VALUES (1, 'a,\"b\"');\n\
         INSERT INTO \"main\".\"people\" (\"id\", \"name\") VALUES (2, NULL);\n"
    );
    let mysql = ClipboardFormat::SqlInsert {
        table: "`shop`.`o``rders`".into(),
        dialect: SqlDialect::Mysql,
    };
    assert!(
        render_clipboard(&mysql, &names(), &[vec![Value::Integer(3), Value::Null]])
            .unwrap()
            .starts_with("INSERT INTO `shop`.`o``rders` (`id`, `name`) VALUES (3, NULL);")
    );
}

#[test]
fn sql_insert_rejects_a_missing_or_injected_table() {
    for table in [
        "",
        "people",
        "\"a\"; DROP TABLE x",
        "\"a\".\"b\".\"c\".\"d\"",
    ] {
        let format = ClipboardFormat::SqlInsert {
            table: table.into(),
            dialect: SqlDialect::Postgres,
        };
        assert_eq!(
            render_clipboard(&format, &names(), &rows())
                .unwrap_err()
                .message,
            CLIPBOARD_SQL_TABLE_REQUIRED,
            "{table}"
        );
    }
}

#[test]
fn json_is_an_array_of_objects() {
    assert_eq!(
        render_clipboard(&ClipboardFormat::Json, &names(), &rows()).unwrap(),
        "[\n  {\"id\":1,\"name\":\"a,\\\"b\\\"\"},\n  {\"id\":2,\"name\":null}\n]\n"
    );
}

#[test]
fn rows_must_match_the_column_count() {
    let rows = vec![vec![Value::Integer(1)]];
    assert!(render_clipboard(&ClipboardFormat::Markdown, &names(), &rows).is_err());
}

#[test]
fn format_codes_round_trip() {
    for code in ["csv", "csv_header", "markdown", "html", "json"] {
        assert!(ClipboardFormat::parse(code, "", None).is_ok(), "{code}");
    }
    assert!(ClipboardFormat::parse("sql_insert", "\"t\"", Some(SqlDialect::Sqlite)).is_ok());
    assert!(ClipboardFormat::parse("sql_insert", "\"t\"", None).is_err());
    assert!(ClipboardFormat::parse("xml", "", None).is_err());
}
