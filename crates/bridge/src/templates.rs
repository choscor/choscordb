use crate::ffi;
use choscordb_sql_language as language;
pub fn generate_sql_template(
    kind: &str,
    qualified: &str,
    columns: Vec<String>,
    columns_loaded: bool,
) -> ffi::SqlTemplateResultDto {
    let failure = |error: &str| ffi::SqlTemplateResultDto {
        error: error.into(),
        ..Default::default()
    };
    let Some(kind) = template_kind(kind) else {
        return failure("Unknown SQL template.");
    };
    let columns: Vec<&str> = columns.iter().map(String::as_str).collect();
    match language::navigator_template(kind, qualified, &columns, columns_loaded) {
        Ok(sql) => ffi::SqlTemplateResultDto {
            valid: true,
            sql,
            error: String::new(),
        },
        Err(error) => failure(&error),
    }
}
fn template_kind(kind: &str) -> Option<language::TemplateKind> {
    Some(match kind {
        "select" => language::TemplateKind::Select,
        "insert" => language::TemplateKind::Insert,
        "update" => language::TemplateKind::Update,
        "delete" => language::TemplateKind::Delete,
        _ => return None,
    })
}
pub fn sql_template_unavailable_reason(
    kind: &str,
    columns_loaded: bool,
    has_column: bool,
) -> String {
    template_kind(kind)
        .map_or(Some("Unknown SQL template."), |kind| {
            language::template_unavailable_reason(kind, columns_loaded, has_column)
        })
        .unwrap_or_default()
        .into()
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn generation_preserves_quoted_names_and_rejects_trailing_sql() {
        let result = generate_sql_template("select", "\"odd.schema\".\"a\"\"b\"", vec![], true);
        assert!(result.valid);
        assert_eq!(result.sql, "SELECT * FROM \"odd.schema\".\"a\"\"b\";");
        let invalid = generate_sql_template("select", "\"table\"; DROP TABLE other", vec![], true);
        assert!(!invalid.valid);
        assert!(!invalid.error.is_empty());
        assert!(!invalid.error.contains("DROP"));
        let unknown = generate_sql_template("unknown", "\"table\"", vec![], true);
        assert!(!unknown.valid);
        assert_eq!(unknown.error, "Unknown SQL template.");
    }
}
