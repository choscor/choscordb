use crate::code_label;
use crate::ffi::{
    self, DocumentIoResultDto, SavedSqlEntryDto, SavedSqlListingDto, SavedSqlPathDto,
};
use choscordb_core::SavedSqlSort;
use std::path::Path;

pub fn saved_sql_sorts() -> Vec<ffi::CodeLabelDto> {
    SavedSqlSort::ALL
        .into_iter()
        .map(|sort| code_label(sort.code(), sort.label()))
        .collect()
}

pub fn saved_sql_list_directory(root: &str, sort: &str) -> SavedSqlListingDto {
    let limit = choscordb_core::MAX_SAVED_SQL_DOCUMENTS as u32;
    let failed = |error: String| SavedSqlListingDto {
        limit,
        error,
        ..SavedSqlListingDto::default()
    };
    let Some(sort) = SavedSqlSort::from_code(sort) else {
        return failed(format!("Unknown saved SQL sort: {sort}"));
    };
    match choscordb_core::saved_sql_tree(Path::new(root), sort) {
        Ok(tree) => SavedSqlListingDto {
            entries: tree
                .nodes
                .into_iter()
                .map(|node| SavedSqlEntryDto {
                    path: node.path.to_string_lossy().into_owned(),
                    relative_path: node.relative_path.to_string_lossy().into_owned(),
                    folder: node.folder,
                    keyword: node.keyword.unwrap_or_default(),
                })
                .collect(),
            snippets: tree
                .snippets
                .into_iter()
                .map(|snippet| ffi::SqlCompletionDto {
                    label: snippet.keyword,
                    insert_text: snippet.sql,
                    kind: "snippet".into(),
                })
                .collect(),
            errors: tree.errors,
            has_more: tree.has_more,
            limit,
            error: String::new(),
        },
        Err(error) => failed(error.to_string()),
    }
}

pub fn saved_sql_set_keyword(root: &str, path: &str, keyword: &str) -> String {
    choscordb_core::set_saved_sql_keyword(Path::new(root), Path::new(path), keyword)
        .err()
        .unwrap_or_default()
}

pub fn saved_sql_create_folder(root: &str, parent: &str, name: &str) -> String {
    choscordb_core::create_saved_sql_folder(Path::new(root), Path::new(parent), name)
        .err()
        .unwrap_or_default()
}

pub fn saved_sql_root(storage_path: &str, default_data_directory: &str) -> String {
    choscordb_core::saved_sql_root(Path::new(storage_path), Path::new(default_data_directory))
        .to_string_lossy()
        .into_owned()
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
