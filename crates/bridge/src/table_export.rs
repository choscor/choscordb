use crate::{BridgeEngine, code_label, ffi, pack, submit, unpack};
use choscordb_core::{
    CsvOptions, DEFAULT_TABLE_EXPORT_PATTERN, LineBreak, TableExportDestination, TableExportError,
    TableExportField, TableExportFormat, TableExportObject, TableExportRequest, plan_table_export,
};

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
        format("xlsx", "Excel"),
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

fn single(value: &str, field: &str) -> Result<char, TableExportError> {
    let mut chars = value.chars();
    match (chars.next(), chars.next()) {
        (Some(c), None) => Ok(c),
        _ => Err(TableExportError::new(
            TableExportField::Options,
            format!("Enter one character for the {field}."),
        )),
    }
}

fn request(dto: &ffi::TableExportRequestDto) -> Result<TableExportRequest, TableExportError> {
    let options = |message: &str| TableExportError::new(TableExportField::Options, message);
    let format = match dto.format.as_str() {
        "csv" => TableExportFormat::Csv(CsvOptions {
            delimiter: single(&dto.delimiter, "delimiter")?,
            quote: single(&dto.quote, "quote")?,
            line_break: match dto.line_break.as_str() {
                "crlf" => LineBreak::Crlf,
                "lf" => LineBreak::Lf,
                _ => return Err(options("Choose a supported line break.")),
            },
            null_text: dto.null_text.clone(),
            header: dto.header,
        }),
        "json" => TableExportFormat::Json,
        "jsonl" => TableExportFormat::JsonLines,
        "xlsx" => TableExportFormat::Xlsx,
        "sql" => TableExportFormat::Sql {
            structure: dto.structure,
            drop: dto.drop,
            data: dto.data,
        },
        _ => return Err(options("Choose a supported format.")),
    };
    if dto.path.is_empty() {
        return Err(TableExportError::new(
            TableExportField::Destination,
            "Choose a destination.",
        ));
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

pub fn table_export_error(dto: &ffi::TableExportRequestDto) -> ffi::TableExportErrorDto {
    match request(dto).and_then(|request| plan_table_export(&request)) {
        Ok(_) => ffi::TableExportErrorDto {
            field: ffi::TableExportFieldDto::None,
            message: String::new(),
        },
        Err(error) => ffi::TableExportErrorDto {
            field: match error.field {
                TableExportField::Tables => ffi::TableExportFieldDto::Tables,
                TableExportField::Options => ffi::TableExportFieldDto::Options,
                TableExportField::Destination => ffi::TableExportFieldDto::Destination,
            },
            message: error.message,
        },
    }
}

pub fn export_tables(
    engine: &mut BridgeEngine,
    connection: u64,
    dto: &ffi::TableExportRequestDto,
) -> ffi::Submit {
    let targets = request(dto)
        .and_then(|request| plan_table_export(&request))
        .map_err(|error| error.message);
    submit(engine, |e| {
        e.export_tables(unpack(connection), targets?)
            .map(pack)
            .map_err(|error| error.to_string())
    })
}
