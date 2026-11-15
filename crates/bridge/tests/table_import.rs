use choscordb_bridge::{
    ffi, import_presentation, table_import_conflicts, table_import_default_mapping,
    table_import_defaults, table_import_encodings, table_import_error, table_import_formats,
    table_import_preview, table_import_review, table_import_unavailable_reason,
};

fn mapping(target: &str, source: i32) -> ffi::TableImportMappingDto {
    ffi::TableImportMappingDto {
        target: target.into(),
        source,
    }
}

fn people(path: &str) -> ffi::TableImportRequestDto {
    ffi::TableImportRequestDto {
        driver: "postgres".into(),
        path: path.into(),
        existing: true,
        object_id: "[\"public\",\"people\"]".into(),
        mapping: vec![mapping("id", 0), mapping("name", -1)],
        ..table_import_defaults()
    }
}

fn new_table() -> ffi::TableImportRequestDto {
    ffi::TableImportRequestDto {
        existing: false,
        parent_kind: "schema".into(),
        parent_qualified: "\"public\"".into(),
        table_name: "imported".into(),
        ..people("/data.csv")
    }
}

fn codes(choices: Vec<ffi::CodeLabelDto>) -> Vec<String> {
    choices.into_iter().map(|choice| choice.code).collect()
}

#[test]
fn defaults_and_choices_come_from_the_engine() {
    let defaults = table_import_defaults();
    assert_eq!(
        (
            defaults.format.as_str(),
            defaults.delimiter.as_str(),
            defaults.quote.as_str(),
            defaults.encoding.as_str(),
            defaults.conflict.as_str(),
        ),
        ("csv", ",", "\"", "utf-8", "stop")
    );
    assert!(defaults.header);
    let formats: Vec<_> = table_import_formats()
        .into_iter()
        .map(|format| (format.code, format.csv_options))
        .collect();
    assert_eq!(
        formats,
        [("csv".to_string(), true), ("json".to_string(), false)]
    );
    assert_eq!(codes(table_import_encodings()), ["utf-8", "latin-1"]);
    assert_eq!(
        codes(table_import_conflicts("postgres")),
        ["stop", "skip", "update"]
    );
    assert_eq!(
        codes(table_import_conflicts("mariadb")),
        ["stop", "skip", "update", "replace"]
    );
    assert!(table_import_conflicts("oracle").is_empty());
}

#[test]
fn default_mapping_matches_table_columns_to_file_columns_by_name() {
    assert_eq!(
        table_import_default_mapping(
            vec!["id".into(), "name".into(), "Name2".into()],
            vec!["name".into(), "id".into()],
        ),
        [1, 0, -1]
    );
}

#[test]
fn request_errors_name_the_problem() {
    assert_eq!(table_import_error(&people("/data.csv")), "");
    assert_eq!(table_import_error(&new_table()), "");
    let cases = [
        (people(""), "Choose a file to import."),
        (
            ffi::TableImportRequestDto {
                delimiter: "\u{e9}".into(),
                ..people("/data.csv")
            },
            "Enter one ASCII character for the delimiter.",
        ),
        (
            ffi::TableImportRequestDto {
                encoding: "utf-16".into(),
                ..people("/data.csv")
            },
            "Choose a supported encoding.",
        ),
        (
            ffi::TableImportRequestDto {
                conflict: "replace".into(),
                ..people("/data.csv")
            },
            "Choose a supported conflict policy.",
        ),
        (
            ffi::TableImportRequestDto {
                mapping: vec![mapping("id", -1)],
                ..people("/data.csv")
            },
            "Choose one or more columns to import.",
        ),
        (
            ffi::TableImportRequestDto {
                table_name: " ".into(),
                ..new_table()
            },
            "Enter a name for the new table.",
        ),
        (
            ffi::TableImportRequestDto {
                parent_kind: "table".into(),
                ..new_table()
            },
            "Select a connection, database or schema for the new table.",
        ),
    ];
    for (request, message) in cases {
        assert_eq!(table_import_error(&request), message);
    }
}

#[test]
fn preview_returns_row_major_cells_with_null_flags_or_the_read_error() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("rows.csv");
    std::fs::write(&path, "id,name\n1,\n2,Bo\n").unwrap();
    let preview = table_import_preview(&people(path.to_str().unwrap()));
    assert!(preview.valid, "{}", preview.error);
    assert_eq!(preview.columns, ["id", "name"]);
    assert_eq!(preview.row_count, 2);
    assert_eq!(preview.cells, ["1", "", "2", "Bo"]);
    assert_eq!(preview.nulls, [false, true, false, false]);
    let missing = table_import_preview(&people(dir.path().join("none.csv").to_str().unwrap()));
    assert!(!missing.valid);
    assert!(!missing.error.is_empty());
}

#[test]
fn import_presentation_names_the_import_and_not_the_export() {
    let failed = import_presentation(ffi::ExportStateDto::Failed, "Row 2: value too long");
    assert_eq!(
        failed.message,
        "Import failed: Row 2: value too long. Retry."
    );
    assert_eq!(
        import_presentation(ffi::ExportStateDto::Cancelled, "").message,
        "Import cancelled. The table did not change."
    );
}

#[test]
fn only_plain_tables_can_receive_imported_rows() {
    assert_eq!(table_import_unavailable_reason("table", ""), "");
    for (kind, subtype) in [("view", ""), ("table", "foreign_table")] {
        assert_eq!(
            table_import_unavailable_reason(kind, subtype),
            "Only tables can receive imported rows."
        );
    }
}

#[test]
fn review_shows_the_sql_for_each_row_before_the_import() {
    let review = |request: &ffi::TableImportRequestDto, keys: Vec<String>| {
        table_import_review(
            request,
            "\"public\".\"people\"",
            vec!["id".into(), "name".into()],
            keys,
            vec!["id".into(), "name".into()],
        )
    };
    let stop = review(&people("/data.csv"), vec![]);
    assert!(stop.valid, "{}", stop.error);
    assert_eq!(
        stop.sql,
        "INSERT INTO \"public\".\"people\" (\"id\") VALUES ($1);"
    );
    let update = ffi::TableImportRequestDto {
        conflict: "update".into(),
        mapping: vec![mapping("name", 1)],
        ..people("/data.csv")
    };
    let failed = review(&update, vec!["id".into()]);
    assert!(!failed.valid);
    assert_eq!(
        failed.error,
        "Import all primary key columns to update rows with the same key."
    );
    let created = table_import_review(&new_table(), "", vec![], vec![], vec!["id".into()]);
    assert_eq!(
        created.sql,
        "CREATE TABLE \"public\".\"imported\" (\"id\" TEXT);\nINSERT INTO \"public\".\"imported\" (\"id\") VALUES ($1);"
    );
}
