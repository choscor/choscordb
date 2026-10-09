use crate::{
    Driver, DurationBucket, EngineBoundaryEvent, ErrorClass, Event, ExportResult, Record, Summary,
    date, memory, zip,
};
use fs2::FileExt;
use serde_json::{Value, json};
use std::{
    collections::{HashMap, VecDeque},
    fs::{self, File, OpenOptions},
    io::{Read, Write},
    path::{Path, PathBuf},
    sync::{
        Arc, Condvar, Mutex,
        atomic::{AtomicBool, AtomicI32, AtomicU64, Ordering},
    },
    thread::{self, JoinHandle},
    time::{Duration, Instant, SystemTime},
};
use tempfile::NamedTempFile;
use uuid::Uuid;

const SCHEMA: u64 = 1;
const RETENTION_DAYS: i64 = 14;
const EXPORT_DAYS: i64 = 7;
const MAX_DAY_BYTES: usize = 256 * 1024;
const MAX_EXPORT_BYTES: usize = 2 * 1024 * 1024;
const MAX_QUEUE: usize = 1024;

struct Pending {
    record: Record,
    utc: String,
}
struct Queue {
    pending: VecDeque<Pending>,
    accepting_prestart: bool,
    starting: bool,
    running: bool,
    stopping: bool,
    writing: bool,
    warning: String,
    marker: PathBuf,
}
impl Default for Queue {
    fn default() -> Self {
        Self {
            pending: VecDeque::new(),
            accepting_prestart: true,
            starting: false,
            running: false,
            stopping: false,
            writing: false,
            warning: String::new(),
            marker: PathBuf::new(),
        }
    }
}
struct State {
    folder: PathBuf,
    version: String,
    build: String,
    queue: Mutex<Queue>,
    changed: Condvar,
    idle: Condvar,
    io: Mutex<()>,
    last_memory: Mutex<Option<Instant>>,
    open_tabs: AtomicI32,
    dropped: AtomicU64,
    synced_dropped: AtomicU64,
    worker: Mutex<Option<JoinHandle<()>>>,
    query_starts: Mutex<HashMap<u64, Instant>>,
}

