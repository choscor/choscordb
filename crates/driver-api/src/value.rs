use crate::Handle;
use serde::{Deserialize, Serialize};
/// Exact database representations; Decimal never passes through a float.
#[derive(Clone, PartialEq, Serialize, Deserialize)]
pub enum Value {
    Null,
    Bool(bool),
    Integer(i64),
    Real(f64),
    Decimal(String),
    Text(String),
    Date(String),
    Time(String),
    Timestamp(String),
    Uuid(String),
    Json(String),
    Binary(Vec<u8>),
    Deferred {
        handle: Handle,
        byte_length: u64,
        database_type: String,
    },
}
impl Value {
    pub fn estimated_bytes(&self) -> usize {
        std::mem::size_of::<Self>()
            + match self {
                Self::Decimal(s)
                | Self::Text(s)
                | Self::Date(s)
                | Self::Time(s)
                | Self::Timestamp(s)
                | Self::Uuid(s)
                | Self::Json(s) => s.capacity(),
                Self::Binary(b) => b.capacity(),
                Self::Deferred { database_type, .. } => database_type.capacity(),
                _ => 0,
            }
    }
}
pub type Row = Vec<Value>;
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Column {
    pub name: String,
    pub database_type: String,
    pub precision: Option<u32>,
    pub scale: Option<i32>,
    pub timezone: Option<String>,
    pub nullable: Option<bool>,
}
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct ResultPage {
    pub index: u64,
    pub rows: Vec<Row>,
    pub has_more: bool,
}
impl ResultPage {
    pub fn estimated_bytes(&self) -> usize {
        std::mem::size_of::<Self>()
            + self.rows.capacity() * std::mem::size_of::<Row>()
            + self
                .rows
                .iter()
                .map(|r| {
                    (r.capacity() - r.len()) * std::mem::size_of::<Value>()
                        + r.iter().map(Value::estimated_bytes).sum::<usize>()
                })
                .sum::<usize>()
    }
}

impl std::fmt::Debug for Value {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str("Value([REDACTED])")
    }
}

/// Maximum allocation for an individual deferred-value read.
pub const MAX_VALUE_CHUNK_BYTES: usize = 64 * 1024;
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DeferredKind {
    Text,
    Binary,
}
#[derive(Clone, PartialEq, Eq)]
pub struct ValueChunk {
    pub bytes: Vec<u8>,
    pub offset: u64,
    pub total_bytes: u64,
    pub kind: DeferredKind,
}
