use crate::GridEditDriver;

pub const MAX_GRID_BINARY_BYTES: usize = 64 * 1024;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum GridCellExpression {
    Default,
    CurrentDate,
    CurrentTime,
    CurrentTimestamp,
}

impl GridCellExpression {
    pub const ALL: [Self; 4] = [
        Self::Default,
        Self::CurrentDate,
        Self::CurrentTime,
        Self::CurrentTimestamp,
    ];
    pub fn sql(self) -> &'static str {
        match self {
            Self::Default => "DEFAULT",
            Self::CurrentDate => "CURRENT_DATE",
            Self::CurrentTime => "CURRENT_TIME",
            Self::CurrentTimestamp => "CURRENT_TIMESTAMP",
        }
    }
    pub fn from_sql(sql: &str) -> Option<Self> {
        Self::ALL
            .into_iter()
            .find(|expression| expression.sql() == sql)
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SetValueKind {
    Null,
    Default,
    Empty,
    Now,
}

impl SetValueKind {
    pub const ALL: [Self; 4] = [Self::Null, Self::Default, Self::Empty, Self::Now];
    pub fn code(self) -> &'static str {
        match self {
            Self::Null => "null",
            Self::Default => "default",
            Self::Empty => "empty",
            Self::Now => "now",
        }
    }
    pub fn label(self) -> &'static str {
        match self {
            Self::Null => "NULL",
            Self::Default => "DEFAULT",
            Self::Empty => "Empty",
            Self::Now => "Now()",
        }
    }
    pub fn from_code(code: &str) -> Option<Self> {
        Self::ALL.into_iter().find(|kind| kind.code() == code)
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum SetValueOutcome {
    Null,
    Text(String),
    Expression(GridCellExpression),
    Omit,
}

pub(crate) const DEFAULT_UNSUPPORTED: &str =
    "SQLite cannot set an existing row to the column default.";

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SetValueError {
    NotNullable,
    DefaultUnsupported,
    NotText,
    NotTemporal,
}

impl std::fmt::Display for SetValueError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(match self {
            Self::NotNullable => "The column does not accept NULL.",
            Self::DefaultUnsupported => DEFAULT_UNSUPPORTED,
            Self::NotText => "Only a text column accepts an empty value.",
            Self::NotTemporal => "Now() applies only to date and time columns.",
        })
    }
}

impl std::error::Error for SetValueError {}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum GridTemporalKind {
    Date,
    Time,
    Timestamp,
}

impl GridTemporalKind {
    pub fn of(database_type: &str) -> Option<Self> {
        let kind = database_type.trim().to_ascii_lowercase();
        if kind == "date" {
            Some(Self::Date)
        } else if kind.starts_with("timestamp") || kind.starts_with("datetime") {
            Some(Self::Timestamp)
        } else if kind == "time" || kind.starts_with("time ") || kind.starts_with("time(") {
            Some(Self::Time)
        } else {
            None
        }
    }
    pub fn code(self) -> &'static str {
        match self {
            Self::Date => "date",
            Self::Time => "time",
            Self::Timestamp => "timestamp",
        }
    }
    pub fn from_code(code: &str) -> Option<Self> {
        [Self::Date, Self::Time, Self::Timestamp]
            .into_iter()
            .find(|kind| kind.code() == code)
    }
    pub fn now(self) -> GridCellExpression {
        match self {
            Self::Date => GridCellExpression::CurrentDate,
            Self::Time => GridCellExpression::CurrentTime,
            Self::Timestamp => GridCellExpression::CurrentTimestamp,
        }
    }
    pub fn text(self, date: (i32, u32, u32), time: (u32, u32, u32)) -> Option<String> {
        let (year, month, day) = date;
        let (hour, minute, second) = time;
        let valid_date =
            (1..=9999).contains(&year) && (1..=12).contains(&month) && (1..=31).contains(&day);
        let valid_time = hour < 24 && minute < 60 && second < 60;
        let date = format!("{year:04}-{month:02}-{day:02}");
        let time = format!("{hour:02}:{minute:02}:{second:02}");
        match self {
            Self::Date if valid_date => Some(date),
            Self::Time if valid_time => Some(time),
            Self::Timestamp if valid_date && valid_time => Some(format!("{date} {time}")),
            _ => None,
        }
    }
}

pub fn grid_binary_column(database_type: &str) -> bool {
    let kind = database_type.trim().to_ascii_lowercase();
    kind == "bytea" || kind.contains("blob") || kind.contains("binary")
}

fn text_type(database_type: &str) -> bool {
    let kind = database_type.trim().to_ascii_lowercase();
    kind.is_empty()
        || ["char", "text", "clob", "string"]
            .iter()
            .any(|part| kind.contains(part))
}

pub fn grid_set_value(
    kind: SetValueKind,
    driver: GridEditDriver,
    database_type: &str,
    inserted: bool,
    nullable: Option<bool>,
) -> Result<SetValueOutcome, SetValueError> {
    match kind {
        SetValueKind::Null if nullable == Some(false) => Err(SetValueError::NotNullable),
        SetValueKind::Null => Ok(SetValueOutcome::Null),
        SetValueKind::Default if inserted => Ok(SetValueOutcome::Omit),
        SetValueKind::Default if driver == GridEditDriver::Sqlite => {
            Err(SetValueError::DefaultUnsupported)
        }
        SetValueKind::Default => Ok(SetValueOutcome::Expression(GridCellExpression::Default)),
        SetValueKind::Empty if text_type(database_type) => Ok(SetValueOutcome::Text(String::new())),
        SetValueKind::Empty => Err(SetValueError::NotText),
        SetValueKind::Now => GridTemporalKind::of(database_type)
            .map(|kind| SetValueOutcome::Expression(kind.now()))
            .ok_or(SetValueError::NotTemporal),
    }
}