/// Thread-safe, bounded diagnostic capture. `record` queues typed data without disk I/O;
/// preview, clear, and export are blocking operations for a caller-owned background task.
pub struct Service {
    state: Arc<State>,
}
impl Service {
    pub fn new(data_dir: impl AsRef<Path>, app_version: &str, build_version: &str) -> Self {
        Self {
            state: Arc::new(State {
                folder: data_dir.as_ref().join("diagnostics"),
                version: safe_version(app_version),
                build: safe_version(build_version),
                queue: Mutex::new(Queue::default()),
                changed: Condvar::new(),
                idle: Condvar::new(),
                io: Mutex::new(()),
                last_memory: Mutex::new(None),
                open_tabs: AtomicI32::new(0),
                dropped: AtomicU64::new(0),
                synced_dropped: AtomicU64::new(0),
                worker: Mutex::new(None),
                query_starts: Mutex::new(HashMap::new()),
            }),
        }
    }
    pub fn folder_path(&self) -> &Path {
        &self.state.folder
    }
    pub fn warning(&self) -> String {
        self.state
            .queue
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner)
            .warning
            .clone()
    }

    pub fn start(&self) -> bool {
        let mut queue = self
            .state
            .queue
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        if queue.running {
            return true;
        }
        if queue.starting {
            return false;
        }
        queue.starting = true;
        queue.stopping = false;
        drop(queue);
        let fail = |message: &str| {
            let mut queue = self
                .state
                .queue
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner);
            queue.starting = false;
            queue.accepting_prestart = false;
            queue.pending.clear();
            queue.warning = message.into();
            self.state.idle.notify_all();
            false
        };
        if self.state.folder.is_symlink()
            || fs::create_dir_all(&self.state.folder).is_err()
            || self.state.folder.is_symlink()
        {
            return fail("Diagnostics storage unavailable.");
        }
        private_permissions(&self.state.folder, true);
        let stale;
        let prior;
        let marker;
        {
            let _io = self
                .state
                .io
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner);
            let Some(_lock) = folder_lock(&self.state.folder, Duration::from_secs(3)) else {
                return fail("Diagnostics storage unavailable.");
            };
            stale = remove_stale_markers(&self.state.folder);
            marker = self.state.folder.join(format!(
                "run-{}-{}.marker",
                std::process::id(),
                Uuid::new_v4()
            ));
            if atomic_write(&marker, b"running\n").is_err() {
                return fail("Diagnostics run marker unavailable.");
            }
            prior = read_dropped(&self.state.folder);
        }
        self.state.dropped.store(prior, Ordering::Relaxed);
        self.state.synced_dropped.store(prior, Ordering::Relaxed);
        let mut queue = self
            .state
            .queue
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        queue.marker = marker;
        queue.accepting_prestart = false;
        queue.running = true;
        queue.stopping = false;
        let state = Arc::clone(&self.state);
        match thread::Builder::new()
            .name("diagnostics-writer".into())
            .spawn(move || {
                if std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
                    writer(Arc::clone(&state))
                }))
                .is_err()
                {
                    let mut queue = state
                        .queue
                        .lock()
                        .unwrap_or_else(std::sync::PoisonError::into_inner);
                    queue.pending.clear();
                    queue.writing = false;
                    queue.running = false;
                    queue.warning = "Some diagnostics could not be saved.".into();
                    state.idle.notify_all();
                }
            }) {
            Ok(worker) => {
                *self
                    .state
                    .worker
                    .lock()
                    .unwrap_or_else(std::sync::PoisonError::into_inner) = Some(worker)
            }
            Err(_) => {
                queue.running = false;
                let marker = std::mem::take(&mut queue.marker);
                drop(queue);
                let _ = fs::remove_file(marker);
                return fail("Diagnostics writer unavailable.");
            }
        }
        let reported = stale.min(16);
        self.state
            .dropped
            .fetch_add(stale.saturating_sub(reported), Ordering::Relaxed);
        for _ in 0..reported {
            queue.pending.push_back(Pending {
                record: Record {
                    event: Event::UncleanExit,
                    ..Record::default()
                },
                utc: date::now_utc(),
            });
        }
        queue.pending.push_back(Pending {
            record: Record {
                event: Event::Startup,
                ..Record::default()
            },
            utc: date::now_utc(),
        });
        queue.starting = false;
        self.state.idle.notify_all();
        self.state.changed.notify_one();
        true
    }

    pub fn stop(&self) {
        {
            let mut queue = self
                .state
                .queue
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner);
            while queue.starting {
                queue = self
                    .state
                    .idle
                    .wait(queue)
                    .unwrap_or_else(std::sync::PoisonError::into_inner);
            }
            if !queue.running {
                queue.accepting_prestart = false;
                queue.pending.clear();
                if !queue.marker.as_os_str().is_empty() {
                    let _ = fs::remove_file(std::mem::take(&mut queue.marker));
                }
                return;
            }
            queue.pending.push_back(Pending {
                record: Record {
                    event: Event::Shutdown,
                    ..Record::default()
                },
                utc: date::now_utc(),
            });
            queue.stopping = true;
            self.state.changed.notify_one();
        }
        if let Some(worker) = self
            .state
            .worker
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner)
            .take()
        {
            let _ = worker.join();
        }
        let marker = self
            .state
            .queue
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner)
            .marker
            .clone();
        {
            let _io = self
                .state
                .io
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner);
            if let Some(_lock) = folder_lock(&self.state.folder, Duration::from_secs(3)) {
                if !sync_dropped(&self.state) {
                    self.warn("Some diagnostics could not be saved.");
                }
            } else {
                self.warn("Some diagnostics could not be saved.");
            }
        }
        let _ = fs::remove_file(marker);
        let mut queue = self
            .state
            .queue
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        queue.marker = PathBuf::new();
        queue.running = false;
    }

    pub fn record(&self, mut record: Record) {
        if record.event == Event::UiHangEnd && record.duration_bucket == DurationBucket::Unknown {
            record.duration_bucket = duration_bucket(record.duration_ms.max(0) as u64);
        }
        let mut queue = self
            .state
            .queue
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        if (!queue.running && !queue.starting && !queue.accepting_prestart) || queue.stopping {
            return;
        }
        if queue.pending.len() >= MAX_QUEUE {
            if high_value(record.event) {
                if let Some(index) = queue
                    .pending
                    .iter()
                    .position(|item| !high_value(item.record.event))
                {
                    queue.pending.remove(index);
                } else {
                    queue.pending.pop_front();
                }
            } else {
                self.state.dropped.fetch_add(1, Ordering::Relaxed);
                return;
            }
            self.state.dropped.fetch_add(1, Ordering::Relaxed);
        }
        queue.pending.push_back(Pending {
            record,
            utc: date::now_utc(),
        });
        self.state.changed.notify_one();
    }
    pub fn sample_memory(&self, open_tabs: i32, force: bool) {
        self.set_open_tabs(open_tabs);
        self.record(Record {
            event: Event::MemorySample,
            open_tabs,
            force_memory: force,
            ..Record::default()
        });
    }
    pub fn set_open_tabs(&self, open_tabs: i32) {
        self.state
            .open_tabs
            .store(open_tabs.clamp(0, 1000), Ordering::Relaxed);
    }
    pub fn open_tabs(&self) -> i32 {
        self.state.open_tabs.load(Ordering::Relaxed)
    }
    pub fn observe_connection(&self, driver: Driver, succeeded: bool, open_tabs: i32) {
        self.record(Record {
            event: if succeeded {
                Event::ConnectionSucceeded
            } else {
                Event::ConnectionFailed
            },
            driver,
            error_class: if succeeded {
                ErrorClass::Unknown
            } else {
                ErrorClass::Connection
            },
            open_tabs,
            ..Record::default()
        });
        if succeeded {
            self.sample_memory(open_tabs, true);
        }
    }
    pub fn observe_command_failure(&self) {
        self.record(Record {
            event: Event::Error,
            error_class: ErrorClass::Internal,
            ..Record::default()
        });
    }
    pub fn observe_engine_event(&self, event: EngineBoundaryEvent, open_tabs: i32) {
        match event {
            EngineBoundaryEvent::QueryQueued(id) => {
                let mut starts = self
                    .state
                    .query_starts
                    .lock()
                    .unwrap_or_else(std::sync::PoisonError::into_inner);
                if !starts.contains_key(&id) {
                    if starts.len() >= 1024 {
                        starts.clear();
                    }
                    starts.insert(id, Instant::now());
                }
            }
            EngineBoundaryEvent::QueryFinished { id, duration_ms } => {
                self.state
                    .query_starts
                    .lock()
                    .unwrap_or_else(std::sync::PoisonError::into_inner)
                    .remove(&id);
                self.record(Record {
                    event: Event::QuerySucceeded,
                    duration_bucket: duration_bucket(duration_ms),
                    open_tabs,
                    ..Record::default()
                });
                self.sample_memory(open_tabs, true);
            }
            EngineBoundaryEvent::QueryFailed { id, cancelled } => {
                let duration_bucket = self
                    .state
                    .query_starts
                    .lock()
                    .unwrap_or_else(std::sync::PoisonError::into_inner)
                    .remove(&id)
                    .map(|start| duration_bucket(start.elapsed().as_millis() as u64))
                    .unwrap_or_default();
                self.record(Record {
                    event: if cancelled {
                        Event::Cancelled
                    } else {
                        Event::QueryFailed
                    },
                    error_class: ErrorClass::Query,
                    duration_bucket,
                    open_tabs,
                    ..Record::default()
                });
                if !cancelled {
                    self.sample_memory(open_tabs, true);
                }
            }
            EngineBoundaryEvent::StoredPage => self.record(Record {
                event: Event::ResultPage,
                open_tabs,
                ..Record::default()
            }),
            EngineBoundaryEvent::ExportFinished => self.record(Record {
                event: Event::ExportSucceeded,
                open_tabs,
                ..Record::default()
            }),
            EngineBoundaryEvent::ExportFailed => self.record(Record {
                event: Event::ExportFailed,
                error_class: ErrorClass::Io,
                open_tabs,
                ..Record::default()
            }),
        }
    }
    pub fn flush(&self) {
        let mut queue = self
            .state
            .queue
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        while queue.running && (!queue.pending.is_empty() || queue.writing) {
            queue = self
                .state
                .idle
                .wait(queue)
                .unwrap_or_else(std::sync::PoisonError::into_inner);
        }
    }
    pub fn preview(&self) -> Summary {
        self.flush();
        let _io = self
            .state
            .io
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        let Some(_lock) = folder_lock(&self.state.folder, Duration::from_millis(500)) else {
            return Summary {
                unavailable_categories: vec!["diagnostic_snapshot".into()],
                ..Summary::default()
            };
        };
        snapshot(&self.state.folder, dropped_total(&self.state)).summary
    }
    pub fn export_zip(
        &self,
        destination: impl AsRef<Path>,
        cancel: Option<&AtomicBool>,
    ) -> ExportResult {
        if is_cancelled(cancel) {
            return self.cancelled();
        }
        self.flush();
        let mut data = {
            let _io = self
                .state
                .io
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner);
            let Some(_lock) = folder_lock(&self.state.folder, Duration::from_secs(3)) else {
                return self.failed("Could not read a consistent diagnostics snapshot.");
            };
            snapshot(&self.state.folder, dropped_total(&self.state))
        };
        if is_cancelled(cancel) {
            return self.cancelled();
        }
        let os_version = os_version();
        if self.state.build == "unknown" {
            data.summary
                .unavailable_categories
                .push("build_version".into());
        }
        if os_version == "unknown" {
            data.summary
                .unavailable_categories
                .push("os_version".into());
        }
        let now = date::now_utc();
        let manifest = json!({
            "schema": SCHEMA,
            "app_version": self.state.version,
            "build_version": self.state.build,
            "os_family": os_family(),
            "os_version": os_version,
            "architecture": safe_version(std::env::consts::ARCH),
            "coverage_start_utc": date::day_name(date::today() + 1 - EXPORT_DAYS),
            "coverage_end_utc": now,
            "first_record_utc": data.summary.from_utc,
            "last_record_utc": data.summary.to_utc,
            "has_history": data.summary.has_history,
            "category_counts": data.summary.category_counts,
            "duration_bucket_counts": data.summary.duration_bucket_counts,
            "unavailable_categories": data.summary.unavailable_categories,
            "omitted_categories": ["sql_text", "result_rows", "native_crash_report", "raw_stack_dump", "heap_snapshot"],
            "dropped_records": data.summary.dropped_records,
            "dropped_records_scope": "since_last_clear_or_stats_reset",
            "invalid_records_omitted": data.invalid_records,
            "export_cap_omitted": data.omitted_by_cap,
            "retention_days": RETENTION_DAYS,
            "export_days": EXPORT_DAYS,
            "day_byte_cap": MAX_DAY_BYTES,
            "archive_byte_cap": MAX_EXPORT_BYTES,
            "memory_sample_interval_seconds": crate::MEMORY_SAMPLE_INTERVAL_SECONDS,
            "event_memory_min_interval_seconds": 5,
            "memory_interpretation": "A memory trend cannot prove an allocation leak.",
            "unclean_exit_interpretation": "An unclean exit does not identify its cause."
        });
        let mut manifest_bytes = serde_json::to_vec(&manifest).unwrap_or_default();
        manifest_bytes.push(b'\n');
        let archive = zip::archive(&[
            ("manifest.json", &manifest_bytes),
            ("events.jsonl", &data.events),
        ]);
        if archive.len() > MAX_EXPORT_BYTES {
            return self.failed("Diagnostics report exceeds its size limit.");
        }
        let destination = destination.as_ref();
        let Some(parent) = destination.parent() else {
            return self.failed("Could not open the selected ZIP destination.");
        };
        let Ok(mut temp) = NamedTempFile::new_in(parent) else {
            return self.failed("Could not open the selected ZIP destination.");
        };
        private_permissions(temp.path(), false);
        for chunk in archive.chunks(64 * 1024) {
            if is_cancelled(cancel) {
                return self.cancelled();
            }
            if temp.write_all(chunk).is_err() {
                return self.failed("Could not write the diagnostics ZIP.");
            }
        }
        if is_cancelled(cancel) {
            return self.cancelled();
        }
        if temp.flush().is_err() || temp.persist(destination).is_err() {
            return self.failed("Could not save the diagnostics ZIP.");
        }
        self.record(Record {
            event: Event::ExportSucceeded,
            ..Record::default()
        });
        ExportResult {
            success: true,
            ..ExportResult::default()
        }
    }
    pub fn clear(&self) -> Result<(), String> {
        self.flush();
        let _io = self
            .state
            .io
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        let Some(_lock) = folder_lock(&self.state.folder, Duration::from_secs(3)) else {
            return Err("Could not lock local diagnostics for clearing.".into());
        };
        if self.state.folder.is_symlink() {
            return Err("Could not clear all local diagnostics.".into());
        }
        remove_stale_markers(&self.state.folder);
        for entry in fs::read_dir(&self.state.folder)
            .map_err(|_| "Could not clear all local diagnostics.")?
        {
            let entry = entry.map_err(|_| "Could not clear all local diagnostics.")?;
            if day_file(&entry.file_name().to_string_lossy()).is_some() {
                fs::remove_file(entry.path())
                    .map_err(|_| "Could not clear all local diagnostics.")?;
            }
        }
        let stats = self.state.folder.join("stats.json");
        if stats.exists() {
            fs::remove_file(stats).map_err(|_| "Could not clear all local diagnostics.")?;
        }
        self.state.dropped.store(0, Ordering::Relaxed);
        self.state.synced_dropped.store(0, Ordering::Relaxed);
        *self
            .state
            .last_memory
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner) = None;
        Ok(())
    }
    fn warn(&self, message: &str) {
        self.state
            .queue
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner)
            .warning = message.into();
    }
    fn cancelled(&self) -> ExportResult {
        self.record(Record {
            event: Event::Cancelled,
            ..Record::default()
        });
        ExportResult {
            cancelled: true,
            ..ExportResult::default()
        }
    }
    fn failed(&self, message: &str) -> ExportResult {
        self.record(Record {
            event: Event::ExportFailed,
            error_class: ErrorClass::Io,
            ..Record::default()
        });
        ExportResult {
            error: message.into(),
            ..ExportResult::default()
        }
    }
}
impl Drop for Service {
    fn drop(&mut self) {
        self.stop();
    }
}

