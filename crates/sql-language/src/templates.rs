use std::fmt::{self, Write};
pub const MAX_TEMPLATE_BYTES: usize = 1024 * 1024;
pub const MAX_TEMPLATE_COLUMNS: usize = 4096;
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum NameError {
    Empty,
    Nul,
    InvalidQualified,
    ResourceLimit,
}
impl fmt::Display for NameError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(match self {
            Self::Empty => "An identifier or required column list is empty",
            Self::Nul => "An identifier contains a NUL character",
            Self::InvalidQualified => "The quoted qualified name is invalid",
            Self::ResourceLimit => "SQL template exceeds resource limits",
        })
    }
}
impl std::error::Error for NameError {}
fn validate_name(name: &str) -> Result<(), NameError> {
    if name.len() > MAX_TEMPLATE_BYTES {
        return Err(NameError::ResourceLimit);
    }
    if name.is_empty() {
        return Err(NameError::Empty);
    }
    if name.contains('\0') {
        return Err(NameError::Nul);
    }
    Ok(())
}
fn quote(output: &mut dyn Write, name: &str) -> fmt::Result {
    output.write_char('"')?;
    for part in name.split_inclusive('"') {
        output.write_str(part)?;
        if part.ends_with('"') {
            output.write_char('"')?;
        }
    }
    output.write_char('"')
}
// Count exact output first. No quoted-name vectors or growing format buffers.
fn bounded(render: impl Fn(&mut dyn Write) -> fmt::Result) -> Result<String, NameError> {
    struct Counter(usize);
    impl Write for Counter {
        fn write_str(&mut self, text: &str) -> fmt::Result {
            self.0 = self
                .0
                .checked_add(text.len())
                .filter(|size| *size <= MAX_TEMPLATE_BYTES)
                .ok_or(fmt::Error)?;
            Ok(())
        }
    }
    let mut counter = Counter(0);
    render(&mut counter).map_err(|_| NameError::ResourceLimit)?;
    let mut result = String::with_capacity(counter.0);
    render(&mut result).map_err(|_| NameError::ResourceLimit)?;
    Ok(result)
}
pub fn quote_identifier(name: &str) -> Result<String, NameError> {
    validate_name(name)?;
    bounded(|out| quote(out, name))
}
pub fn qualified_name(parts: &[&str]) -> Result<String, NameError> {
    if parts.is_empty() {
        return Err(NameError::Empty);
    }
    let mut bytes = 0usize;
    for part in parts {
        bytes = bytes.saturating_add(part.len());
        if bytes > MAX_TEMPLATE_BYTES {
            return Err(NameError::ResourceLimit);
        }
        validate_name(part)?;
    }
    bounded(|out| {
        for (index, part) in parts.iter().enumerate() {
            if index > 0 {
                out.write_char('.')?;
            }
            quote(out, part)?;
        }
        Ok(())
    })
}
#[derive(Debug, Clone, Copy)]
pub enum TemplateKind {
    Select,
    Insert,
    Update,
    Delete,
}
/// Editable SQL only; templates never execute or bind parameters.
pub fn template(
    kind: TemplateKind,
    object: &[&str],
    columns: &[&str],
) -> Result<String, NameError> {
    let name = qualified_name(object)?;
    template_from_qualified(kind, &name, columns)
}
/// Accepts only complete double-quoted identifier components separated by dots.
/// Dots within components, escaped quotes and Unicode remain unchanged.
pub fn template_from_qualified(
    kind: TemplateKind,
    name: &str,
    columns: &[&str],
) -> Result<String, NameError> {
    if columns.len() > MAX_TEMPLATE_COLUMNS || name.len() > MAX_TEMPLATE_BYTES {
        return Err(NameError::ResourceLimit);
    }
    validate_qualified(name)?;
    let mut input = name.len();
    for column in columns {
        input = input.saturating_add(column.len());
        if input > MAX_TEMPLATE_BYTES {
            return Err(NameError::ResourceLimit);
        }
        validate_name(column)?;
    }
    if matches!(kind, TemplateKind::Update) && columns.is_empty() {
        return Err(NameError::Empty);
    }
    bounded(|out| match kind {
        TemplateKind::Select => {
            out.write_str("SELECT ")?;
            if columns.is_empty() {
                out.write_char('*')?;
            } else {
                column_list(out, columns)?;
            }
            write!(out, " FROM {name};")
        }
        TemplateKind::Insert if columns.is_empty() => {
            write!(out, "INSERT INTO {name} DEFAULT VALUES;")
        }
        TemplateKind::Insert => {
            write!(out, "INSERT INTO {name} (")?;
            column_list(out, columns)?;
            out.write_str(") VALUES (")?;
            for index in 0..columns.len() {
                if index > 0 {
                    out.write_str(", ")?;
                }
                write!(out, "${}", index + 1)?;
            }
            out.write_str(");")
        }
        TemplateKind::Update => {
            write!(out, "UPDATE {name} SET ")?;
            for (index, column) in columns.iter().enumerate() {
                if index > 0 {
                    out.write_str(", ")?;
                }
                quote(out, column)?;
                write!(out, " = ${}", index + 1)?;
            }
            out.write_str(" WHERE /* predicate */;")
        }
        TemplateKind::Delete => write!(out, "DELETE FROM {name} WHERE /* predicate */;"),
    })
}
fn column_list(out: &mut dyn Write, columns: &[&str]) -> fmt::Result {
    for (index, column) in columns.iter().enumerate() {
        if index > 0 {
            out.write_str(", ")?;
        }
        quote(out, column)?;
    }
    Ok(())
}
fn validate_qualified(name: &str) -> Result<(), NameError> {
    if name.is_empty() {
        return Err(NameError::Empty);
    }
    if name.contains('\0') {
        return Err(NameError::Nul);
    }
    let bytes = name.as_bytes();
    let mut index = 0;
    loop {
        if bytes.get(index) != Some(&b'"') {
            return Err(NameError::InvalidQualified);
        }
        index += 1;
        let start = index;
        let mut closed = false;
        while index < bytes.len() {
            if bytes[index] == b'"' {
                if bytes.get(index + 1) == Some(&b'"') {
                    index += 2;
                    continue;
                }
                if index == start {
                    return Err(NameError::Empty);
                }
                index += 1;
                closed = true;
                break;
            }
            index += 1;
        }
        if !closed {
            return Err(NameError::InvalidQualified);
        }
        if index == bytes.len() {
            return Ok(());
        }
        if bytes[index] != b'.' {
            return Err(NameError::InvalidQualified);
        }
        index += 1;
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn strict_driver_qualified_names_preserve_components() {
        let name = "\"odd.schema\".\"a\"\"表\"";
        assert_eq!(
            template_from_qualified(TemplateKind::Select, name, &[]).unwrap(),
            format!("SELECT * FROM {name};")
        );
        for invalid in [
            "table",
            "\"table\"; DROP TABLE x",
            "\"schema\".",
            "\"unterminated",
            "\"\"",
            "\"a\"..\"b\"",
        ] {
            assert!(template_from_qualified(TemplateKind::Select, invalid, &[]).is_err());
        }
    }
    #[test]
    fn four_templates_quote_columns_and_keep_editable_predicates() {
        let name = "\"s\".\"table\"";
        let columns = ["a.b", "é\"x"];
        assert_eq!(
            template_from_qualified(TemplateKind::Insert, name, &columns).unwrap(),
            "INSERT INTO \"s\".\"table\" (\"a.b\", \"é\"\"x\") VALUES ($1, $2);"
        );
        assert_eq!(
            template_from_qualified(TemplateKind::Update, name, &columns).unwrap(),
            "UPDATE \"s\".\"table\" SET \"a.b\" = $1, \"é\"\"x\" = $2 WHERE /* predicate */;"
        );
        assert_eq!(
            template_from_qualified(TemplateKind::Delete, name, &[]).unwrap(),
            "DELETE FROM \"s\".\"table\" WHERE /* predicate */;"
        );
        assert_eq!(
            template_from_qualified(TemplateKind::Insert, name, &[]).unwrap(),
            "INSERT INTO \"s\".\"table\" DEFAULT VALUES;"
        );
        assert_eq!(
            template_from_qualified(TemplateKind::Update, name, &[]),
            Err(NameError::Empty)
        );
        assert_eq!(
            template(TemplateKind::Select, &["s", "table"], &columns).unwrap(),
            "SELECT \"a.b\", \"é\"\"x\" FROM \"s\".\"table\";"
        );
        assert_eq!(
            template_from_qualified(TemplateKind::Select, "\"nul\0name\"", &[]),
            Err(NameError::Nul)
        );
    }
    #[test]
    fn output_and_column_limits_precede_allocation() {
        assert!(
            template_from_qualified(TemplateKind::Insert, "\"table\"", &vec!["x"; 4097]).is_err()
        );
        assert!(
            template(
                TemplateKind::Select,
                &[&"x".repeat(MAX_TEMPLATE_BYTES)],
                &[]
            )
            .is_err()
        );
        assert!(quote_identifier(&"\"".repeat(MAX_TEMPLATE_BYTES / 2)).is_err());
    }
}
