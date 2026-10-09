use crate::ffi;
use choscordb_diagnostics::{Driver, DurationBucket, ErrorClass, Event, Record, Service};
use std::{
    panic::{AssertUnwindSafe, catch_unwind},
    sync::{
        Arc,
        atomic::{AtomicBool, Ordering},
    },
};

fn protect<T: Default>(work: impl FnOnce() -> T) -> T {
    catch_unwind(AssertUnwindSafe(work)).unwrap_or_default()
}

pub struct RustDiagnostics {
    service: Arc<Service>,
}

pub fn diagnostics_clone(service: &RustDiagnostics) -> Box<RustDiagnostics> {
    Box::new(RustDiagnostics {
        service: Arc::clone(&service.service),
    })
}

pub struct DiagnosticCancellation {
    cancelled: AtomicBool,
}

pub fn diagnostics_new(
    data_dir: &str,
    app_version: &str,
    build_version: &str,
) -> Box<RustDiagnostics> {
    Box::new(RustDiagnostics {
        service: Arc::new(Service::new(data_dir, app_version, build_version)),
    })
}
pub fn diagnostics_start(service: &RustDiagnostics) -> bool {
    protect(|| service.service.start())
}
pub fn diagnostics_stop(service: &RustDiagnostics) {
    protect(|| service.service.stop());
}
pub fn diagnostics_record(service: &RustDiagnostics, record: ffi::DiagnosticRecordDto) {
    protect(|| {
        let Some(event) = event(record.event) else {
            return;
        };
        let Some(driver) = driver(record.driver) else {
            return;
        };
        let Some(error_class) = error_class(record.error_class) else {
            return;
        };
        let Some(duration_bucket) = duration_bucket(record.duration_bucket) else {
            return;
        };
        service.service.record(Record {
            event,
            driver,
            error_class,
            duration_bucket,
            open_tabs: record.open_tabs,
            duration_ms: record.duration_ms,
            force_memory: record.force_memory,
        });
    });
}
pub fn diagnostics_sample_memory(service: &RustDiagnostics, open_tabs: i32, force: bool) {
    protect(|| service.service.sample_memory(open_tabs, force));
}
pub fn diagnostics_set_open_tabs(service: &RustDiagnostics, open_tabs: i32) {
    protect(|| service.service.set_open_tabs(open_tabs));
}
pub fn diagnostics_flush(service: &RustDiagnostics) {
    protect(|| service.service.flush());
}
pub fn diagnostics_preview(service: &RustDiagnostics) -> ffi::DiagnosticSummaryDto {
    protect(|| {
        let summary = service.service.preview();
        ffi::DiagnosticSummaryDto {
            estimated_bytes: summary.estimated_bytes,
            category_counts: summary
                .category_counts
                .into_iter()
                .map(|(name, count)| ffi::DiagnosticCountDto { name, count })
                .collect(),
            duration_bucket_counts: summary
                .duration_bucket_counts
                .into_iter()
                .map(|(name, count)| ffi::DiagnosticCountDto { name, count })
                .collect(),
            from_utc: summary.from_utc,
            to_utc: summary.to_utc,
            unavailable_categories: summary.unavailable_categories,
            dropped_records: summary.dropped_records,
            has_history: summary.has_history,
        }
    })
}
pub fn diagnostics_export_destination(path: &str) -> String {
    choscordb_diagnostics::export_destination(path)
}
pub fn diagnostics_memory_sample_interval_seconds() -> u64 {
    choscordb_diagnostics::MEMORY_SAMPLE_INTERVAL_SECONDS
}
pub fn diagnostics_export_zip(
    service: &RustDiagnostics,
    destination: &str,
    cancellation: &DiagnosticCancellation,
) -> ffi::DiagnosticExportDto {
    catch_unwind(AssertUnwindSafe(|| {
        let result = service
            .service
            .export_zip(destination, Some(&cancellation.cancelled));
        ffi::DiagnosticExportDto {
            success: result.success,
            cancelled: result.cancelled,
            error: result.error,
        }
    }))
    .unwrap_or_else(|_| ffi::DiagnosticExportDto {
        error: "Diagnostics export unavailable.".into(),
        ..Default::default()
    })
}
pub fn diagnostics_clear(service: &RustDiagnostics) -> ffi::DiagnosticClearDto {
    catch_unwind(AssertUnwindSafe(|| match service.service.clear() {
        Ok(()) => ffi::DiagnosticClearDto {
            success: true,
            error: String::new(),
        },
        Err(error) => ffi::DiagnosticClearDto {
            success: false,
            error,
        },
    }))
    .unwrap_or_else(|_| ffi::DiagnosticClearDto {
        success: false,
        error: "Diagnostics storage unavailable.".into(),
    })
}
pub fn diagnostics_folder_path(service: &RustDiagnostics) -> String {
    protect(|| service.service.folder_path().to_string_lossy().into_owned())
}
pub fn diagnostics_warning(service: &RustDiagnostics) -> String {
    protect(|| service.service.warning())
}
pub fn diagnostics_new_cancellation() -> Box<DiagnosticCancellation> {
    Box::new(DiagnosticCancellation {
        cancelled: AtomicBool::new(false),
    })
}
pub fn diagnostics_cancel(cancellation: &DiagnosticCancellation) {
    cancellation.cancelled.store(true, Ordering::Relaxed);
}
pub fn diagnostics_attach_engine(engine: &mut crate::BridgeEngine, service: &RustDiagnostics) {
    protect(|| {
        if let Some(core) = engine.engine.as_mut() {
            core.set_diagnostics(Arc::clone(&service.service));
        }
    });
}
pub fn diagnostics_observe_command_failure(service: &RustDiagnostics) {
    protect(|| service.service.observe_command_failure());
}

fn event(value: u16) -> Option<Event> {
    Some(match value {
        0 => Event::Startup,
        1 => Event::Shutdown,
        2 => Event::ConnectionSucceeded,
        3 => Event::ConnectionFailed,
        4 => Event::QuerySucceeded,
        5 => Event::QueryFailed,
        6 => Event::ResultPage,
        7 => Event::ExportSucceeded,
        8 => Event::ExportFailed,
        9 => Event::Cancelled,
        10 => Event::UiHangStart,
        11 => Event::UiHangEnd,
        12 => Event::UncleanExit,
        13 => Event::Error,
        14 => Event::MemorySample,
        _ => return None,
    })
}
fn driver(value: u16) -> Option<Driver> {
    Some(match value {
        0 => Driver::Unknown,
        1 => Driver::SQLite,
        2 => Driver::PostgreSQL,
        3 => Driver::MySQL,
        _ => return None,
    })
}
fn error_class(value: u16) -> Option<ErrorClass> {
    Some(match value {
        0 => ErrorClass::Unknown,
        1 => ErrorClass::Connection,
        2 => ErrorClass::Authentication,
        3 => ErrorClass::Query,
        4 => ErrorClass::Io,
        5 => ErrorClass::Internal,
        _ => return None,
    })
}
fn duration_bucket(value: u16) -> Option<DurationBucket> {
    Some(match value {
        0 => DurationBucket::Unknown,
        1 => DurationBucket::Under100Ms,
        2 => DurationBucket::Under1s,
        3 => DurationBucket::Under10s,
        4 => DurationBucket::Over10s,
        _ => return None,
    })
}
