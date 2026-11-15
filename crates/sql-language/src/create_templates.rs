#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CreateTemplate {
    Table,
    View,
    Function,
    Procedure,
    Schema,
}

impl CreateTemplate {
    pub const ALL: [Self; 5] = [
        Self::Table,
        Self::View,
        Self::Function,
        Self::Procedure,
        Self::Schema,
    ];

    pub fn code(self) -> &'static str {
        match self {
            Self::Table => "table",
            Self::View => "view",
            Self::Function => "function",
            Self::Procedure => "procedure",
            Self::Schema => "schema",
        }
    }

    pub fn label(self) -> &'static str {
        match self {
            Self::Table => "New table",
            Self::View => "New view",
            Self::Function => "New function",
            Self::Procedure => "New procedure",
            Self::Schema => "New schema",
        }
    }

    pub fn from_code(code: &str) -> Option<Self> {
        Self::ALL
            .into_iter()
            .find(|template| template.code() == code)
    }
}

pub fn create_template_parent(parent_kind: &str) -> Option<bool> {
    match parent_kind {
        "connection" | "database" => Some(false),
        "schema" => Some(true),
        _ => None,
    }
}

pub fn create_template_unavailable(template: CreateTemplate, driver: &str) -> Option<&'static str> {
    match driver {
        "postgres" | "mysql" | "mariadb" => None,
        "sqlite" if matches!(template, CreateTemplate::Table | CreateTemplate::View) => None,
        "sqlite" => Some("SQLite does not support this object type."),
        _ => Some("This connection does not support this object type."),
    }
}

pub fn create_template(
    template: CreateTemplate,
    driver: &str,
    parent_kind: &str,
    parent_qualified: &str,
) -> Result<String, &'static str> {
    if let Some(reason) = create_template_unavailable(template, driver) {
        return Err(reason);
    }
    let qualify = create_template_parent(parent_kind)
        .ok_or("Select a connection, database or schema to create an object.")?;
    if qualify && (parent_qualified.is_empty() || parent_qualified.contains('\0')) {
        return Err("The selected schema has an invalid name.");
    }
    let prefix = if qualify && template != CreateTemplate::Schema {
        format!("{parent_qualified}.")
    } else {
        String::new()
    };
    let mysql = matches!(driver, "mysql" | "mariadb");
    Ok(match template {
        CreateTemplate::Table => {
            let key = match driver {
                "postgres" => "id bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY",
                "sqlite" => "id INTEGER PRIMARY KEY",
                _ => "id BIGINT AUTO_INCREMENT PRIMARY KEY",
            };
            format!("CREATE TABLE {prefix}new_table (\n  {key}\n);")
        }
        CreateTemplate::View => format!("CREATE VIEW {prefix}new_view AS\nSELECT 1 AS value;"),
        CreateTemplate::Function if mysql => {
            format!("CREATE FUNCTION {prefix}new_function()\nRETURNS INT\nDETERMINISTIC\nRETURN 1;")
        }
        CreateTemplate::Function => format!(
            "CREATE FUNCTION {prefix}new_function()\nRETURNS integer\nLANGUAGE sql\nAS $$\n  SELECT 1;\n$$;"
        ),
        CreateTemplate::Procedure if mysql => {
            format!("CREATE PROCEDURE {prefix}new_procedure()\nSELECT 1;")
        }
        CreateTemplate::Procedure => format!(
            "CREATE PROCEDURE {prefix}new_procedure()\nLANGUAGE sql\nAS $$\n  SELECT 1;\n$$;"
        ),
        CreateTemplate::Schema if mysql => "CREATE DATABASE new_database;".into(),
        CreateTemplate::Schema => "CREATE SCHEMA new_schema;".into(),
    })
}
