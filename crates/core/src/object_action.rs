use serde_json::Value;

const MAX_NAME_UNITS: usize = 1024 * 1024;

pub const SCRIPT_SELECT_ROWS: u32 = 100;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ObjectAction {
    Drop,
    Rename,
    Truncate {
        cascade: bool,
        restart_identity: bool,
    },
    Clone {
        with_data: bool,
    },
}

impl ObjectAction {
    pub fn from_code(
        code: &str,
        cascade: bool,
        restart_identity: bool,
        with_data: bool,
    ) -> Option<Self> {
        Some(match code {
            "drop" => Self::Drop,
            "rename" => Self::Rename,
            "truncate" => Self::Truncate {
                cascade,
                restart_identity,
            },
            "clone" => Self::Clone { with_data },
            _ => return None,
        })
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct TruncateTraits {
    pub cascade: bool,
    pub restart_identity: bool,
}

pub fn truncate_traits(driver: &str) -> TruncateTraits {
    let postgres = driver == "postgres";
    TruncateTraits {
        cascade: postgres,
        restart_identity: postgres,
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ObjectActionStatement {
    pub statements: Vec<String>,
    pub sql: String,
    pub new_object_id: String,
    pub new_qualified_name: String,
}

impl ObjectActionStatement {
    fn new(statements: Vec<String>, new_object_id: String, new_qualified_name: String) -> Self {
        Self {
            sql: statements.join("\n"),
            statements,
            new_object_id,
            new_qualified_name,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ObjectScript {
    Create,
    Drop,
    Truncate,
    SelectRows,
}

impl ObjectScript {
    pub const ALL: [Self; 4] = [Self::Create, Self::Drop, Self::Truncate, Self::SelectRows];

    pub fn code(self) -> &'static str {
        match self {
            Self::Create => "create",
            Self::Drop => "drop",
            Self::Truncate => "truncate",
            Self::SelectRows => "select",
        }
    }

    pub fn label(self) -> String {
        match self {
            Self::Create => "CREATE".into(),
            Self::Drop => "DROP".into(),
            Self::Truncate => "TRUNCATE".into(),
            Self::SelectRows => format!("SELECT {SCRIPT_SELECT_ROWS} rows"),
        }
    }

    pub fn from_code(code: &str) -> Option<Self> {
        Self::ALL.into_iter().find(|script| script.code() == code)
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ObjectScriptText {
    Sql(String),
    Ddl,
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
        "mysql" | "mariadb" => {
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
    if !matches!(driver, "sqlite" | "mysql" | "mariadb") {
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

pub fn object_action_availability(
    action: ObjectAction,
    driver: &str,
    kind: &str,
    relation_subtype: &str,
) -> Result<(), &'static str> {
    if !matches!(kind, "table" | "view" | "sequence") {
        return Err("Only tables, views and sequences support this action.");
    }
    if !matches!(driver, "sqlite" | "postgres" | "mysql" | "mariadb") {
        return Err("This connection does not support this action.");
    }
    if kind == "sequence" && driver != "mariadb" {
        return Err("This connection does not support sequence actions.");
    }
    if !matches!(
        (driver, kind, relation_subtype),
        (_, _, "")
            | ("postgres", "view", "materialized_view")
            | ("postgres", "table", "foreign_table")
            | ("mariadb", "table", "system_versioned")
    ) {
        return Err("The selected relation type is not supported.");
    }
    match action {
        ObjectAction::Rename if driver == "sqlite" && kind == "view" => {
            Err("SQLite does not support renaming views directly.")
        }
        ObjectAction::Truncate { .. } | ObjectAction::Clone { .. }
            if kind != "table" || !relation_subtype.is_empty() =>
        {
            Err("Only tables support this action.")
        }
        ObjectAction::Clone { .. } if driver == "sqlite" => {
            Err("SQLite cannot clone a table with its keys and indexes.")
        }
        _ => Ok(()),
    }
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
    let mysql = matches!(driver, "mysql" | "mariadb");
    let delimiter = if mysql { '`' } else { '"' };
    let original = format!(
        "{}.{}",
        quoted(&schema, delimiter),
        quoted(&old_name, delimiter)
    );
    let keyword = match relation_subtype {
        "materialized_view" => "MATERIALIZED VIEW",
        "foreign_table" => "FOREIGN TABLE",
        _ if kind == "table" => "TABLE",
        _ if kind == "sequence" => "SEQUENCE",
        _ => "VIEW",
    };
    let truncate = truncate_traits(driver);
    let target_name = match action {
        ObjectAction::Drop => {
            return Ok(ObjectActionStatement::new(
                vec![format!("DROP {keyword} {original};")],
                String::new(),
                original,
            ));
        }
        ObjectAction::Truncate {
            cascade,
            restart_identity,
        } => {
            if cascade && !truncate.cascade {
                return Err("This connection does not support CASCADE for truncate.");
            }
            if restart_identity && !truncate.restart_identity {
                return Err("This connection does not support RESTART IDENTITY for truncate.");
            }
            let sql = if driver == "sqlite" {
                format!("DELETE FROM {original};")
            } else {
                format!(
                    "TRUNCATE TABLE {original}{}{};",
                    if restart_identity {
                        " RESTART IDENTITY"
                    } else {
                        ""
                    },
                    if cascade { " CASCADE" } else { "" }
                )
            };
            return Ok(ObjectActionStatement::new(
                vec![sql],
                String::new(),
                original,
            ));
        }
        ObjectAction::Rename | ObjectAction::Clone { .. } => new_name,
    };
    if !valid_name(target_name, driver, true) {
        return Err("Enter a valid unqualified object name.");
    }
    if target_name == old_name {
        return Err("Enter a different object name.");
    }
    let target = format!(
        "{}.{}",
        quoted(&schema, delimiter),
        quoted(target_name, delimiter)
    );
    let json_id = || {
        serde_json::to_string(&[schema.as_str(), target_name])
            .expect("validated names always serialize to JSON")
    };
    if let ObjectAction::Clone { with_data } = action {
        let mut statements = vec![if mysql {
            format!("CREATE TABLE {target} LIKE {original};")
        } else {
            format!("CREATE TABLE {target} (LIKE {original} INCLUDING ALL);")
        }];
        if with_data {
            statements.push(if mysql {
                format!("INSERT INTO {target} SELECT * FROM {original};")
            } else {
                format!("INSERT INTO {target} OVERRIDING SYSTEM VALUE SELECT * FROM {original};")
            });
        }
        let new_object_id = if mysql { json_id() } else { String::new() };
        return Ok(ObjectActionStatement::new(
            statements,
            new_object_id,
            target,
        ));
    }
    let sql = if mysql {
        format!("RENAME TABLE {original} TO {target};")
    } else {
        format!(
            "ALTER {keyword} {original} RENAME TO {};",
            quoted(target_name, delimiter)
        )
    };
    let new_object_id = if driver == "postgres" {
        object_id.to_owned()
    } else {
        json_id()
    };
    Ok(ObjectActionStatement::new(vec![sql], new_object_id, target))
}

pub fn object_script_availability(
    script: ObjectScript,
    driver: &str,
    kind: &str,
    relation_subtype: &str,
) -> Result<(), &'static str> {
    match script {
        ObjectScript::Create | ObjectScript::Drop => {
            object_action_availability(ObjectAction::Drop, driver, kind, relation_subtype)
        }
        ObjectScript::Truncate => object_action_availability(
            ObjectAction::Truncate {
                cascade: false,
                restart_identity: false,
            },
            driver,
            kind,
            relation_subtype,
        ),
        ObjectScript::SelectRows if kind == "sequence" => {
            Err("Only tables and views support this script.")
        }
        ObjectScript::SelectRows => {
            object_action_availability(ObjectAction::Drop, driver, kind, relation_subtype)
        }
    }
}

pub fn object_script(
    script: ObjectScript,
    driver: &str,
    kind: &str,
    object_id: &str,
    qualified_name: &str,
    relation_subtype: &str,
) -> Result<ObjectScriptText, &'static str> {
    object_script_availability(script, driver, kind, relation_subtype)?;
    let action = match script {
        ObjectScript::Create => {
            identity(driver, object_id, qualified_name)
                .ok_or("The selected object has an invalid identity.")?;
            return Ok(ObjectScriptText::Ddl);
        }
        ObjectScript::Drop => ObjectAction::Drop,
        ObjectScript::Truncate => ObjectAction::Truncate {
            cascade: false,
            restart_identity: false,
        },
        ObjectScript::SelectRows => {
            let original = prepare_object_action(
                ObjectAction::Drop,
                driver,
                kind,
                object_id,
                qualified_name,
                "",
                relation_subtype,
            )?
            .new_qualified_name;
            return Ok(ObjectScriptText::Sql(format!(
                "SELECT * FROM {original} LIMIT {SCRIPT_SELECT_ROWS};"
            )));
        }
    };
    prepare_object_action(
        action,
        driver,
        kind,
        object_id,
        qualified_name,
        "",
        relation_subtype,
    )
    .map(|statement| ObjectScriptText::Sql(statement.sql))
}

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
