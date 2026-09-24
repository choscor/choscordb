use serde::{Deserialize, Serialize};

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum TransactionIsolation {
    ReadUncommitted,
    ReadCommitted,
    RepeatableRead,
    Serializable,
}
impl TransactionIsolation {
    pub fn sql_name(self) -> &'static str {
        match self {
            Self::ReadUncommitted => "READ UNCOMMITTED",
            Self::ReadCommitted => "READ COMMITTED",
            Self::RepeatableRead => "REPEATABLE READ",
            Self::Serializable => "SERIALIZABLE",
        }
    }
}
/// Native characteristics for the next explicit transaction.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct TransactionCharacteristics {
    pub isolation: Option<TransactionIsolation>,
    pub read_only: Option<bool>,
    pub deferrable: Option<bool>,
}
