use choscordb_driver_api::{DriverError, ErrorKind, GridEditDriver, quote_import_identifier};

fn invalid(message: impl Into<String>) -> DriverError {
    DriverError::new(ErrorKind::InvalidInput, message)
}

fn unsupported(message: &str) -> DriverError {
    DriverError::new(ErrorKind::Unsupported, message)
}

const SWITCH_FIRST: &str = "Switch to another database before you rename or drop this one.";
const SETTING_RULE: &str =
    "An encoding or a collation has only letters, digits, dots, hyphens, underscores and @.";
const MAX_SETTING_CHARS: usize = 64;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DatabaseActionKind {
    Create,
    Rename,
    Drop,
}

impl DatabaseActionKind {
    pub const ALL: [Self; 3] = [Self::Create, Self::Rename, Self::Drop];
    pub fn code(self) -> &'static str {
        match self {
            Self::Create => "create",
            Self::Rename => "rename",
            Self::Drop => "drop",
        }
    }
    pub fn label(self) -> &'static str {
        match self {
            Self::Create => "New database",
            Self::Rename => "Rename database",
            Self::Drop => "Drop database",
        }
    }
    pub fn from_code(code: &str) -> Option<Self> {
        Self::ALL.into_iter().find(|kind| kind.code() == code)
    }
    pub fn fields(self) -> DatabaseActionFields {
        DatabaseActionFields {
            existing: self != Self::Create,
            new_name: self == Self::Rename,
            settings: self == Self::Create,
            confirmation: self == Self::Drop,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct DatabaseActionFields {
    pub existing: bool,
    pub new_name: bool,
    pub settings: bool,
    pub confirmation: bool,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DatabaseActionRequest {
    pub driver: String,
    pub kind: DatabaseActionKind,
    pub current: String,
    pub name: String,
    pub new_name: String,
    pub encoding: String,
    pub collation: String,
    pub confirmation: String,
}

fn driver(name: &str) -> Result<GridEditDriver, DriverError> {
    match GridEditDriver::from_id(name) {
        Some(GridEditDriver::Sqlite) => Err(unsupported(
            "SQLite keeps each database in its own file. Create, rename or delete the file instead.",
        )),
        Some(driver) => Ok(driver),
        None => Err(unsupported("This connection cannot manage databases.")),
    }
}

fn name(driver: GridEditDriver, text: &str) -> Result<String, DriverError> {
    if text.trim().is_empty() {
        return Err(invalid("Enter a database name."));
    }
    if text.len() > 63 {
        return Err(invalid("A database name has at most 63 bytes."));
    }
    quote_import_identifier(driver, text)
        .map_err(|_| invalid("A database name cannot contain a NUL character."))
}

fn setting(text: &str) -> Result<Option<&str>, DriverError> {
    let text = text.trim();
    if text.is_empty() {
        return Ok(None);
    }
    let valid = text.chars().count() <= MAX_SETTING_CHARS
        && text
            .chars()
            .all(|c| c.is_ascii_alphanumeric() || matches!(c, '.' | '-' | '_' | '@'));
    valid
        .then_some(Some(text))
        .ok_or_else(|| invalid(SETTING_RULE))
}

pub fn plan_database_action(request: &DatabaseActionRequest) -> Result<String, DriverError> {
    let driver = driver(&request.driver)?;
    let quoted = name(driver, &request.name)?;
    match request.kind {
        DatabaseActionKind::Create => {
            let encoding = setting(&request.encoding)?;
            let collation = setting(&request.collation)?;
            let mut sql = format!("CREATE DATABASE {quoted}");
            match driver {
                GridEditDriver::Mysql => {
                    if let Some(encoding) = encoding {
                        sql.push_str(&format!(" CHARACTER SET {encoding}"));
                    }
                    if let Some(collation) = collation {
                        sql.push_str(&format!(" COLLATE {collation}"));
                    }
                }
                _ => {
                    if encoding.is_some() || collation.is_some() {
                        sql.push_str(" TEMPLATE template0");
                    }
                    if let Some(encoding) = encoding {
                        sql.push_str(&format!(" ENCODING '{encoding}'"));
                    }
                    if let Some(collation) = collation {
                        sql.push_str(&format!(" LC_COLLATE '{collation}' LC_CTYPE '{collation}'"));
                    }
                }
            }
            sql.push(';');
            Ok(sql)
        }
        DatabaseActionKind::Rename => {
            if driver == GridEditDriver::Mysql {
                return Err(unsupported("MySQL and MariaDB cannot rename a database."));
            }
            if request.name == request.current {
                return Err(invalid(SWITCH_FIRST));
            }
            let new_name = name(driver, &request.new_name)?;
            if request.new_name == request.name {
                return Err(invalid(
                    "Enter a new name that is different from the old name.",
                ));
            }
            Ok(format!("ALTER DATABASE {quoted} RENAME TO {new_name};"))
        }
        DatabaseActionKind::Drop => {
            if request.name == request.current {
                return Err(invalid(SWITCH_FIRST));
            }
            if request.confirmation != request.name {
                return Err(invalid(format!(
                    "Type the database name {} to drop it.",
                    request.name
                )));
            }
            Ok(format!("DROP DATABASE {quoted};"))
        }
    }
}