fn is_cancelled(cancel: Option<&AtomicBool>) -> bool {
    cancel.is_some_and(|flag| flag.load(Ordering::Relaxed))
}
fn safe_version(value: &str) -> String {
    if (1..=40).contains(&value.len())
        && value
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || b"._+-".contains(&byte))
    {
        value.into()
    } else {
        "unknown".into()
    }
}
fn os_family() -> &'static str {
    match std::env::consts::OS {
        "macos" => "macos",
        "windows" => "windows",
        "linux" => "linux",
        _ => "unknown",
    }
}
fn os_version() -> String {
    // A trusted absolute version tool is available on these platforms. Windows has no
    // equivalent fixed-path command across supported installations, so report it unavailable
    // instead of resolving an executable from PATH during diagnostics export.
    #[cfg(target_os = "linux")]
    let mut command = {
        let mut command = std::process::Command::new("/usr/bin/uname");
        command.arg("-r");
        command
    };
    #[cfg(target_os = "macos")]
    let mut command = {
        let mut command = std::process::Command::new("/usr/bin/sw_vers");
        command.arg("-productVersion");
        command
    };
    #[cfg(any(target_os = "linux", target_os = "macos"))]
    {
        command_output(&mut command, Duration::from_secs(2))
            .and_then(|output| String::from_utf8(output.stdout).ok())
            .map(|version| safe_version(version.trim()))
            .unwrap_or_else(|| "unknown".into())
    }
    #[cfg(not(any(target_os = "linux", target_os = "macos")))]
    {
        "unknown".into()
    }
}
fn command_output(
    command: &mut std::process::Command,
    timeout: Duration,
) -> Option<std::process::Output> {
    use std::process::Stdio;
    let mut child = command
        .stdout(Stdio::piped())
        .stderr(Stdio::null())
        .spawn()
        .ok()?;
    let deadline = Instant::now() + timeout;
    loop {
        match child.try_wait() {
            Ok(Some(_)) => return child.wait_with_output().ok(),
            Ok(None) if Instant::now() < deadline => thread::sleep(Duration::from_millis(10)),
            _ => {
                let _ = child.kill();
                let _ = child.wait();
                return None;
            }
        }
    }
}
fn high_value(event: Event) -> bool {
    matches!(
        event,
        Event::ConnectionFailed
            | Event::QueryFailed
            | Event::UiHangStart
            | Event::UiHangEnd
            | Event::UncleanExit
            | Event::Error
            | Event::ExportFailed
    )
}
fn duration_bucket(duration_ms: u64) -> DurationBucket {
    if duration_ms < 100 {
        DurationBucket::Under100Ms
    } else if duration_ms < 1_000 {
        DurationBucket::Under1s
    } else if duration_ms < 10_000 {
        DurationBucket::Under10s
    } else {
        DurationBucket::Over10s
    }
}
fn event_name(event: Event) -> &'static str {
    match event {
        Event::Startup => "startup",
        Event::Shutdown => "shutdown",
        Event::ConnectionSucceeded => "connection_succeeded",
        Event::ConnectionFailed => "connection_failed",
        Event::QuerySucceeded => "query_succeeded",
        Event::QueryFailed => "query_failed",
        Event::ResultPage => "result_page",
        Event::ExportSucceeded => "export_succeeded",
        Event::ExportFailed => "export_failed",
        Event::Cancelled => "cancelled",
        Event::UiHangStart => "ui_hang_start",
        Event::UiHangEnd => "ui_hang_end",
        Event::UncleanExit => "unclean_exit",
        Event::Error => "error",
        Event::MemorySample => "memory_sample",
    }
}
fn driver_name(driver: Driver) -> &'static str {
    match driver {
        Driver::Unknown => "unknown",
        Driver::SQLite => "sqlite",
        Driver::PostgreSQL => "postgresql",
        Driver::MySQL => "mysql",
    }
}
fn error_name(error: ErrorClass) -> &'static str {
    match error {
        ErrorClass::Unknown => "unknown",
        ErrorClass::Connection => "connection",
        ErrorClass::Authentication => "authentication",
        ErrorClass::Query => "query",
        ErrorClass::Io => "io",
        ErrorClass::Internal => "internal",
    }
}
fn bucket_name(bucket: DurationBucket) -> &'static str {
    match bucket {
        DurationBucket::Unknown => "unknown",
        DurationBucket::Under100Ms => "under_100_ms",
        DurationBucket::Under1s => "under_1_s",
        DurationBucket::Under10s => "under_10_s",
        DurationBucket::Over10s => "over_10_s",
    }
}

