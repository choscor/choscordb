use crate::GridEditDriver;
use crate::grid_edit::{identifier_delimiter, quoted, valid_qualified};
use std::collections::BTreeMap;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum StructureFieldKind {
    Text,
    Names,
    Type,
    Flag,
    Column,
    Columns,
    Index,
    Key,
    Nullability,
    Action,
}

impl StructureFieldKind {
    pub fn input(self) -> &'static str {
        match self {
            Self::Text | Self::Names | Self::Columns => "text",
            Self::Flag => "flag",
            Self::Type => "editable_choice",
            Self::Column | Self::Index | Self::Key | Self::Nullability | Self::Action => "choice",
        }
    }

    pub fn names(self) -> &'static str {
        match self {
            Self::Column => "table_columns",
            Self::Index => "table_indexes",
            Self::Key => "table_keys",
            _ => "",
        }
    }

    pub fn choices(self) -> &'static [(&'static str, &'static str)] {
        match self {
            Self::Nullability => &[
                ("keep", "Keep the NULL rule"),
                ("null", "Allow NULL"),
                ("not_null", "Require a value"),
            ],
            Self::Action => &[
                ("", "Database default"),
                ("no_action", "NO ACTION"),
                ("restrict", "RESTRICT"),
                ("cascade", "CASCADE"),
                ("set_null", "SET NULL"),
            ],
            _ => &[],
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct StructureField {
    pub key: &'static str,
    pub label: &'static str,
    pub kind: StructureFieldKind,
    pub required: bool,
}

const fn field(
    key: &'static str,
    label: &'static str,
    kind: StructureFieldKind,
    required: bool,
) -> StructureField {
    StructureField {
        key,
        label,
        kind,
        required,
    }
}

use StructureFieldKind as F;

const ADD_COLUMN: &[StructureField] = &[
    field("name", "Name", F::Text, true),
    field("type", "Type", F::Type, true),
    field("not_null", "Require a value (NOT NULL)", F::Flag, false),
    field("default", "Default expression", F::Text, false),
    field("comment", "Comment", F::Text, false),
];
const CHANGE_COLUMN: &[StructureField] = &[
    field("column", "Column", F::Column, true),
    field("name", "New name", F::Text, false),
    field("type", "New type", F::Type, false),
    field("nullability", "NULL rule", F::Nullability, false),
    field("default", "New default expression", F::Text, false),
    field("drop_default", "Remove the default", F::Flag, false),
    field("comment", "New comment", F::Text, false),
];
const DROP_COLUMN: &[StructureField] = &[field("column", "Column", F::Column, true)];
const ADD_INDEX: &[StructureField] = &[
    field("name", "Name", F::Text, true),
    field("columns", "Columns", F::Columns, true),
    field("unique", "Unique", F::Flag, false),
];
const DROP_INDEX: &[StructureField] = &[field("index", "Index", F::Index, true)];
const PRIMARY_KEY: &[StructureField] = &[field("columns", "Columns", F::Columns, true)];
const ADD_FOREIGN_KEY: &[StructureField] = &[
    field("name", "Name", F::Text, false),
    field("columns", "Columns", F::Columns, true),
    field("ref_table", "Referenced table", F::Text, true),
    field("ref_columns", "Referenced columns", F::Names, true),
    field("on_update", "On update", F::Action, false),
    field("on_delete", "On delete", F::Action, false),
];
const DROP_KEY: &[StructureField] = &[field("key", "Key", F::Key, true)];

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum StructurePane {
    Columns,
    Indexes,
    Keys,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum StructureChangeKind {
    AddColumn,
    ChangeColumn,
    DropColumn,
    AddIndex,
    DropIndex,
    PrimaryKey,
    AddForeignKey,
    DropKey,
}

impl StructureChangeKind {
    pub const ALL: [Self; 8] = [
        Self::AddColumn,
        Self::ChangeColumn,
        Self::DropColumn,
        Self::AddIndex,
        Self::DropIndex,
        Self::PrimaryKey,
        Self::AddForeignKey,
        Self::DropKey,
    ];

    pub fn code(self) -> &'static str {
        match self {
            Self::AddColumn => "add_column",
            Self::ChangeColumn => "change_column",
            Self::DropColumn => "drop_column",
            Self::AddIndex => "add_index",
            Self::DropIndex => "drop_index",
            Self::PrimaryKey => "primary_key",
            Self::AddForeignKey => "add_foreign_key",
            Self::DropKey => "drop_key",
        }
    }

    pub fn label(self) -> &'static str {
        match self {
            Self::AddColumn => "Add column",
            Self::ChangeColumn => "Change column",
            Self::DropColumn => "Drop column",
            Self::AddIndex => "Add index",
            Self::DropIndex => "Drop index",
            Self::PrimaryKey => "Set primary key",
            Self::AddForeignKey => "Add foreign key",
            Self::DropKey => "Drop key",
        }
    }

    pub fn pane(self) -> StructurePane {
        match self {
            Self::AddColumn | Self::ChangeColumn | Self::DropColumn => StructurePane::Columns,
            Self::AddIndex | Self::DropIndex => StructurePane::Indexes,
            Self::PrimaryKey | Self::AddForeignKey | Self::DropKey => StructurePane::Keys,
        }
    }

    pub fn from_code(code: &str) -> Option<Self> {
        Self::ALL.into_iter().find(|kind| kind.code() == code)
    }

    pub fn fields(self) -> &'static [StructureField] {
        match self {
            Self::AddColumn => ADD_COLUMN,
            Self::ChangeColumn => CHANGE_COLUMN,
            Self::DropColumn => DROP_COLUMN,
            Self::AddIndex => ADD_INDEX,
            Self::DropIndex => DROP_INDEX,
            Self::PrimaryKey => PRIMARY_KEY,
            Self::AddForeignKey => ADD_FOREIGN_KEY,
            Self::DropKey => DROP_KEY,
        }
    }

    pub fn unavailable(
        self,
        dialect: &dyn StructureDialect,
        new_table: bool,
    ) -> Option<&'static str> {
        let creates = matches!(
            self,
            Self::AddColumn | Self::AddIndex | Self::PrimaryKey | Self::AddForeignKey
        );
        if new_table && !creates {
            return Some("A new table has no columns, indexes or keys to change yet.");
        }
        if new_table {
            return None;
        }
        dialect.rebuild_reason(self)
    }
}

