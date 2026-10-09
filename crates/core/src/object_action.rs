//! Validation and SQL generation for navigator relation actions.
use serde_json::Value;

const MAX_NAME_UNITS: usize = 1024 * 1024;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ObjectAction {
    Drop,
    Rename,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ObjectActionStatement {
    pub sql: String,
    pub new_object_id: String,
    pub new_qualified_name: String,
}

fn valid_name(name: &str, driver: &str, is_new: bool) -> bool {
    if name.is_empty()
        || (is_new && name.trim().is_empty())
        || name.encode_utf16().count() > MAX_NAME_UNITS
        || name.contains('\0')
    {
        return false;
    }
    match driver {
        "mysql" => {
            name.encode_utf16().count() <= 64
                && !name.ends_with(' ')
                && name.chars().all(|character| character.len_utf16() == 1)
        }
        "postgres" => name.len() <= 63,
        "sqlite" => !is_new || !name.to_ascii_lowercase().starts_with("sqlite_"),
        _ => false,
    }
}

fn quoted(name: &str, delimiter: char) -> String {
    let escaped = name.replace(delimiter, &format!("{delimiter}{delimiter}"));
    format!("{delimiter}{escaped}{delimiter}")
}

fn quoted_part(input: &str, position: &mut usize) -> Option<String> {
    let tail = input.get(*position..)?;
    if !tail.starts_with('"') {
        return None;
    }
    *position += 1;
    let mut part = String::new();
    while let Some(character) = input.get(*position..)?.chars().next() {
        *position += character.len_utf8();
        if character != '"' {
            part.push(character);
            continue;
        }
        if input.get(*position..)?.starts_with('"') {
            part.push('"');
            *position += 1;
            continue;
        }
        return Some(part);
    }
    None
}

fn identity(driver: &str, object_id: &str, qualified_name: &str) -> Option<(String, String)> {
    if object_id.encode_utf16().count() > MAX_NAME_UNITS * 2
        || qualified_name.encode_utf16().count() > MAX_NAME_UNITS * 2
    {
        return None;
    }
    if driver == "postgres" {
        let mut id_parts = object_id.split(':');
        let (Some("pg"), Some("relation"), Some(oid), None) = (
            id_parts.next(),
            id_parts.next(),
            id_parts.next(),
            id_parts.next(),
        ) else {
            return None;
        };
        let number = oid.parse::<u32>().ok()?;
        if number == 0 || oid != number.to_string() {
            return None;
        }
        let mut position = 0;
        let schema = quoted_part(qualified_name, &mut position)?;
        if qualified_name.get(position..)?.starts_with('.') {
            position += 1;
        } else {
            return None;
        }
        let name = quoted_part(qualified_name, &mut position)?;
        if position != qualified_name.len()
            || !valid_name(&schema, driver, false)
            || !valid_name(&name, driver, false)
        {
            return None;
        }
        return Some((schema, name));
    }
    if driver != "sqlite" && driver != "mysql" {
        return None;
    }
    let Value::Array(parts) = serde_json::from_str::<Value>(object_id).ok()? else {
        return None;
    };
    if parts.len() != 2 {
        return None;
    }
    let schema = parts[0].as_str()?;
    let name = parts[1].as_str()?;
    if !valid_name(schema, driver, false) || !valid_name(name, driver, false) {
        return None;
    }
    Some((schema.to_owned(), name.to_owned()))
}

/// Whether `action` applies to an object before its identity is known.
pub fn object_action_availability(
    action: ObjectAction,
    driver: &str,
    kind: &str,
    relation_subtype: &str,
) -> Result<(), &'static str> {
    if kind != "table" && kind != "view" {
        return Err("Only tables and views support this action.");
    }
    if !matches!(driver, "sqlite" | "postgres" | "mysql") {
        return Err("This connection does not support this action.");
    }
    if !relation_subtype.is_empty()
        && !(driver == "postgres"
            && ((relation_subtype == "materialized_view" && kind == "view")
                || (relation_subtype == "foreign_table" && kind == "table")))
    {
        return Err("The selected relation type is not supported.");
    }
    if action == ObjectAction::Rename && driver == "sqlite" && kind == "view" {
        return Err("SQLite does not support renaming views directly.");
    }
    Ok(())
}

pub fn prepare_object_action(
    action: ObjectAction,
    driver: &str,
    kind: &str,
    object_id: &str,
    qualified_name: &str,
    new_name: &str,
    relation_subtype: &str,
) -> Result<ObjectActionStatement, &'static str> {
    object_action_availability(action, driver, kind, relation_subtype)?;
    let (schema, old_name) = identity(driver, object_id, qualified_name)
        .ok_or("The selected object has an invalid identity.")?;
    let delimiter = if driver == "mysql" { '`' } else { '"' };
    let original = format!(
        "{}.{}",
        quoted(&schema, delimiter),
        quoted(&old_name, delimiter)
    );
    let keyword = match relation_subtype {
        "materialized_view" => "MATERIALIZED VIEW",
        "foreign_table" => "FOREIGN TABLE",
        _ if kind == "table" => "TABLE",
        _ => "VIEW",
    };
    if action == ObjectAction::Drop {
        return Ok(ObjectActionStatement {
            sql: format!("DROP {keyword} {original};"),
            new_object_id: String::new(),
            new_qualified_name: original,
        });
    }
    if !valid_name(new_name, driver, true) {
        return Err("Enter a valid unqualified object name.");
    }
    if new_name == old_name {
        return Err("Enter a different object name.");
    }
    let target = format!(
        "{}.{}",
        quoted(&schema, delimiter),
        quoted(new_name, delimiter)
    );
    let sql = if driver == "mysql" {
        format!("RENAME TABLE {original} TO {target};")
    } else {
        format!(
            "ALTER {keyword} {original} RENAME TO {};",
            quoted(new_name, delimiter)
        )
    };
    let new_object_id = if driver == "postgres" {
        object_id.to_owned()
    } else {
        serde_json::to_string(&[schema.as_str(), new_name])
            .expect("validated names always serialize to JSON")
    };
    Ok(ObjectActionStatement {
        sql,
        new_object_id,
        new_qualified_name: target,
    })
}

/// Decode driver identity/display labels for presentation without treating the
/// display label as an executable or authoritative action target.
pub fn object_display_identity(object_id: &str, qualified_name: &str) -> (Option<String>, String) {
    if let Ok(parts) = serde_json::from_str::<Vec<String>>(object_id)
        && let Some(name) = parts.last().filter(|name| !name.is_empty())
    {
        return ((parts.len() >= 2).then(|| parts[0].clone()), name.clone());
    }
    let mut parts = Vec::new();
    let mut part = String::new();
    let mut quote = None;
    let mut chars = qualified_name.chars().peekable();
    while let Some(character) = chars.next() {
        if let Some(delimiter) = quote {
            if character == delimiter {
                if chars.peek() == Some(&delimiter) {
                    chars.next();
                    part.push(delimiter);
                } else {
                    quote = None;
                }
            } else {
                part.push(character);
            }
        } else if (character == '\"' || character == '`') && part.is_empty() {
            quote = Some(character);
        } else if character == '.' {
            parts.push(std::mem::take(&mut part));
        } else {
            part.push(character);
        }
    }
    if quote.is_some() {
        return (None, qualified_name.to_owned());
    }
    parts.push(part);
    let schema = (parts.len() >= 2).then(|| parts[0].clone());
    (schema, parts.pop().unwrap_or_default())
}
