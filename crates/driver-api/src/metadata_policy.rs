/// PostgreSQL namespaces reserved for system use.
pub fn is_postgres_system_schema(schema: &str) -> bool {
    schema == "information_schema" || schema.starts_with("pg_")
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

/// Whether an unverified navigator object is visible under the current schema policy.
pub fn navigator_object_visible(
    driver: &str,
    show_system_schemas: bool,
    qualified_name: &str,
) -> bool {
    if show_system_schemas || !driver.eq_ignore_ascii_case("postgres") {
        return true;
    }
    postgres_schema_from_qualified(qualified_name)
        .is_some_and(|schema| !is_postgres_system_schema(&schema))
}
