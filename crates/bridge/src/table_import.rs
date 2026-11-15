use crate::{BridgeEngine, ffi, pack, submit, unpack};
use choscordb_core::{
    CsvReadOptions, ImportData, ImportEncoding, ImportFile, ImportFileFormat, ImportMapping,
    ImportRead, TableImportRequest, TableImportTarget, plan_table_import, read_import_file,
    table_import_mapping_error, table_import_unavailable,
};
use choscordb_driver_api::{
    EditColumn, EditTarget, GridEditDriver, ImportConflict, ObjectId, quote_import_identifier,
};
use choscordb_sql_language::create_template_parent;

fn code_label(code: &str, label: &str) -> ffi::CodeLabelDto {
    ffi::CodeLabelDto {
        code: code.into(),
        label: label.into(),
    }
}

fn import_driver(name: &str) -> Result<GridEditDriver, String> {
    crate::grid_edit::grid_driver(name).map_err(|_| "This connection cannot import files.".into())
}

pub fn table_import_unavailable_reason(kind: &str, relation_subtype: &str) -> String {
    table_import_unavailable(kind, relation_subtype)
        .unwrap_or_default()
        .into()
}

pub fn table_import_formats() -> Vec<ffi::TableExportFormatDto> {
    let format = |code: &str, label: &str| ffi::TableExportFormatDto {
        code: code.into(),
        label: label.into(),
        csv_options: code == "csv",
        sql_options: false,
    };
    vec![format("csv", "CSV"), format("json", "JSON or JSON Lines")]
}

pub fn table_import_encodings() -> Vec<ffi::CodeLabelDto> {
    ImportEncoding::ALL
        .into_iter()
        .map(|encoding| code_label(encoding.code(), encoding.label()))
        .collect()
}

pub fn table_import_conflicts(driver: &str) -> Vec<ffi::CodeLabelDto> {
    let Ok(driver) = import_driver(driver) else {
        return Vec::new();
    };
    ImportConflict::ALL
        .into_iter()
        .filter(|conflict| conflict.supported(driver))
        .map(|conflict| code_label(conflict.code(), conflict.label()))
        .collect()
}

pub fn table_import_defaults() -> ffi::TableImportRequestDto {
    let csv = CsvReadOptions::default();
    ffi::TableImportRequestDto {
        format: "csv".into(),
        delimiter: char::from(csv.delimiter).to_string(),
        quote: char::from(csv.quote).to_string(),
        header: csv.header,
        null_text: csv.null_text,
        encoding: ImportEncoding::Utf8.code().into(),
        conflict: ImportConflict::Stop.code().into(),
        ..Default::default()
    }
}

pub fn table_import_default_mapping(
    table_columns: Vec<String>,
    file_columns: Vec<String>,
) -> Vec<i32> {
    table_columns
        .iter()
        .map(|column| {
            file_columns
                .iter()
                .position(|name| name == column)
                .and_then(|index| i32::try_from(index).ok())
                .unwrap_or(-1)
        })
        .collect()
}

fn single_byte(value: &str, field: &str) -> Result<u8, String> {
    match value.as_bytes() {
        [byte] if byte.is_ascii() && !matches!(byte, b'\n' | b'\r') => Ok(*byte),
        _ => Err(format!("Enter one ASCII character for the {field}.")),
    }
}

fn file(dto: &ffi::TableImportRequestDto) -> Result<ImportFile, String> {
    if dto.path.is_empty() {
        return Err("Choose a file to import.".into());
    }
    let format = match dto.format.as_str() {
        "csv" => ImportFileFormat::Csv(CsvReadOptions {
            delimiter: single_byte(&dto.delimiter, "delimiter")?,
            quote: single_byte(&dto.quote, "quote")?,
            header: dto.header,
            null_text: dto.null_text.clone(),
        }),
        "json" => ImportFileFormat::Json,
        _ => return Err("Choose a supported format.".into()),
    };
    Ok(ImportFile {
        path: dto.path.clone().into(),
        format,
        encoding: ImportEncoding::from_code(&dto.encoding).ok_or("Choose a supported encoding.")?,
    })
}