fn serialized(pending: &Pending) -> Value {
    let record = pending.record;
    let mut value = json!({
        "schema": SCHEMA, "utc": pending.utc,
        "event": event_name(record.event), "driver": driver_name(record.driver),
        "error_class": error_name(record.error_class), "duration_bucket": bucket_name(record.duration_bucket),
        "open_tabs": record.open_tabs.clamp(0, 1000)
    });
    if record.event == Event::UiHangEnd {
        value["duration_ms"] = json!(record.duration_ms.clamp(0, 600_000));
    }
    if record.event == Event::MemorySample {
        let (resident, footprint, peak) = memory_bytes();
        if let Some(bytes) = resident {
            value["resident_bytes"] = json!(bytes);
        }
        if let Some(bytes) = footprint {
            value["footprint_bytes"] = json!(bytes);
        }
        if let Some(bytes) = peak {
            value["peak_bytes"] = json!(bytes);
        }
    }
    value
}
fn json_line(value: &Value) -> Vec<u8> {
    let mut output = serde_json::to_vec(value).unwrap_or_default();
    output.push(b'\n');
    output
}

fn memory_bytes() -> (Option<u64>, Option<u64>, Option<u64>) {
    match proc_memstat::try_snapshot() {
        Ok(sample) => (Some(sample.rss), memory::footprint_bytes(), sample.peak_rss),
        Err(_) => (None, memory::footprint_bytes(), None),
    }
}

