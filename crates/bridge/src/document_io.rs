use crate::ffi::DocumentIoResultDto;
use std::path::Path;

pub fn read_sql_document_file(path: &str) -> DocumentIoResultDto {
    match choscordb_core::read_sql_document(Path::new(path)) {
        Ok(bytes) => DocumentIoResultDto {
            bytes,
            error: String::new(),
        },
        Err(error) => DocumentIoResultDto {
            bytes: Vec::new(),
            error: error.to_string(),
        },
    }
}

pub fn write_sql_document_file(path: &str, bytes: &[u8]) -> DocumentIoResultDto {
    let error = choscordb_core::write_sql_document(Path::new(path), bytes)
        .err()
        .map_or_else(String::new, |error| error.to_string());
    DocumentIoResultDto {
        bytes: Vec::new(),
        error,
    }
}