pub trait StructureDialect: Send + Sync {
    fn sql(&self) -> GridEditDriver;
    fn column_types(&self) -> &'static [&'static str];
    fn rebuild_reason(&self, kind: StructureChangeKind) -> Option<&'static str>;
    fn column_alter_reason(&self) -> Option<&'static str>;
    fn column_comment_reason(&self) -> Option<&'static str>;
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct StructureColumn {
    pub name: String,
    pub data_type: String,
    pub nullable: bool,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct StructureKey {
    pub name: String,
    pub kind: String,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct StructureTable {
    pub qualified_name: String,
    pub new_table: bool,
    pub columns: Vec<StructureColumn>,
    pub indexes: Vec<String>,
    pub keys: Vec<StructureKey>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct StructureChange {
    pub kind: StructureChangeKind,
    pub values: BTreeMap<String, Vec<String>>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct StructureError {
    pub change: Option<usize>,
    pub message: String,
}

impl std::fmt::Display for StructureError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self.change {
            Some(index) => write!(f, "Change {}: {}", index + 1, self.message),
            None => f.write_str(&self.message),
        }
    }
}

impl std::error::Error for StructureError {}

type Checked<T> = Result<T, String>;

impl StructureChange {
    fn text(&self, key: &str) -> Option<String> {
        self.values
            .get(key)
            .and_then(|values| values.first())
            .map(|value| value.trim().to_owned())
            .filter(|value| !value.is_empty())
    }

    fn required(&self, key: &str) -> Checked<String> {
        self.text(key).ok_or_else(|| {
            let label = self
                .kind
                .fields()
                .iter()
                .find(|field| field.key == key)
                .map_or(key, |field| field.label);
            format!("Enter a value for {label}.")
        })
    }

    fn flag(&self, key: &str) -> bool {
        self.text(key).is_some_and(|value| value == "true")
    }

    fn list(&self, key: &str) -> Vec<String> {
        self.values
            .get(key)
            .into_iter()
            .flatten()
            .flat_map(|value| value.split(','))
            .map(|value| value.trim().to_owned())
            .filter(|value| !value.is_empty())
            .collect()
    }
}

pub fn structure_change_summary(change: &StructureChange) -> String {
    let subject = ["name", "column", "index", "key"]
        .iter()
        .find_map(|key| change.text(key))
        .or_else(|| {
            let columns = change.list("columns");
            (!columns.is_empty()).then(|| columns.join(", "))
        })
        .unwrap_or_default();
    format!("{} {subject}", change.kind.label())
        .trim_end()
        .to_owned()
}

struct Dialect {
    driver: GridEditDriver,
}

impl Dialect {
    fn mysql(&self) -> bool {
        self.driver == GridEditDriver::Mysql
    }

    fn name(&self, name: &str) -> Checked<String> {
        quoted(name, identifier_delimiter(self.driver))
            .map_err(|_| format!("The name {name:?} is not valid."))
    }

    fn joined(&self, names: &[String], separator: &str) -> Checked<String> {
        names
            .iter()
            .map(|name| self.name(name))
            .collect::<Checked<Vec<_>>>()
            .map(|names| names.join(separator))
    }

    fn names(&self, names: &[String]) -> Checked<String> {
        self.joined(names, ", ")
    }

    fn dotted(&self, name: &str) -> Checked<String> {
        self.joined(&name.split('.').map(str::to_owned).collect::<Vec<_>>(), ".")
    }

    fn literal(&self, text: &str) -> Checked<String> {
        if self.mysql() && text.contains('\\') {
            return Err("A MySQL column comment cannot contain a backslash.".into());
        }
        Ok(format!("'{}'", text.replace('\'', "''")))
    }
}

fn hides_more_sql(text: &str) -> bool {
    let mut depth = 0usize;
    let mut quote: Option<char> = None;
    for c in text.chars() {
        match (quote, c) {
            (Some(open), _) if c == open => quote = None,
            (Some(_), _) => {}
            (None, '\'' | '"') => quote = Some(c),
            (None, '(') => depth += 1,
            (None, ')') if depth == 0 => return true,
            (None, ')') => depth -= 1,
            (None, ',') if depth == 0 => return true,
            (None, '$') => return true,
            _ => {}
        }
    }
    depth > 0 || quote.is_some()
}

fn data_type(text: &str) -> Checked<String> {
    let valid = text.len() <= 128
        && !hides_more_sql(text)
        && text
            .chars()
            .all(|c| c.is_ascii_alphanumeric() || matches!(c, ' ' | '_' | '(' | ')' | ',' | '.'));
    if valid {
        Ok(text.to_owned())
    } else {
        Err(format!("The type {text:?} is not valid."))
    }
}

fn expression(text: &str) -> Checked<String> {
    if text.len() > 1024
        || text.contains(';')
        || text.contains('\0')
        || text.contains("--")
        || text.contains("/*")
        || text.contains('#')
        || text.contains('\\')
        || hides_more_sql(text)
    {
        return Err(
            "The default expression cannot contain ;, --, /*, # or a backslash. Its strings and parentheses must close. A $ must be in a string, and a comma in a string or parentheses."
                .into(),
        );
    }
    Ok(text.to_owned())
}

fn action(code: Option<String>) -> Checked<Option<&'static str>> {
    match code.as_deref() {
        None => Ok(None),
        Some("no_action") => Ok(Some("NO ACTION")),
        Some("restrict") => Ok(Some("RESTRICT")),
        Some("cascade") => Ok(Some("CASCADE")),
        Some("set_null") => Ok(Some("SET NULL")),
        Some(other) => Err(format!("The referential action {other:?} is not valid.")),
    }
}

fn split_qualified(name: &str, delimiter: char) -> Vec<String> {
    let mut parts = Vec::new();
    let mut part = String::new();
    let mut chars = name.chars().peekable();
    let mut inside = false;
    while let Some(c) = chars.next() {
        match (inside, c) {
            (true, c) if c == delimiter && chars.peek() == Some(&delimiter) => {
                chars.next();
                part.push(c);
            }
            (true, c) if c == delimiter => inside = false,
            (true, c) => part.push(c),
            (false, c) if c == delimiter => inside = true,
            (false, '.') => parts.push(std::mem::take(&mut part)),
            (false, _) => {}
        }
    }
    parts.push(part);
    parts
}

struct Planner<'a> {
    table: &'a StructureTable,
    rules: &'a dyn StructureDialect,
    dialect: Dialect,
    columns: Vec<StructureColumn>,
    indexes: Vec<String>,
    definitions: Vec<String>,
    constraints: Vec<String>,
    statements: Vec<String>,
    after: Vec<String>,
    primary_key: bool,
}