fn writer(state: Arc<State>) {
    loop {
        let mut pending = {
            let mut queue = state
                .queue
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner);
            while queue.pending.is_empty() && !queue.stopping {
                queue = state
                    .changed
                    .wait(queue)
                    .unwrap_or_else(std::sync::PoisonError::into_inner);
            }
            if queue.pending.is_empty() && queue.stopping {
                break;
            }
            queue.writing = true;
            queue.pending.pop_front().unwrap()
        };
        let mut ok = true;
        let mut stats_ok = true;
        {
            let _io = state
                .io
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner);
            if let Some(_lock) = folder_lock(&state.folder, Duration::from_secs(3)) {
                let mut save = true;
                if pending.record.event == Event::MemorySample {
                    pending.utc = date::now_utc();
                    let mut last = state
                        .last_memory
                        .lock()
                        .unwrap_or_else(std::sync::PoisonError::into_inner);
                    let interval = if pending.record.force_memory { 5 } else { 60 };
                    if last.is_some_and(|last| last.elapsed() < Duration::from_secs(interval)) {
                        save = false;
                    } else {
                        *last = Some(Instant::now());
                    }
                }
                if save {
                    let line = json_line(&serialized(&pending));
                    ok = append_bounded(
                        &state.folder.join(format!("{}.jsonl", &pending.utc[..10])),
                        &line,
                        high_value(pending.record.event),
                        &state.dropped,
                    );
                    prune(&state.folder);
                }
                if !ok {
                    state.dropped.fetch_add(1, Ordering::Relaxed);
                }
                stats_ok = sync_dropped(&state);
                touch_marker(
                    &state
                        .queue
                        .lock()
                        .unwrap_or_else(std::sync::PoisonError::into_inner)
                        .marker,
                );
            } else {
                ok = false;
                let mut backlog = {
                    let mut queue = state
                        .queue
                        .lock()
                        .unwrap_or_else(std::sync::PoisonError::into_inner);
                    std::mem::take(&mut queue.pending)
                };
                backlog.push_front(pending);
                let mut priority = Vec::new();
                for item in backlog {
                    if high_value(item.record.event) && priority.len() < 8 {
                        priority.push(item);
                    } else {
                        state.dropped.fetch_add(1, Ordering::Relaxed);
                    }
                }
                if let Some(_lock) = folder_lock(&state.folder, Duration::from_millis(500)) {
                    for item in priority {
                        let line = json_line(&serialized(&item));
                        if !append_bounded(
                            &state.folder.join(format!("{}.jsonl", &item.utc[..10])),
                            &line,
                            true,
                            &state.dropped,
                        ) {
                            state.dropped.fetch_add(1, Ordering::Relaxed);
                        }
                    }
                    prune(&state.folder);
                    stats_ok = sync_dropped(&state);
                    touch_marker(
                        &state
                            .queue
                            .lock()
                            .unwrap_or_else(std::sync::PoisonError::into_inner)
                            .marker,
                    );
                } else {
                    state
                        .dropped
                        .fetch_add(priority.len() as u64, Ordering::Relaxed);
                }
            }
        }
        let mut queue = state
            .queue
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        if !ok || !stats_ok {
            queue.warning = "Some diagnostics could not be saved.".into();
        }
        queue.writing = false;
        state.idle.notify_all();
    }
    state.idle.notify_all();
}

