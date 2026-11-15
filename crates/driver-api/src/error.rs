use serde::{Deserialize, Serialize};
pub type Result<T> = std::result::Result<T, DriverError>;
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub enum ErrorKind {
    InvalidInput,
    Connection,
    Authentication,
    Tls,
    Query,
    Cancelled,
    Timeout,
    Disconnected,
    Unsupported,
    StaleHandle,
    ResourceLimit,
    Io,
    Internal,
}
#[derive(Clone, Serialize, Deserialize)]
pub struct DriverError {
    pub kind: ErrorKind,
    pub message: String,
    pub vendor_code: Option<String>,
    #[serde(default)]
    pub failed_statement: Option<u64>,
}
impl DriverError {
    pub fn new(kind: ErrorKind, message: impl Into<String>) -> Self {
        Self {
            kind,
            message: message.into(),
            vendor_code: None,
            failed_statement: None,
        }
    }
    pub fn with_code(mut self, code: impl Into<String>) -> Self {
        self.vendor_code = Some(code.into());
        self
    }
    pub fn at_statement(mut self, index: usize) -> Self {
        self.failed_statement = u64::try_from(index).ok();
        self
    }
}
impl std::fmt::Display for DriverError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "Database error ({:?})", self.kind)
    }
}
impl std::fmt::Debug for DriverError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        std::fmt::Display::fmt(self, f)
    }
}
impl std::error::Error for DriverError {}
