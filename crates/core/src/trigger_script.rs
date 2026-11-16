use crate::object_action::{identity, quoted};
use choscordb_driver_api::{DriverError, ErrorKind, GridEditDriver};

fn invalid(message: &str) -> DriverError {
    DriverError::new(ErrorKind::InvalidInput, message)
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TriggerScript {
    Create,
    Edit,
    Drop,
}

impl TriggerScript {
    pub const ALL: [Self; 3] = [Self::Create, Self::Edit, Self::Drop];
    pub fn code(self) -> &'static str {
        match self {
            Self::Create => "create",
            Self::Edit => "edit",
            Self::Drop => "drop",
        }
    }
    pub fn label(self) -> &'static str {
        match self {
            Self::Create => "New trigger",
            Self::Edit => "Edit trigger",
            Self::Drop => "Drop trigger",
        }
    }
    pub fn needs_trigger(self) -> bool {
        self != Self::Create
    }
    pub fn from_code(code: &str) -> Option<Self> {
        Self::ALL.into_iter().find(|script| script.code() == code)
    }
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct TriggerScriptRequest {
    pub driver: String,
    pub table_id: String,
    pub table_name: String,
    pub trigger: String,
    pub definition: String,
}

fn dialect(driver: &str) -> Result<GridEditDriver, DriverError> {
    GridEditDriver::from_id(driver).ok_or_else(|| {
        DriverError::new(
            ErrorKind::Unsupported,
            "This connection cannot manage triggers.",
        )
    })
}

fn drop_statement(dialect: &GridEditDriver, schema: &str, table: &str, trigger: &str) -> String {
    match dialect {
        GridEditDriver::Postgres => format!(
            "DROP TRIGGER {} ON {}.{};",
            quoted(trigger, '"'),
            quoted(schema, '"'),
            quoted(table, '"')
        ),
        GridEditDriver::Mysql => format!(
            "DROP TRIGGER {}.{};",
            quoted(schema, '`'),
            quoted(trigger, '`')
        ),
        GridEditDriver::Sqlite => format!(
            "DROP TRIGGER {}.{};",
            quoted(schema, '"'),
            quoted(trigger, '"')
        ),
    }
}

fn create_template(dialect: &GridEditDriver, schema: &str, table: &str) -> String {
    let trigger = format!("{table}_trigger");
    match dialect {
        GridEditDriver::Postgres => {
            let function = format!("{}.{}", quoted(schema, '"'), quoted(&trigger, '"'));
            format!(
                "CREATE OR REPLACE FUNCTION {function}() RETURNS trigger\nLANGUAGE plpgsql AS $$\nBEGIN\n  RETURN NEW;\nEND;\n$$;\n\nCREATE TRIGGER {} BEFORE INSERT OR UPDATE ON {}.{}\nFOR EACH ROW EXECUTE FUNCTION {function}();",
                quoted(&trigger, '"'),
                quoted(schema, '"'),
                quoted(table, '"')
            )
        }
        GridEditDriver::Mysql => format!(
            "CREATE TRIGGER {}.{} BEFORE INSERT ON {}.{}\nFOR EACH ROW\nBEGIN\n  SET @changed_at = NOW();\nEND;",
            quoted(schema, '`'),
            quoted(&trigger, '`'),
            quoted(schema, '`'),
            quoted(table, '`')
        ),
        GridEditDriver::Sqlite => format!(
            "CREATE TRIGGER {}.{} AFTER INSERT ON {}\nFOR EACH ROW\nBEGIN\n  SELECT NEW.rowid;\nEND;",
            quoted(schema, '"'),
            quoted(&trigger, '"'),
            quoted(table, '"')
        ),
    }
}

pub fn trigger_script(
    script: TriggerScript,
    request: &TriggerScriptRequest,
) -> Result<String, DriverError> {
    let dialect = dialect(&request.driver)?;
    let (schema, table) = identity(&request.driver, &request.table_id, &request.table_name)
        .ok_or_else(|| invalid("The selected table has an invalid identity."))?;
    if script == TriggerScript::Create {
        return Ok(create_template(&dialect, &schema, &table));
    }
    if request.trigger.is_empty() {
        return Err(invalid("Choose a trigger."));
    }
    let drop = drop_statement(&dialect, &schema, &table, &request.trigger);
    if script == TriggerScript::Drop {
        return Ok(drop);
    }
    let definition = request.definition.trim().trim_end_matches(';').trim_end();
    if definition.is_empty() {
        return Err(invalid("The trigger definition is unavailable."));
    }
    let delimiter = match dialect {
        GridEditDriver::Postgres => None,
        GridEditDriver::Mysql => Some('`'),
        GridEditDriver::Sqlite => Some('"'),
    };
    if let Some(delimiter) = delimiter
        && !definition.starts_with(&format!("CREATE TRIGGER {}.", quoted(&schema, delimiter)))
    {
        return Err(invalid(
            "The trigger definition does not name the schema of its table.",
        ));
    }
    Ok(format!("{drop}\n\n{definition};"))
}