fn private_permissions(path: &Path, directory: bool) {
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let _ = fs::set_permissions(
            path,
            fs::Permissions::from_mode(if directory { 0o700 } else { 0o600 }),
        );
    }
    #[cfg(not(unix))]
    {
        let _ = (path, directory);
    }
}
struct FolderLock(File);
impl Drop for FolderLock {
    fn drop(&mut self) {
        let _ = FileExt::unlock(&self.0);
    }
}
fn folder_lock(folder: &Path, timeout: Duration) -> Option<FolderLock> {
    let path = folder.join(".io.lock");
    if path.is_symlink() {
        return None;
    }
    let mut options = OpenOptions::new();
    options.read(true).write(true).create(true).truncate(false);
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;
        options.mode(0o600).custom_flags(libc::O_NOFOLLOW);
    }
    #[cfg(windows)]
    {
        use std::os::windows::fs::OpenOptionsExt;
        // Open the reparse point itself so the post-open attribute check cannot
        // accidentally inspect a target selected between the two checks.
        options.custom_flags(0x0020_0000); // FILE_FLAG_OPEN_REPARSE_POINT
    }
    let file = options.open(&path).ok()?;
    if !file.metadata().ok()?.is_file() {
        return None;
    }
    #[cfg(windows)]
    {
        use std::os::windows::fs::MetadataExt;
        if file.metadata().ok()?.file_attributes() & 0x0000_0400 != 0 {
            // FILE_ATTRIBUTE_REPARSE_POINT
            return None;
        }
    }
    private_permissions(&path, false);
    let deadline = Instant::now() + timeout;
    loop {
        match FileExt::try_lock_exclusive(&file) {
            Ok(()) => {
                if file.set_len(0).is_err() {
                    let _ = FileExt::unlock(&file);
                    return None;
                }
                return Some(FolderLock(file));
            }
            Err(error)
                if (error.kind() == std::io::ErrorKind::WouldBlock
                    || error.raw_os_error() == fs2::lock_contended_error().raw_os_error())
                    && Instant::now() < deadline =>
            {
                thread::sleep(Duration::from_millis(10))
            }
            Err(_) => return None,
        }
    }
}
fn atomic_write(path: &Path, bytes: &[u8]) -> std::io::Result<()> {
    let parent = path.parent().ok_or(std::io::ErrorKind::InvalidInput)?;
    let mut file = NamedTempFile::new_in(parent)?;
    private_permissions(file.path(), false);
    file.write_all(bytes)?;
    file.flush()?;
    file.persist(path).map_err(|error| error.error)?;
    Ok(())
}
fn day_file(name: &str) -> Option<i64> {
    name.strip_suffix(".jsonl").and_then(date::parse_day)
}
fn prune(folder: &Path) {
    let today = date::today();
    let Ok(entries) = fs::read_dir(folder) else {
        return;
    };
    for entry in entries.flatten() {
        let name = entry.file_name().to_string_lossy().into_owned();
        let Some(day) = day_file(&name) else {
            continue;
        };
        if day < today + 1 - RETENTION_DAYS || day > today || entry.path().is_symlink() {
            let _ = fs::remove_file(entry.path());
        }
    }
}
fn read_dropped(folder: &Path) -> u64 {
    let path = folder.join("stats.json");
    let Ok(metadata) = fs::symlink_metadata(&path) else {
        return 0;
    };
    if metadata.file_type().is_symlink() || !metadata.is_file() || metadata.len() > 1024 {
        return 0;
    }
    let Ok(bytes) = fs::read(path) else {
        return 0;
    };
    let Ok(value) = serde_json::from_slice::<Value>(&bytes) else {
        return 0;
    };
    if value["schema"] != SCHEMA {
        return 0;
    }
    let Some(dropped) = value["dropped_records"].as_u64() else {
        return 0;
    };
    if dropped > 1_000_000_000_000_000 {
        return 0;
    }
    dropped
}
fn sync_dropped(state: &State) -> bool {
    let current = state.dropped.load(Ordering::Relaxed);
    let synced = state.synced_dropped.load(Ordering::Relaxed);
    let delta = if current >= synced {
        current - synced
    } else {
        current
    };
    if delta == 0 {
        return true;
    }
    let previous = read_dropped(&state.folder);
    let value = json!({"schema": SCHEMA, "dropped_records": previous.saturating_add(delta)});
    let result = atomic_write(&state.folder.join("stats.json"), &json_line(&value)).is_ok();
    if result {
        state.synced_dropped.store(current, Ordering::Relaxed);
    }
    result
}
fn dropped_total(state: &State) -> u64 {
    let current = state.dropped.load(Ordering::Relaxed);
    let synced = state.synced_dropped.load(Ordering::Relaxed);
    read_dropped(&state.folder).saturating_add(if current >= synced {
        current - synced
    } else {
        current
    })
}
fn touch_marker(path: &Path) {
    if !path.as_os_str().is_empty() {
        let _ = OpenOptions::new()
            .write(true)
            .open(path)
            .and_then(|file| file.set_times(fs::FileTimes::new().set_modified(SystemTime::now())));
    }
}
fn remove_stale_markers(folder: &Path) -> u64 {
    let mut stale = 0;
    let Ok(entries) = fs::read_dir(folder) else {
        return 0;
    };
    for entry in entries.flatten() {
        let name = entry.file_name().to_string_lossy().into_owned();
        let Some(rest) = name
            .strip_prefix("run-")
            .and_then(|name| name.strip_suffix(".marker"))
        else {
            continue;
        };
        let Some((pid, uuid)) = rest.split_once('-') else {
            continue;
        };
        let Ok(pid) = pid.parse::<u32>() else {
            continue;
        };
        if uuid.len() != 36 || Uuid::parse_str(uuid).is_err() {
            continue;
        }
        let Ok(metadata) = fs::symlink_metadata(entry.path()) else {
            continue;
        };
        if !metadata.is_file() || metadata.file_type().is_symlink() {
            continue;
        }
        if metadata
            .modified()
            .ok()
            .and_then(|time| SystemTime::now().duration_since(time).ok())
            .is_some_and(|age| age > Duration::from_secs(RETENTION_DAYS as u64 * 86_400))
        {
            let _ = fs::remove_file(entry.path());
        } else if !process_alive(pid) && fs::remove_file(entry.path()).is_ok() {
            stale += 1;
        }
    }
    stale
}
#[cfg(target_os = "linux")]
fn process_alive(pid: u32) -> bool {
    Path::new("/proc").join(pid.to_string()).exists()
}
#[cfg(target_os = "macos")]
fn process_alive(pid: u32) -> bool {
    let mut command = std::process::Command::new("/bin/kill");
    command.args(["-0", &pid.to_string()]);
    command_output(&mut command, Duration::from_secs(1))
        .is_none_or(|output| output.status.success())
}
#[cfg(target_os = "windows")]
fn process_alive(pid: u32) -> bool {
    let mut command = std::process::Command::new("tasklist");
    command.args(["/FI", &format!("PID eq {pid}"), "/FO", "CSV", "/NH"]);
    command_output(&mut command, Duration::from_secs(2)).is_none_or(|output| {
        String::from_utf8_lossy(&output.stdout).contains(&format!(",\"{pid}\""))
    })
}
#[cfg(not(any(target_os = "linux", target_os = "macos", target_os = "windows")))]
fn process_alive(_pid: u32) -> bool {
    true
}

