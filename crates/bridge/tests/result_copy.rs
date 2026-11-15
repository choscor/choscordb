use choscordb_bridge::*;

fn cell(kind: ffi::CellKindDto, text: &str, integer: i64) -> ffi::CopyCellDto {
    ffi::CopyCellDto {
        selected: true,
        original: ffi::CellDto {
            kind,
            text: text.into(),
            integer,
            ..Default::default()
        },
        ..Default::default()
    }
}

fn request(format: &str, sources: [&str; 2], driver: &str) -> ffi::CopyRequestDto {
    ffi::CopyRequestDto {
        rows: vec![ffi::CopyRowDto {
            cells: vec![
                cell(ffi::CellKindDto::Integer, "", 5),
                cell(ffi::CellKindDto::Text, "it's", 0),
            ],
        }],
        resolutions: vec![],
        byte_budget: 4096,
        valid_unicode: true,
        format: format.into(),
        columns: vec!["id".into(), "name".into()],
        source_tables: sources.iter().map(|source| (*source).into()).collect(),
        driver: driver.into(),
    }
}

#[test]
fn tsv_stays_the_default_copy_format() {
    let result = render_copy_policy(request("tsv", ["", ""], "sqlite"));
    assert_eq!(result.error, "");
    assert_eq!(result.text, "5\tit's");
}

#[test]
fn markdown_and_csv_with_header_use_column_names() {
    let result = render_copy_policy(request("markdown", ["", ""], ""));
    assert_eq!(result.text, "| id | name |\n| --- | --- |\n| 5 | it's |\n");
    let result = render_copy_policy(request("csv_header", ["", ""], "sqlite"));
    assert_eq!(result.text, "\"id\",\"name\"\r\n\"5\",\"it's\"\r\n");
}

#[test]
fn sql_insert_uses_the_one_source_table_and_driver_dialect() {
    let result = render_copy_policy(request(
        "sql_insert",
        ["\"public\".\"users\"", "\"public\".\"users\""],
        "postgres",
    ));
    assert_eq!(result.error, "");
    assert_eq!(
        result.text,
        "INSERT INTO \"public\".\"users\" (\"id\", \"name\") VALUES (5, E'it''s');\n"
    );
}

#[test]
fn sql_insert_from_two_tables_is_a_typed_error() {
    let result = render_copy_policy(request(
        "sql_insert",
        ["\"public\".\"users\"", "\"public\".\"orders\""],
        "postgres",
    ));
    assert_eq!(result.text, "");
    assert_eq!(result.error, "invalid_format");
    assert_eq!(result.reason, CLIPBOARD_SQL_TABLE_REQUIRED);
}

#[test]
fn copy_formats_are_listed_by_rust() {
    let codes = copy_format_options()
        .into_iter()
        .map(|option| option.code)
        .collect::<Vec<_>>();
    assert_eq!(
        codes,
        [
            "tsv",
            "csv",
            "csv_header",
            "markdown",
            "html",
            "sql_insert",
            "json"
        ]
    );
}
