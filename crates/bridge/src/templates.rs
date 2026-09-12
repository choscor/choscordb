use crate::ffi;
use choscordb_sql_language as language;
pub fn sql_template_limits() -> ffi::SqlTemplateLimitsDto {
    ffi::SqlTemplateLimitsDto {
        max_bytes: language::MAX_TEMPLATE_BYTES as u64,
        max_columns: language::MAX_TEMPLATE_COLUMNS as u64,
    }
}
pub fn generate_sql_template(
    kind: &str,
    qualified: &str,
    columns: Vec<String>,
) -> ffi::SqlTemplateResultDto {
    let failure = |error: &str| ffi::SqlTemplateResultDto {
        error: error.into(),
        ..Default::default()
    };
    let kind = match kind {
        "select" => language::TemplateKind::Select,
        "insert" => language::TemplateKind::Insert,
        "update" => language::TemplateKind::Update,
        "delete" => language::TemplateKind::Delete,
        _ => return failure("Unknown SQL template"),
    };
    if columns.len() > language::MAX_TEMPLATE_COLUMNS {
        return failure("SQL template exceeds resource limits");
    }
    let columns: Vec<&str> = columns.iter().map(String::as_str).collect();
    match language::template_from_qualified(kind, qualified, &columns) {
        Ok(sql) => ffi::SqlTemplateResultDto {
            valid: true,
            sql,
            error: String::new(),
        },
        Err(error) => failure(&error.to_string()),
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn generation_preserves_quoted_names_and_rejects_trailing_sql() {
        let result = generate_sql_template("select", "\"odd.schema\".\"a\"\"b\"", vec![]);
        assert!(result.valid);
        assert_eq!(result.sql, "SELECT * FROM \"odd.schema\".\"a\"\"b\";");
        let invalid = generate_sql_template("select", "\"table\"; DROP TABLE other", vec![]);
        assert!(!invalid.valid);
        assert!(!invalid.error.is_empty());
        assert!(!invalid.error.contains("DROP"));
        assert!(!generate_sql_template("unknown", "\"table\"", vec![]).valid);
    }
}