fn valid_record(value: &Value, day: i64) -> bool {
    let Some(object) = value.as_object() else {
        return false;
    };
    const KEYS: &[&str] = &[
        "schema",
        "utc",
        "event",
        "driver",
        "error_class",
        "duration_bucket",
        "open_tabs",
        "resident_bytes",
        "footprint_bytes",
        "peak_bytes",
        "duration_ms",
    ];
    if object.keys().any(|key| !KEYS.contains(&key.as_str())) || value["schema"] != SCHEMA {
        return false;
    }
    if value["utc"].as_str().and_then(date::timestamp_day) != Some(day) {
        return false;
    }
    if !EVENT_NAMES.contains(&value["event"].as_str().unwrap_or(""))
        || !DRIVER_NAMES.contains(&value["driver"].as_str().unwrap_or(""))
        || !ERROR_NAMES.contains(&value["error_class"].as_str().unwrap_or(""))
        || !BUCKET_NAMES.contains(&value["duration_bucket"].as_str().unwrap_or(""))
    {
        return false;
    }
    if !value["open_tabs"].as_u64().is_some_and(|tabs| tabs <= 1000) {
        return false;
    }
    if object.contains_key("duration_ms")
        && (value["event"] != "ui_hang_end"
            || !value["duration_ms"]
                .as_u64()
                .is_some_and(|duration| duration <= 600_000))
    {
        return false;
    }
    for key in ["resident_bytes", "footprint_bytes", "peak_bytes"] {
        if object.contains_key(key)
            && (value["event"] != "memory_sample"
                || !value[key]
                    .as_f64()
                    .is_some_and(|bytes| (0.0..=1.0e16).contains(&bytes)))
        {
            return false;
        }
    }
    true
}
const EVENT_NAMES: &[&str] = &[
    "startup",
    "shutdown",
    "connection_succeeded",
    "connection_failed",
    "query_succeeded",
    "query_failed",
    "result_page",
    "export_succeeded",
    "export_failed",
    "cancelled",
    "ui_hang_start",
    "ui_hang_end",
    "unclean_exit",
    "error",
    "memory_sample",
];
const DRIVER_NAMES: &[&str] = &["unknown", "sqlite", "postgresql", "mysql"];
const ERROR_NAMES: &[&str] = &[
    "unknown",
    "connection",
    "authentication",
    "query",
    "io",
    "internal",
];
const BUCKET_NAMES: &[&str] = &[
    "unknown",
    "under_100_ms",
    "under_1_s",
    "under_10_s",
    "over_10_s",
];

