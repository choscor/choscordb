use choscordb_bridge::{
    ffi, table_export_defaults, table_export_error, table_export_formats, table_export_line_breaks,
};

fn people() -> ffi::TableExportRequestDto {
    ffi::TableExportRequestDto {
        driver: "sqlite".into(),
        objects: vec![ffi::TableExportObjectDto {
            object_id: "[\"main\",\"people\"]".into(),
            qualified_name: "people".into(),
            kind: "table".into(),
            relation_subtype: String::new(),
        }],
        path: "/exports".into(),
        ..table_export_defaults()
    }
}

#[test]
fn defaults_and_choices_come_from_the_engine() {
    let defaults = table_export_defaults();
    assert_eq!(
        (
            defaults.format.as_str(),
            defaults.delimiter.as_str(),
            defaults.quote.as_str(),
            defaults.line_break.as_str(),
            defaults.pattern.as_str(),
        ),
        ("csv", ",", "\"", "crlf", "{table}")
    );
    assert!(defaults.header && defaults.folder);
    let formats: Vec<_> = table_export_formats()
        .into_iter()
        .map(|f| (f.code, f.csv_options, f.sql_options))
        .collect();
    assert_eq!(
        formats,
        [
            ("csv".to_string(), true, false),
            ("json".to_string(), false, false),
            ("jsonl".to_string(), false, false),
            ("xlsx".to_string(), false, false),
            ("sql".to_string(), false, true),
        ]
    );
    let breaks: Vec<_> = table_export_line_breaks()
        .into_iter()
        .map(|b| b.code)
        .collect();
    assert_eq!(breaks, ["crlf", "lf"]);
}

#[test]
fn request_errors_name_the_field() {
    assert_eq!(table_export_error(&people()), "");
    let lf = ffi::TableExportRequestDto {
        line_break: "lf".into(),
        delimiter: "\t".into(),
        ..people()
    };
    assert_eq!(table_export_error(&lf), "");
    let excel = ffi::TableExportRequestDto {
        format: "xlsx".into(),
        ..people()
    };
    assert_eq!(table_export_error(&excel), "");
    let cases = [
        (
            ffi::TableExportRequestDto {
                delimiter: ";;".into(),
                ..people()
            },
            "Enter one character for the delimiter.",
        ),
        (
            ffi::TableExportRequestDto {
                quote: String::new(),
                ..people()
            },
            "Enter one character for the quote.",
        ),
        (
            ffi::TableExportRequestDto {
                path: String::new(),
                ..people()
            },
            "Choose a destination.",
        ),
        (
            ffi::TableExportRequestDto {
                format: "xml".into(),
                ..people()
            },
            "Choose a supported format.",
        ),
        (
            ffi::TableExportRequestDto {
                objects: vec![],
                ..people()
            },
            "Choose one or more tables to export.",
        ),
    ];
    for (request, message) in cases {
        assert_eq!(table_export_error(&request), message);
    }
}
