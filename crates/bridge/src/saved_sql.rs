//! Typed transport for the saved SQL sidebar and document picker.
use crate::ffi::{DocumentIoResultDto, SavedSqlEntryDto, SavedSqlListingDto, SavedSqlPathDto};
use std::path::Path;

pub fn saved_sql_list_directory(root: &str) -> SavedSqlListingDto {
    match choscordb_core::list_saved_sql_documents(Path::new(root)) {
        Ok(listing) => SavedSqlListingDto {
            entries: listing
                .documents
                .into_iter()
                .map(|document| SavedSqlEntryDto {
                    path: document.path.to_string_lossy().into_owned(),
                    relative_path: document.relative_path.to_string_lossy().into_owned(),
                    size_bytes: document.size_bytes,
                })
                .collect(),
            has_more: listing.has_more,
            error: String::new(),
        },
        Err(error) => SavedSqlListingDto {
            entries: Vec::new(),
            has_more: false,
            error: error.to_string(),
        },
    }
}

pub fn saved_sql_prepare_directory(root: &str) -> String {
    choscordb_core::ensure_saved_sql_directory(Path::new(root))
        .err()
        .map_or_else(String::new, |error| error.to_string())
}

pub fn saved_sql_document_identity(path: &str) -> SavedSqlPathDto {
    match choscordb_core::saved_sql_document_identity(Path::new(path)) {
        Ok(path) => SavedSqlPathDto {
            path: path.to_string_lossy().into_owned(),
            error: String::new(),
        },
        Err(error) => SavedSqlPathDto {
            path: String::new(),
            error: error.to_string(),
        },
    }
}

pub fn saved_sql_read_file(root: &str, path: &str) -> DocumentIoResultDto {
    match choscordb_core::read_saved_sql_document(Path::new(root), Path::new(path)) {
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
