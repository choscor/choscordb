use crate::ffi;
use choscordb_sql_language as language;
pub struct CompletionCatalog {
    items: Vec<language::Completion>,
    partial: bool,
}
pub fn completion_limits() -> ffi::CompletionLimitsDto {
    ffi::CompletionLimitsDto {
        max_results: language::MAX_COMPLETION_RESULTS as u64,
        max_prefix_bytes: language::MAX_COMPLETION_PREFIX_BYTES as u64,
        max_metadata_entries: language::MAX_COMPLETION_METADATA_ENTRIES as u64,
        max_metadata_bytes: language::MAX_COMPLETION_METADATA_BYTES as u64,
        max_source_bytes: language::MAX_SEARCH_SOURCE_BYTES as u64,
    }
}
pub fn completion_catalog(
    items: Vec<ffi::SqlCompletionDto>,
    partial: bool,
) -> Box<CompletionCatalog> {
    let mut partial = partial || items.len() > language::MAX_COMPLETION_METADATA_ENTRIES;
    let mut used = 0usize;
    let mut bounded = Vec::new();
    for mut item in items
        .into_iter()
        .take(language::MAX_COMPLETION_METADATA_ENTRIES)
    {
        let bytes = item.label.len().saturating_add(item.insert_text.len());
        if bytes > language::MAX_COMPLETION_METADATA_BYTES - used {
            partial = true;
            continue;
        }
        let kind = match item.kind.as_str() {
            "schema" | "database" => language::CompletionKind::Schema,
            "table" | "view" => language::CompletionKind::Table,
            "column" => language::CompletionKind::Column,
            _ => {
                partial = true;
                continue;
            }
        };
        if item.label.contains('\0') || item.insert_text.contains('\0') {
            partial = true;
            continue;
        }
        used += bytes;
        item.label.shrink_to_fit();
        item.insert_text.shrink_to_fit();
        bounded.push(language::Completion {
            label: item.label,
            insert_text: item.insert_text,
            kind,
        });
    }
    Box::new(CompletionCatalog {
        items: bounded,
        partial,
    })
}
pub fn complete_sql(
    catalog: &CompletionCatalog,
    sql: &str,
    cursor: u64,
    requested: bool,
) -> ffi::CompletionReplyDto {
    let Ok(cursor) = usize::try_from(cursor) else {
        return ffi::CompletionReplyDto::default();
    };
    let Some(range) = language::completion_context(sql, cursor) else {
        return ffi::CompletionReplyDto::default();
    };
    let prefix = &sql[range.clone()];
    if !requested && prefix.chars().count() < 2 {
        return ffi::CompletionReplyDto::default();
    }
    let Ok(page) =
        language::bounded_completions(prefix, &catalog.items, language::MAX_COMPLETION_RESULTS)
    else {
        return ffi::CompletionReplyDto::default();
    };
    ffi::CompletionReplyDto {
        valid: true,
        partial: catalog.partial || page.partial,
        start: range.start as u64,
        end: range.end as u64,
        items: page
            .items
            .into_iter()
            .map(|item| ffi::SqlCompletionDto {
                label: item.label,
                insert_text: item.insert_text,
                kind: match item.kind {
                    language::CompletionKind::Keyword => "keyword",
                    language::CompletionKind::Schema => "schema",
                    language::CompletionKind::Table => "table",
                    language::CompletionKind::Column => "column",
                }
                .into(),
            })
            .collect(),
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn catalog_does_not_retain_oversized_string_capacity() {
        let mut label = String::with_capacity(language::MAX_COMPLETION_METADATA_BYTES * 2);
        label.push_str("table");
        let catalog = completion_catalog(
            vec![ffi::SqlCompletionDto {
                label,
                insert_text: "\"table\"".into(),
                kind: "table".into(),
            }],
            false,
        );
        assert!(catalog.items[0].label.capacity() <= language::MAX_COMPLETION_METADATA_BYTES);
    }
    #[test]
    fn schema_completion_preserves_quoted_insertion_and_exact_replacement_range() {
        let catalog = completion_catalog(
            vec![ffi::SqlCompletionDto {
                label: "é name".into(),
                insert_text: "\"main\".\"é name\"".into(),
                kind: "table".into(),
            }],
            false,
        );
        let sql = "SELECT * FROM main.é";
        let result = complete_sql(&catalog, sql, sql.len() as u64, false);
        assert!(result.valid);
        assert_eq!(&sql[result.start as usize..result.end as usize], "main.é");
        assert_eq!(result.items.len(), 1);
        assert_eq!(result.items[0].insert_text, "\"main\".\"é name\"");
        let comment = "-- main.é";
        assert!(!complete_sql(&catalog, comment, comment.len() as u64, true).valid);
        assert!(!complete_sql(&catalog, "s", 1, false).valid);
        assert!(complete_sql(&catalog, "s", 1, true).valid);
    }
}
