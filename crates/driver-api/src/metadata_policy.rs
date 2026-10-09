/// PostgreSQL namespaces reserved for system use.
pub fn is_postgres_system_schema(schema: &str) -> bool {
    schema == "information_schema" || schema.starts_with("pg_")
}

/// Whether a loaded navigator node is a PostgreSQL system schema, hidden with
/// everything below it until system schemas are shown.
pub fn is_system_schema_node(kind: &str, name: &str) -> bool {
    kind == "schema" && is_postgres_system_schema(name)
}

fn component(input: &str, position: &mut usize) -> Option<String> {
    let tail = input.get(*position..)?;
    if tail.starts_with('"') {
        *position += 1;
        let mut result = String::new();
        while let Some(character) = input.get(*position..)?.chars().next() {
            *position += character.len_utf8();
            if character == '\0' {
                return None;
            }
            if character != '"' {
                result.push(character);
                continue;
            }
            if input.get(*position..)?.starts_with('"') {
                result.push('"');
                *position += 1;
                continue;
            }
            return (!result.is_empty()).then_some(result);
        }
        return None;
    }
    let mut result = String::new();
    while let Some(character) = input.get(*position..)?.chars().next() {
        if character == '.' {
            break;
        }
        if if result.is_empty() {
            !(character.is_alphabetic() || character == '_')
        } else {
            !(character.is_alphanumeric() || character == '_' || character == '$')
        } {
            return None;
        }
        result.push(character);
        *position += character.len_utf8();
    }
    (!result.is_empty()).then(|| result.to_ascii_lowercase())
}

/// Parse a PostgreSQL qualified object name and return its schema component.
/// Unquoted names fold to lowercase; quoted names preserve case and doubled quotes.
pub fn postgres_schema_from_qualified(qualified_name: &str) -> Option<String> {
    if qualified_name.len() > 1024 * 1024 {
        return None;
    }
    let mut position = 0;
    let schema = component(qualified_name, &mut position)?;
    let mut parts = 1;
    while position < qualified_name.len() {
        if !qualified_name.get(position..)?.starts_with('.') {
            return None;
        }
        position += 1;
        component(qualified_name, &mut position)?;
        parts += 1;
    }
    (parts >= 2).then_some(schema)
}

/// Whether `driver` hides system schemas from the navigator under this setting.
pub fn system_schemas_hidden(driver: &str, show_system_schemas: bool) -> bool {
    !show_system_schemas && driver.eq_ignore_ascii_case("postgres")
}

/// Whether an unverified navigator object is visible under the current schema policy.
pub fn navigator_object_visible(
    driver: &str,
    show_system_schemas: bool,
    qualified_name: &str,
) -> bool {
    if !system_schemas_hidden(driver, show_system_schemas) {
        return true;
    }
    postgres_schema_from_qualified(qualified_name)
        .is_some_and(|schema| !is_postgres_system_schema(&schema))
}

/// The column property naming its 1-based primary-key position (0 when not a key).
pub const PRIMARY_KEY_POSITION: &str = "Primary key position";

/// Whether column metadata reports the column as part of the primary key.
pub fn primary_key_column(properties: &[crate::MetadataProperty]) -> bool {
    properties.iter().any(|property| {
        property.name == PRIMARY_KEY_POSITION
            && property.availability == crate::MetadataAvailability::Available
            && property
                .value
                .parse::<u32>()
                .is_ok_and(|position| position > 0)
    })
}

/// Catalog labels one result's column metadata may carry across the bridge.
pub const MAX_RESULT_CELL_METADATA_BYTES: usize = 1024 * 1024;

/// Rejects result column metadata whose labels exceed the transfer budget.
pub fn check_result_cell_metadata(
    columns: &[crate::ResultCellMetadata],
) -> Result<(), crate::DriverError> {
    let bytes: usize = columns
        .iter()
        .map(|column| {
            [
                &column.source_column,
                &column.source_object,
                &column.source_qualified_name,
                &column.fk_target_object,
                &column.fk_target_qualified_name,
                &column.fk_target_column,
            ]
            .into_iter()
            .chain(&column.enum_choices)
            .map(String::len)
            .sum::<usize>()
        })
        .sum();
    if bytes > MAX_RESULT_CELL_METADATA_BYTES {
        return Err(crate::DriverError::new(
            crate::ErrorKind::ResourceLimit,
            "Result column metadata exceeds 1 MiB.",
        ));
    }
    Ok(())
}
