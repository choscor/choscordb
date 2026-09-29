//! Privacy-bounded local diagnostics. This crate never accepts SQL, row values, paths,
//! driver messages, or arbitrary payload fields as diagnostic record data.
use std::collections::BTreeMap;

mod date;
mod memory;
mod service;
mod zip;
pub use service::Service;

#[derive(Clone, Copy, Debug)]
pub enum EngineBoundaryEvent {
    QueryQueued(u64),
    QueryFinished { id: u64, duration_ms: u64 },
    QueryFailed { id: u64, cancelled: bool },
    StoredPage,
    ExportFinished,
    ExportFailed,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub enum Event {
    Startup,
    Shutdown,
    ConnectionSucceeded,
    ConnectionFailed,
    QuerySucceeded,
    QueryFailed,
    ResultPage,
    ExportSucceeded,
    ExportFailed,
    Cancelled,
    UiHangStart,
    UiHangEnd,
    UncleanExit,
    #[default]
    Error,
    MemorySample,
}
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub enum Driver {
    #[default]
    Unknown,
    SQLite,
    PostgreSQL,
    MySQL,
}
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub enum ErrorClass {
    #[default]
    Unknown,
    Connection,
    Authentication,
    Query,
    Io,
    Internal,
}
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub enum DurationBucket {
    #[default]
    Unknown,
    Under100Ms,
    Under1s,
    Under10s,
    Over10s,
}
#[derive(Clone, Copy, Debug, Default)]
pub struct Record {
    pub event: Event,
    pub driver: Driver,
    pub error_class: ErrorClass,
    pub duration_bucket: DurationBucket,
    pub open_tabs: i32,
    pub duration_ms: i32,
    pub force_memory: bool,
}
#[derive(Clone, Debug, Default)]
pub struct Summary {
    pub estimated_bytes: u64,
    pub category_counts: BTreeMap<String, u64>,
    pub duration_bucket_counts: BTreeMap<String, u64>,
    pub from_utc: String,
    pub to_utc: String,
    pub unavailable_categories: Vec<String>,
    pub dropped_records: u64,
    pub has_history: bool,
}
#[derive(Clone, Debug, Default)]
pub struct ExportResult {
    pub success: bool,
    pub cancelled: bool,
    pub error: String,
}