fn append_bounded(path: &Path, line: &[u8], priority: bool, dropped: &AtomicU64) -> bool {
    if path.is_symlink() || line.len() > MAX_DAY_BYTES {
        return false;
    }
    let size = fs::metadata(path)
        .map(|meta| meta.len() as usize)
        .unwrap_or(0);
    if size > MAX_DAY_BYTES {
        if atomic_write(path, line).is_err() {
            return false;
        }
        dropped.fetch_add(1, Ordering::Relaxed);
        return true;
    }
    if size + line.len() > MAX_DAY_BYTES {
        let Ok(bytes) = fs::read(path) else {
            return false;
        };
        let Some(day) = path
            .file_name()
            .and_then(|name| day_file(&name.to_string_lossy()))
        else {
            return false;
        };
        let mut kept = VecDeque::new();
        let mut total = 0;
        for raw in bytes.split_inclusive(|byte| *byte == b'\n') {
            let parsed = serde_json::from_slice::<Value>(raw).ok();
            if raw.len() > 1024
                || !raw.ends_with(b"\n")
                || !parsed
                    .as_ref()
                    .is_some_and(|value| valid_record(value, day))
            {
                dropped.fetch_add(1, Ordering::Relaxed);
                continue;
            }
            let canonical = json_line(parsed.as_ref().unwrap());
            total += canonical.len();
            kept.push_back(canonical);
        }
        while total + line.len() > MAX_DAY_BYTES && !kept.is_empty() {
            let remove = kept
                .iter()
                .position(|bytes| {
                    let value = serde_json::from_slice::<Value>(bytes).unwrap_or(Value::Null);
                    !matches!(
                        value["event"].as_str().unwrap_or(""),
                        "connection_failed"
                            | "query_failed"
                            | "ui_hang_start"
                            | "ui_hang_end"
                            | "unclean_exit"
                            | "error"
                            | "export_failed"
                    )
                })
                .or(if priority { Some(0) } else { None });
            let Some(index) = remove else {
                return false;
            };
            total -= kept.remove(index).unwrap().len();
            dropped.fetch_add(1, Ordering::Relaxed);
        }
        let mut replacement = Vec::with_capacity(total);
        for item in kept {
            replacement.extend_from_slice(&item);
        }
        if atomic_write(path, &replacement).is_err() {
            return false;
        }
    }
    let Ok(mut output) = OpenOptions::new().append(true).create(true).open(path) else {
        return false;
    };
    private_permissions(path, false);
    output.write_all(line).is_ok() && output.flush().is_ok()
}

struct Snapshot {
    events: Vec<u8>,
    summary: Summary,
    invalid_records: u64,
    omitted_by_cap: u64,
}
fn snapshot(folder: &Path, dropped: u64) -> Snapshot {
    let mut result = Snapshot {
        events: Vec::new(),
        summary: Summary {
            dropped_records: dropped,
            unavailable_categories: vec!["crash_signature".into(), "native_thread_sample".into()],
            ..Summary::default()
        },
        invalid_records: 0,
        omitted_by_cap: 0,
    };
    let mut resident = false;
    let mut footprint = false;
    let mut peak = false;
    let today = date::today();
    for offset in (0..EXPORT_DAYS).rev() {
        let day = today - offset;
        let path = folder.join(format!("{}.jsonl", date::day_name(day)));
        let Ok(metadata) = fs::symlink_metadata(&path) else {
            continue;
        };
        if metadata.file_type().is_symlink() || !metadata.is_file() {
            continue;
        }
        let Ok(file) = File::open(path) else {
            continue;
        };
        let mut data = Vec::new();
        if file
            .take(MAX_DAY_BYTES as u64 + 1)
            .read_to_end(&mut data)
            .is_err()
        {
            continue;
        }
        for raw in data.split_inclusive(|byte| *byte == b'\n') {
            if raw.len() > 1024 || !raw.ends_with(b"\n") {
                result.invalid_records += 1;
                continue;
            }
            let Ok(value) = serde_json::from_slice::<Value>(raw) else {
                result.invalid_records += 1;
                continue;
            };
            if !valid_record(&value, day) {
                result.invalid_records += 1;
                continue;
            }
            let canonical = json_line(&value);
            if result.events.len() + canonical.len() > MAX_EXPORT_BYTES - 8192 {
                result.omitted_by_cap += 1;
                continue;
            }
            result.events.extend_from_slice(&canonical);
            let event = value["event"].as_str().unwrap_or("unknown");
            *result
                .summary
                .category_counts
                .entry(event.into())
                .or_default() += 1;
            let bucket = value["duration_bucket"].as_str().unwrap_or("unknown");
            if bucket != "unknown" {
                *result
                    .summary
                    .duration_bucket_counts
                    .entry(bucket.into())
                    .or_default() += 1;
            }
            if event == "memory_sample" {
                resident |= value.get("resident_bytes").is_some();
                footprint |= value.get("footprint_bytes").is_some();
                peak |= value.get("peak_bytes").is_some();
            }
            let utc = value["utc"].as_str().unwrap_or("");
            if result.summary.from_utc.is_empty() || utc < &result.summary.from_utc {
                result.summary.from_utc = utc.into();
            }
            if result.summary.to_utc.is_empty() || utc > &result.summary.to_utc {
                result.summary.to_utc = utc.into();
            }
        }
        if metadata.len() > MAX_DAY_BYTES as u64 {
            result.invalid_records += 1;
        }
    }
    if !resident {
        result
            .summary
            .unavailable_categories
            .push("resident_memory".into());
    }
    if !footprint {
        result
            .summary
            .unavailable_categories
            .push("physical_footprint".into());
    }
    if !peak {
        result
            .summary
            .unavailable_categories
            .push("peak_memory".into());
    }
    result.summary.has_history = !result.events.is_empty();
    result.summary.estimated_bytes = result.events.len() as u64 + 2048;
    result
}