fn target(
    dto: &ffi::TableImportRequestDto,
    driver: GridEditDriver,
) -> Result<TableImportTarget, String> {
    if dto.existing {
        return Ok(TableImportTarget::Existing {
            object: ObjectId(dto.object_id.clone()),
        });
    }
    let qualify = create_template_parent(&dto.parent_kind)
        .ok_or("Select a connection, database or schema for the new table.")?;
    if dto.table_name.trim().is_empty() {
        return Err("Enter a name for the new table.".into());
    }
    let name = quote_import_identifier(driver, &dto.table_name).map_err(|e| e.to_string())?;
    Ok(TableImportTarget::New {
        qualified_name: if qualify {
            format!("{}.{name}", dto.parent_qualified)
        } else {
            name
        },
    })
}

fn request(dto: &ffi::TableImportRequestDto) -> Result<TableImportRequest, String> {
    let driver = import_driver(&dto.driver)?;
    let request = TableImportRequest {
        driver,
        file: file(dto)?,
        target: target(dto, driver)?,
        mapping: dto
            .mapping
            .iter()
            .filter_map(|entry| {
                usize::try_from(entry.source)
                    .ok()
                    .map(|source| ImportMapping {
                        target: entry.target.clone(),
                        source,
                    })
            })
            .collect(),
        conflict: ImportConflict::from_code(&dto.conflict)
            .filter(|conflict| conflict.supported(driver))
            .ok_or("Choose a supported conflict policy.")?,
    };
    if let Some(error) = table_import_mapping_error(&request.mapping) {
        return Err(error);
    }
    Ok(request)
}

pub fn table_import_error(dto: &ffi::TableImportRequestDto) -> String {
    request(dto).err().unwrap_or_default()
}

pub fn table_import_preview(dto: &ffi::TableImportRequestDto) -> ffi::TableImportPreviewDto {
    let data = file(dto).and_then(|file| {
        read_import_file(&file, ImportRead::Preview).map_err(|error| error.message)
    });
    match data {
        Ok(data) => ffi::TableImportPreviewDto {
            valid: true,
            error: String::new(),
            row_count: u32::try_from(data.rows.len()).unwrap_or(u32::MAX),
            nulls: data.rows.iter().flatten().map(Option::is_none).collect(),
            cells: data
                .rows
                .into_iter()
                .flatten()
                .map(Option::unwrap_or_default)
                .collect(),
            columns: data.columns,
        },
        Err(error) => ffi::TableImportPreviewDto {
            valid: false,
            error,
            ..Default::default()
        },
    }
}

pub fn table_import_review(
    dto: &ffi::TableImportRequestDto,
    qualified_name: &str,
    table_columns: Vec<String>,
    key_columns: Vec<String>,
    file_columns: Vec<String>,
) -> ffi::TableImportReviewDto {
    let target = EditTarget {
        qualified_name: qualified_name.into(),
        columns: table_columns
            .into_iter()
            .map(|name| EditColumn {
                key: key_columns.contains(&name),
                name,
                database_type: String::new(),
                nullable: true,
                generated: false,
            })
            .collect(),
        key_columns,
        ..Default::default()
    };
    let data = ImportData {
        columns: file_columns,
        rows: Vec::new(),
    };
    let reviewed = request(dto).and_then(|request| {
        plan_table_import(&request, Some(&target), data).map_err(|error| error.message)
    });
    match reviewed {
        Ok(plan) => ffi::TableImportReviewDto {
            valid: true,
            error: String::new(),
            sql: plan
                .create
                .into_iter()
                .chain(std::iter::once(plan.insert))
                .map(|statement| statement + ";")
                .collect::<Vec<_>>()
                .join("\n"),
        },
        Err(error) => ffi::TableImportReviewDto {
            valid: false,
            error,
            sql: String::new(),
        },
    }
}

pub fn import_table(
    engine: &mut BridgeEngine,
    connection: u64,
    dto: &ffi::TableImportRequestDto,
) -> ffi::Submit {
    let request = request(dto);
    submit(engine, |e| {
        e.import_table(unpack(connection), request?)
            .map(pack)
            .map_err(|error| error.to_string())
    })
}