impl Planner<'_> {
    fn column(&self, name: &str) -> Checked<StructureColumn> {
        self.columns
            .iter()
            .find(|column| column.name == name)
            .cloned()
            .ok_or_else(|| format!("The table has no column {name}."))
    }

    fn existing_columns(&self, names: Vec<String>) -> Checked<String> {
        if names.is_empty() {
            return Err("Choose one or more columns.".into());
        }
        for name in &names {
            self.column(name)?;
        }
        self.dialect.names(&names)
    }

    fn alter(&mut self, clause: String) {
        self.statements.push(format!(
            "ALTER TABLE {} {clause}",
            self.table.qualified_name
        ));
    }

    fn comment(&mut self, column: &str, comment: &str) -> Checked<()> {
        let statement = format!(
            "COMMENT ON COLUMN {}.{} IS {}",
            self.table.qualified_name,
            self.dialect.name(column)?,
            self.dialect.literal(comment)?
        );
        if self.table.new_table {
            self.after.push(statement);
        } else {
            self.statements.push(statement);
        }
        Ok(())
    }

    fn column_definition(
        &self,
        name: &str,
        data_type: &str,
        nullable: bool,
        default: Option<&str>,
        comment: Option<&str>,
    ) -> Checked<String> {
        let mut definition = format!("{} {data_type}", self.dialect.name(name)?);
        definition.push_str(if nullable { "" } else { " NOT NULL" });
        if let Some(default) = default {
            definition.push_str(&format!(" DEFAULT {default}"));
        }
        if let (true, Some(comment)) = (self.dialect.mysql(), comment) {
            definition.push_str(&format!(" COMMENT {}", self.dialect.literal(comment)?));
        }
        Ok(definition)
    }

    fn table_parts(&self) -> Vec<String> {
        split_qualified(
            &self.table.qualified_name,
            identifier_delimiter(self.dialect.driver),
        )
    }

    fn index_name(&self, name: &str) -> Checked<String> {
        let mut parts = self.table_parts();
        parts.pop();
        parts.push(name.to_owned());
        self.dialect.joined(&parts, ".")
    }

    fn apply(&mut self, change: &StructureChange) -> Checked<()> {
        if let Some(reason) = change.kind.unavailable(self.rules, self.table.new_table) {
            return Err(reason.into());
        }
        match change.kind {
            StructureChangeKind::AddColumn => self.add_column(change),
            StructureChangeKind::ChangeColumn => self.change_column(change),
            StructureChangeKind::DropColumn => {
                let column = change.required("column")?;
                self.column(&column)?;
                self.columns.retain(|existing| existing.name != column);
                let clause = format!("DROP COLUMN {}", self.dialect.name(&column)?);
                self.alter(clause);
                Ok(())
            }
            StructureChangeKind::AddIndex => self.add_index(change),
            StructureChangeKind::DropIndex => {
                let index = change.required("index")?;
                if !self.indexes.contains(&index) {
                    return Err(format!("The table has no index {index}."));
                }
                self.indexes.retain(|existing| *existing != index);
                let statement = if self.dialect.mysql() {
                    format!(
                        "DROP INDEX {} ON {}",
                        self.dialect.name(&index)?,
                        self.table.qualified_name
                    )
                } else {
                    format!("DROP INDEX {}", self.index_name(&index)?)
                };
                self.statements.push(statement);
                Ok(())
            }
            StructureChangeKind::PrimaryKey => {
                if self.primary_key {
                    return Err("A table has only one primary key.".into());
                }
                self.primary_key = true;
                let columns = self.existing_columns(change.list("columns"))?;
                if self.table.new_table {
                    self.constraints.push(format!("PRIMARY KEY ({columns})"));
                } else {
                    self.alter(format!("ADD PRIMARY KEY ({columns})"));
                }
                Ok(())
            }
            StructureChangeKind::AddForeignKey => self.add_foreign_key(change),
            StructureChangeKind::DropKey => {
                let name = change.required("key")?;
                let key = self
                    .table
                    .keys
                    .iter()
                    .find(|key| key.name == name)
                    .ok_or_else(|| format!("The table has no key {name}."))?;
                let clause = match (self.dialect.mysql(), key.kind.as_str()) {
                    (true, "primarykey") => "DROP PRIMARY KEY".into(),
                    (true, "foreignkey") => {
                        format!("DROP FOREIGN KEY {}", self.dialect.name(&name)?)
                    }
                    (true, _) => format!("DROP INDEX {}", self.dialect.name(&name)?),
                    (false, _) => format!("DROP CONSTRAINT {}", self.dialect.name(&name)?),
                };
                self.alter(clause);
                Ok(())
            }
        }
    }

    fn add_column(&mut self, change: &StructureChange) -> Checked<()> {
        let name = change.required("name")?;
        if self.columns.iter().any(|column| column.name == name) {
            return Err(format!("The table already has a column {name}."));
        }
        let data_type = data_type(&change.required("type")?)?;
        let nullable = !change.flag("not_null");
        let default = change.text("default").map(|d| expression(&d)).transpose()?;
        let comment = change.text("comment");
        if comment.is_some()
            && let Some(reason) = self.rules.column_comment_reason()
        {
            return Err(reason.into());
        }
        let definition = self.column_definition(
            &name,
            &data_type,
            nullable,
            default.as_deref(),
            comment.as_deref(),
        )?;
        if self.table.new_table {
            self.definitions.push(definition);
        } else {
            self.alter(format!("ADD COLUMN {definition}"));
        }
        if let (false, Some(comment)) = (self.dialect.mysql(), &comment) {
            self.comment(&name, comment)?;
        }
        self.columns.push(StructureColumn {
            name,
            data_type,
            nullable,
        });
        Ok(())
    }

    fn change_column(&mut self, change: &StructureChange) -> Checked<()> {
        let current = self.column(&change.required("column")?)?;
        let rename = change.text("name").filter(|name| *name != current.name);
        let new_type = change.text("type").map(|t| data_type(&t)).transpose()?;
        let nullable = match change.text("nullability").as_deref() {
            None | Some("keep") => None,
            Some("null") => Some(true),
            Some("not_null") => Some(false),
            Some(other) => return Err(format!("The NULL rule {other:?} is not valid.")),
        };
        let default = change.text("default").map(|d| expression(&d)).transpose()?;
        let drop_default = change.flag("drop_default");
        let comment = change.text("comment");
        if default.is_some() && drop_default {
            return Err("Enter a default or remove the default, not both.".into());
        }
        let alters = new_type.is_some()
            || nullable.is_some()
            || default.is_some()
            || drop_default
            || comment.is_some();
        if rename.is_none() && !alters {
            return Err("Choose what to change in the column.".into());
        }
        if alters && let Some(reason) = self.rules.column_alter_reason() {
            return Err(reason.into());
        }
        if let Some(name) = &rename
            && self.columns.iter().any(|column| column.name == *name)
        {
            return Err(format!("The table already has a column {name}."));
        }
        let column = self.dialect.name(&current.name)?;
        if self.dialect.mysql() {
            if new_type.is_some() || nullable.is_some() || comment.is_some() {
                let definition = self.column_definition(
                    &current.name,
                    new_type.as_deref().unwrap_or(&current.data_type),
                    nullable.unwrap_or(current.nullable),
                    default.as_deref(),
                    comment.as_deref(),
                )?;
                self.alter(format!("MODIFY COLUMN {definition}"));
            } else if let Some(default) = &default {
                self.alter(format!("ALTER COLUMN {column} SET DEFAULT {default}"));
            }
            if drop_default {
                self.alter(format!("ALTER COLUMN {column} DROP DEFAULT"));
            }
        } else {
            if let Some(new_type) = &new_type {
                self.alter(format!("ALTER COLUMN {column} TYPE {new_type}"));
            }
            match nullable {
                Some(true) => self.alter(format!("ALTER COLUMN {column} DROP NOT NULL")),
                Some(false) => self.alter(format!("ALTER COLUMN {column} SET NOT NULL")),
                None => {}
            }
            if let Some(default) = &default {
                self.alter(format!("ALTER COLUMN {column} SET DEFAULT {default}"));
            }
            if drop_default {
                self.alter(format!("ALTER COLUMN {column} DROP DEFAULT"));
            }
            if let Some(comment) = &comment {
                self.comment(&current.name, comment)?;
            }
        }
        if let Some(name) = rename {
            let clause = format!("RENAME COLUMN {column} TO {}", self.dialect.name(&name)?);
            self.alter(clause);
            if let Some(entry) = self
                .columns
                .iter_mut()
                .find(|entry| entry.name == current.name)
            {
                entry.name = name;
            }
        }
        if let Some(entry) = self
            .columns
            .iter_mut()
            .find(|entry| entry.name == current.name)
        {
            if let Some(new_type) = new_type {
                entry.data_type = new_type;
            }
            if let Some(nullable) = nullable {
                entry.nullable = nullable;
            }
        }
        Ok(())
    }

    fn add_index(&mut self, change: &StructureChange) -> Checked<()> {
        let name = change.required("name")?;
        if self.indexes.contains(&name) {
            return Err(format!("The table already has an index {name}."));
        }
        let columns = self.existing_columns(change.list("columns"))?;
        let unique = if change.flag("unique") { "UNIQUE " } else { "" };
        let statement = if self.dialect.driver == GridEditDriver::Sqlite {
            let parts = self.table_parts();
            let table = parts.last().cloned().unwrap_or_default();
            format!(
                "CREATE {unique}INDEX {} ON {} ({columns})",
                self.index_name(&name)?,
                self.dialect.name(&table)?
            )
        } else {
            format!(
                "CREATE {unique}INDEX {} ON {} ({columns})",
                self.dialect.name(&name)?,
                self.table.qualified_name
            )
        };
        if self.table.new_table {
            self.after.push(statement);
        } else {
            self.statements.push(statement);
        }
        self.indexes.push(name);
        Ok(())
    }

    fn add_foreign_key(&mut self, change: &StructureChange) -> Checked<()> {
        let columns = change.list("columns");
        let references = change.list("ref_columns");
        if references.len() != columns.len() {
            return Err("Enter one referenced column for each column.".into());
        }
        let columns = self.existing_columns(columns)?;
        let table = self.dialect.dotted(&change.required("ref_table")?)?;
        let references = self.dialect.names(&references)?;
        let mut clause = match change.text("name") {
            Some(name) => format!("CONSTRAINT {} ", self.dialect.name(&name)?),
            None => String::new(),
        };
        clause.push_str(&format!(
            "FOREIGN KEY ({columns}) REFERENCES {table} ({references})"
        ));
        if let Some(action) = action(change.text("on_update"))? {
            clause.push_str(&format!(" ON UPDATE {action}"));
        }
        if let Some(action) = action(change.text("on_delete"))? {
            clause.push_str(&format!(" ON DELETE {action}"));
        }
        if self.table.new_table {
            self.constraints.push(clause);
        } else {
            self.alter(format!("ADD {clause}"));
        }
        Ok(())
    }
}

