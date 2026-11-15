use crate::{BridgeEngine, ffi, pack, submit, unpack};
use choscordb_core::{
    CsvOptions, DEFAULT_TABLE_EXPORT_PATTERN, LineBreak, TableExportDestination, TableExportFormat,
    TableExportObject, TableExportRequest, plan_table_export,
};

fn code_label(code: &str, label: &str) -> ffi::CodeLabelDto {
    ffi::CodeLabelDto {
        code: code.into(),
        label: label.into(),
    }
}

pub fn table_export_formats() -> Vec<ffi::TableExportFormatDto> {
    let format = |code: &str, label: &str| ffi::TableExportFormatDto {
        code: code.into(),
        label: label.into(),
        csv_options: code == "csv",
        sql_options: code == "sql",
    };
    vec![
        format("csv", "CSV"),
        format("json", "JSON"),
        format("jsonl", "JSON Lines"),
        format("sql", "SQL"),
    ]
}

pub fn table_export_line_breaks() -> Vec<ffi::CodeLabelDto> {
    vec![
        code_label("crlf", "Windows (CRLF)"),
        code_label("lf", "Unix (LF)"),
    ]
}

pub fn table_export_defaults() -> ffi::TableExportRequestDto {
    let csv = CsvOptions::default();
    ffi::TableExportRequestDto {
        format: "csv".into(),
        delimiter: csv.delimiter.to_string(),
        quote: csv.quote.to_string(),
        line_break: "crlf".into(),
        null_text: csv.null_text,
        header: csv.header,
        structure: true,
        drop: false,
        data: true,
        folder: true,
        pattern: DEFAULT_TABLE_EXPORT_PATTERN.into(),
        ..Default::default()
    }
}

fn single(value: &str, field: &str) -> Result<char, String> {
    let mut chars = value.chars();
    match (chars.next(), chars.next()) {
        (Some(c), None) => Ok(c),
        _ => Err(format!("Enter one character for the {field}.")),
    }
}

fn request(dto: &ffi::TableExportRequestDto) -> Result<TableExportRequest, String> {
    let format = match dto.format.as_str() {
        "csv" => TableExportFormat::Csv(CsvOptions {
            delimiter: single(&dto.delimiter, "delimiter")?,
            quote: single(&dto.quote, "quote")?,
            line_break: match dto.line_break.as_str() {
                "crlf" => LineBreak::Crlf,
                "lf" => LineBreak::Lf,
                _ => return Err("Choose a supported line break.".into()),
            },
            null_text: dto.null_text.clone(),
            header: dto.header,
        }),
        "json" => TableExportFormat::Json,
        "jsonl" => TableExportFormat::JsonLines,
        "sql" => TableExportFormat::Sql {
            structure: dto.structure,
            drop: dto.drop,
            data: dto.data,
        },
        _ => return Err("Choose a supported format.".into()),
    };
    if dto.path.is_empty() {
        return Err("Choose a destination.".into());
    }
    Ok(TableExportRequest {
        driver: dto.driver.clone(),
        objects: dto
            .objects
            .iter()
            .map(|object| TableExportObject {
                object_id: object.object_id.clone(),
                qualified_name: object.qualified_name.clone(),
                kind: object.kind.clone(),
                relation_subtype: object.relation_subtype.clone(),
            })
            .collect(),
        format,
        destination: if dto.folder {
            TableExportDestination::Folder {
                path: dto.path.clone().into(),
                pattern: dto.pattern.clone(),
            }
        } else {
            TableExportDestination::File(dto.path.clone().into())
        },
    })
}

pub fn table_export_error(dto: &ffi::TableExportRequestDto) -> String {
    match request(dto).and_then(|request| plan_table_export(&request)) {
        Ok(_) => String::new(),
        Err(error) => error,
    }
}

pub fn export_tables(
    engine: &mut BridgeEngine,
    connection: u64,
    dto: &ffi::TableExportRequestDto,
) -> ffi::Submit {
    let targets = request(dto).and_then(|request| plan_table_export(&request));
    submit(engine, |e| {
        e.export_tables(unpack(connection), targets?)
            .map(pack)
            .map_err(|error| error.to_string())
    })
}