pub fn plan_structure(
    table: &StructureTable,
    rules: &dyn StructureDialect,
    changes: &[StructureChange],
) -> Result<Vec<String>, StructureError> {
    let failure = |change: Option<usize>, message: String| StructureError { change, message };
    if !valid_qualified(
        &table.qualified_name,
        identifier_delimiter(rules.sql()) as u8,
    ) {
        return Err(failure(None, "The table name is not valid.".into()));
    }
    if changes.is_empty() {
        return Err(failure(None, "Add one or more changes.".into()));
    }
    let mut planner = Planner {
        table,
        rules,
        dialect: Dialect {
            driver: rules.sql(),
        },
        columns: table.columns.clone(),
        indexes: table.indexes.clone(),
        definitions: Vec::new(),
        constraints: Vec::new(),
        statements: Vec::new(),
        after: Vec::new(),
        primary_key: false,
    };
    for (index, change) in changes.iter().enumerate() {
        planner
            .apply(change)
            .map_err(|message| failure(Some(index), message))?;
    }
    if !table.new_table {
        return Ok(planner.statements);
    }
    if planner.definitions.is_empty() {
        return Err(failure(
            None,
            "Add one or more columns to the new table.".into(),
        ));
    }
    let body: Vec<String> = planner
        .definitions
        .into_iter()
        .chain(planner.constraints)
        .map(|line| format!("  {line}"))
        .collect();
    let mut statements = vec![format!(
        "CREATE TABLE {} (\n{}\n)",
        table.qualified_name,
        body.join(",\n")
    )];
    statements.extend(planner.after);
    Ok(statements)
}
