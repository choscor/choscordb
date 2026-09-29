use choscordb_diagnostics::{
    Driver, DurationBucket, EngineBoundaryEvent, ErrorClass, Event, Record, Service,
};
use serde_json::Value;
use std::{fs, sync::atomic::AtomicBool};

fn events(folder: &std::path::Path) -> Vec<Value> {
    let mut result = Vec::new();
    for entry in fs::read_dir(folder).unwrap() {
        let path = entry.unwrap().path();
        if path
            .extension()
            .is_some_and(|extension| extension == "jsonl")
        {
            for line in fs::read_to_string(path).unwrap().lines() {
                result.push(serde_json::from_str(line).unwrap());
            }
        }
    }
    result
}

fn zip_member(path: &std::path::Path, wanted: &[u8]) -> Vec<u8> {
    let bytes = fs::read(path).unwrap();
    let mut offset = 0;
    while offset + 30 <= bytes.len() && &bytes[offset..offset + 4] == b"PK\x03\x04" {
        let size = u32::from_le_bytes(bytes[offset + 18..offset + 22].try_into().unwrap()) as usize;
        let name_size =
            u16::from_le_bytes(bytes[offset + 26..offset + 28].try_into().unwrap()) as usize;
        let extra_size =
            u16::from_le_bytes(bytes[offset + 28..offset + 30].try_into().unwrap()) as usize;
        let name = &bytes[offset + 30..offset + 30 + name_size];
        let start = offset + 30 + name_size + extra_size;
        if name == wanted {
            return bytes[start..start + size].to_vec();
        }
        offset = start + size;
    }
    panic!("ZIP member missing");
}

#[test]
fn typed_record_is_written_with_existing_schema_and_no_freeform_fields() {
    let root = tempfile::tempdir().unwrap();
    let service = Service::new(root.path(), "1.2.3", "build-4");
    assert!(service.start());
    service.record(Record {
        event: Event::QueryFailed,
        driver: Driver::PostgreSQL,
        error_class: ErrorClass::Query,
        duration_bucket: DurationBucket::Under1s,
        open_tabs: 3,
        ..Record::default()
    });
    service.flush();
    let records = events(service.folder_path());
    let failure = records
        .iter()
        .find(|item| item["event"] == "query_failed")
        .unwrap();
    assert_eq!(failure["schema"], 1);
    assert_eq!(failure["driver"], "postgresql");
    assert_eq!(failure["error_class"], "query");
    assert_eq!(failure["duration_bucket"], "under_1_s");
    assert_eq!(failure["open_tabs"], 3);
    assert_eq!(failure.as_object().unwrap().len(), 7);
    service.stop();
}

#[test]
fn records_queued_before_background_start_are_kept() {
    let root = tempfile::tempdir().unwrap();
    let service = Service::new(root.path(), "1.2.3", "build-4");
    service.record(Record {
        event: Event::MemorySample,
        ..Record::default()
    });
    assert!(service.start());
    service.flush();
    assert!(
        events(service.folder_path())
            .iter()
            .any(|value| value["event"] == "memory_sample")
    );
    service.stop();
}

#[test]
fn export_cancellation_preserves_existing_destination() {
    let root = tempfile::tempdir().unwrap();
    let service = Service::new(root.path(), "1.2.3", "build-4");
    assert!(service.start());
    let destination = root.path().join("existing.zip");
    fs::write(&destination, "keep existing bytes").unwrap();
    let cancelled = AtomicBool::new(true);
    let outcome = service.export_zip(&destination, Some(&cancelled));
    assert!(outcome.cancelled);
    assert_eq!(fs::read(&destination).unwrap(), b"keep existing bytes");
    service.stop();
}

#[test]
fn run_marker_stays_bounded_during_capture_and_disappears_on_clean_stop() {
    let root = tempfile::tempdir().unwrap();
    let service = Service::new(root.path(), "1.2.3", "build-4");
    assert!(service.start());
    for _ in 0..20 {
        service.record(Record {
            event: Event::ResultPage,
            ..Record::default()
        });
    }
    service.flush();
    let markers: Vec<_> = fs::read_dir(service.folder_path())
        .unwrap()
        .map(|entry| entry.unwrap().path())
        .filter(|path| {
            path.extension()
                .is_some_and(|extension| extension == "marker")
        })
        .collect();
    assert_eq!(markers.len(), 1);
    assert_eq!(fs::read(&markers[0]).unwrap(), b"running\n");
    service.stop();
    assert!(!markers[0].exists());
}

#[test]
fn malformed_utf8_calendar_text_is_omitted_without_crashing_preview() {
    let root = tempfile::tempdir().unwrap();
    let service = Service::new(root.path(), "1.2.3", "build-4");
    assert!(service.start());
    service.flush();
    let path = fs::read_dir(service.folder_path())
        .unwrap()
        .map(|entry| entry.unwrap().path())
        .find(|path| {
            path.extension()
                .is_some_and(|extension| extension == "jsonl")
        })
        .unwrap();
    use std::io::Write;
    let mut output = fs::OpenOptions::new().append(true).open(path).unwrap();
    output.write_all(b"{\"schema\":1,\"utc\":\"\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9T00:00:00.000Z\",\"event\":\"error\",\"driver\":\"unknown\",\"error_class\":\"unknown\",\"duration_bucket\":\"unknown\",\"open_tabs\":0}\n").unwrap();
    writeln!(
        output,
        "{}",
        serde_json::json!({
            "schema": 1,
            "utc": "2026-09-29T12:34:界Z",
            "event": "error",
            "driver": "unknown",
            "error_class": "unknown",
            "duration_bucket": "unknown",
            "open_tabs": 0,
        })
    )
    .unwrap();
    assert_eq!(service.preview().category_counts.get("error"), None);
    service.stop();
}

#[cfg(target_os = "macos")]
#[test]
fn memory_sample_keeps_macos_physical_footprint() {
    let root = tempfile::tempdir().unwrap();
    let service = Service::new(root.path(), "1.2.3", "build-4");
    assert!(service.start());
    service.sample_memory(0, true);
    service.flush();
    let sample = events(service.folder_path())
        .into_iter()
        .find(|value| value["event"] == "memory_sample")
        .unwrap();
    assert!(
        sample["footprint_bytes"]
            .as_u64()
            .is_some_and(|bytes| bytes > 0)
    );
    service.stop();
}

#[test]
fn recording_stays_responsive_while_start_waits_for_folder_lock() {
    use fs2::FileExt;
    use std::{
        sync::{Arc, mpsc},
        thread,
        time::Duration,
    };

    let root = tempfile::tempdir().unwrap();
    let folder = root.path().join("diagnostics");
    fs::create_dir(&folder).unwrap();
    let lock = fs::OpenOptions::new()
        .create(true)
        .truncate(false)
        .read(true)
        .write(true)
        .open(folder.join(".io.lock"))
        .unwrap();
    lock.lock_exclusive().unwrap();
    let service = Arc::new(Service::new(root.path(), "1.2.3", "build-4"));
    let starting = Arc::clone(&service);
    let startup = thread::spawn(move || starting.start());
    thread::sleep(Duration::from_millis(100));
    let recording = Arc::clone(&service);
    let (tx, rx) = mpsc::channel();
    let recorder = thread::spawn(move || {
        recording.record(Record {
            event: Event::Error,
            ..Record::default()
        });
        tx.send(()).unwrap();
    });
    let responsive = rx.recv_timeout(Duration::from_millis(250)).is_ok();
    lock.unlock().unwrap();
    assert!(startup.join().unwrap());
    recorder.join().unwrap();
    assert!(responsive, "record blocked on startup filesystem I/O");
    service.flush();
    assert!(
        events(service.folder_path())
            .iter()
            .any(|value| value["event"] == "error")
    );
    service.stop();
}

#[test]
fn export_manifest_and_members_keep_schema_one_contract() {
    let root = tempfile::tempdir().unwrap();
    let service = Service::new(root.path(), "1.2.3", "build-4");
    assert!(service.start());
    service.record(Record {
        event: Event::QueryFailed,
        driver: Driver::SQLite,
        duration_bucket: DurationBucket::Under100Ms,
        ..Record::default()
    });
    let report = root.path().join("report.zip");
    let outcome = service.export_zip(&report, None);
    assert!(outcome.success, "{}", outcome.error);
    let manifest: Value = serde_json::from_slice(&zip_member(&report, b"manifest.json")).unwrap();
    assert_eq!(manifest["schema"], 1);
    assert_eq!(manifest["app_version"], "1.2.3");
    assert_eq!(manifest["build_version"], "build-4");
    assert_eq!(manifest["category_counts"]["query_failed"], 1);
    assert_eq!(manifest["duration_bucket_counts"]["under_100_ms"], 1);
    assert_eq!(manifest["retention_days"], 14);
    assert_eq!(manifest["export_days"], 7);
    assert_eq!(manifest["day_byte_cap"], 256 * 1024);
    assert_eq!(manifest["archive_byte_cap"], 2 * 1024 * 1024);
    assert!(
        manifest["unavailable_categories"]
            .as_array()
            .unwrap()
            .contains(&Value::String("crash_signature".into()))
    );
    let exported = String::from_utf8(zip_member(&report, b"events.jsonl")).unwrap();
    assert!(exported.contains("\"event\":\"query_failed\""));
    assert!(!exported.contains(root.path().to_str().unwrap()));
    service.stop();
}

#[test]
fn export_reports_a_sanitized_os_version_on_supported_platforms() {
    let root = tempfile::tempdir().unwrap();
    let service = Service::new(root.path(), "1.2.3", "build-4");
    assert!(service.start());
    let path = root.path().join("platform.zip");
    assert!(service.export_zip(&path, None).success);
    let manifest: Value = serde_json::from_slice(&zip_member(&path, b"manifest.json")).unwrap();
    let version = manifest["os_version"].as_str().unwrap();
    assert_ne!(version, "unknown");
    assert!(version.len() <= 40);
    assert!(
        version
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || b"._+-".contains(&byte))
    );
    if version == "unknown" {
        assert!(
            manifest["unavailable_categories"]
                .as_array()
                .unwrap()
                .contains(&Value::String("os_version".into()))
        );
    }
    service.stop();
}

#[test]
fn export_marks_missing_build_identifier_as_unavailable() {
    let root = tempfile::tempdir().unwrap();
    let service = Service::new(root.path(), "1.2.3", "");
    assert!(service.start());
    let path = root.path().join("missing-build.zip");
    assert!(service.export_zip(&path, None).success);
    let manifest: Value = serde_json::from_slice(&zip_member(&path, b"manifest.json")).unwrap();
    assert_eq!(manifest["app_version"], "1.2.3");
    assert_eq!(manifest["build_version"], "unknown");
    assert!(
        manifest["unavailable_categories"]
            .as_array()
            .unwrap()
            .contains(&Value::String("build_version".into()))
    );
    service.stop();
}

#[test]
fn export_drops_unapproved_fields_and_ignores_unrelated_files() {
    let root = tempfile::tempdir().unwrap();
    let service = Service::new(root.path(), "password=TOP_SECRET", "build/secret");
    assert!(service.start());
    service.flush();
    let day = fs::read_dir(service.folder_path())
        .unwrap()
        .map(|entry| entry.unwrap().path())
        .find(|path| {
            path.extension()
                .is_some_and(|extension| extension == "jsonl")
        })
        .unwrap();
    let mut value = events(service.folder_path())[0].clone();
    value["event"] = Value::String("query_failed".into());
    value["sql"] = Value::String("SELECT TOP_SECRET FROM private_table".into());
    use std::io::Write;
    writeln!(
        fs::OpenOptions::new().append(true).open(day).unwrap(),
        "{value}"
    )
    .unwrap();
    fs::write(
        service.folder_path().join("unrelated.txt"),
        "PRIVATE_HOST_PATH",
    )
    .unwrap();
    let report = root.path().join("privacy.zip");
    assert!(service.export_zip(&report, None).success);
    let bytes = fs::read(&report).unwrap();
    for secret in [
        "TOP_SECRET",
        "PRIVATE_HOST_PATH",
        "private_table",
        root.path().to_str().unwrap(),
    ] {
        assert!(!String::from_utf8_lossy(&bytes).contains(secret));
    }
    let manifest: Value = serde_json::from_slice(&zip_member(&report, b"manifest.json")).unwrap();
    assert_eq!(manifest["app_version"], "unknown");
    assert!(manifest["invalid_records_omitted"].as_u64().unwrap() >= 1);
    service.stop();
}

#[test]
fn clear_only_removes_diagnostic_days_and_resumes_capture() {
    let root = tempfile::tempdir().unwrap();
    let service = Service::new(root.path(), "1.2.3", "build-4");
    assert!(service.start());
    service.record(Record {
        event: Event::Error,
        ..Record::default()
    });
    service.flush();
    let user_data = root.path().join("profiles.sqlite");
    fs::write(&user_data, b"keep profile data").unwrap();
    assert_eq!(service.preview().category_counts["error"], 1);
    service.clear().unwrap();
    assert!(!service.preview().has_history);
    assert_eq!(fs::read(user_data).unwrap(), b"keep profile data");
    service.record(Record {
        event: Event::Error,
        ..Record::default()
    });
    assert_eq!(service.preview().category_counts["error"], 1);
    service.stop();
}

#[test]
fn day_cap_replaces_oversized_artifact_with_failure_and_counts_drop() {
    let root = tempfile::tempdir().unwrap();
    let service = Service::new(root.path(), "1.2.3", "build-4");
    assert!(service.start());
    service.flush();
    let day = fs::read_dir(service.folder_path())
        .unwrap()
        .map(|entry| entry.unwrap().path())
        .find(|path| {
            path.extension()
                .is_some_and(|extension| extension == "jsonl")
        })
        .unwrap();
    fs::write(&day, vec![b'x'; 270 * 1024]).unwrap();
    service.record(Record {
        event: Event::QueryFailed,
        error_class: ErrorClass::Query,
        ..Record::default()
    });
    service.flush();
    assert!(fs::metadata(day).unwrap().len() <= 256 * 1024);
    assert_eq!(service.preview().category_counts["query_failed"], 1);
    assert!(service.preview().dropped_records >= 1);
    service.stop();
}

#[test]
fn engine_outcomes_are_classified_in_rust_without_sql_or_error_text() {
    let root = tempfile::tempdir().unwrap();
    let service = Service::new(root.path(), "1.2.3", "build-4");
    assert!(service.start());
    service.observe_connection(Driver::PostgreSQL, false, 2);
    service.observe_engine_event(EngineBoundaryEvent::QueryQueued(7), 2);
    service.observe_engine_event(
        EngineBoundaryEvent::QueryFailed {
            id: 7,
            cancelled: false,
        },
        2,
    );
    service.observe_engine_event(EngineBoundaryEvent::QueryQueued(8), 2);
    service.observe_engine_event(
        EngineBoundaryEvent::QueryFailed {
            id: 8,
            cancelled: true,
        },
        2,
    );
    service.observe_command_failure();
    service.flush();
    let records = events(service.folder_path());
    assert!(
        records
            .iter()
            .any(|item| item["event"] == "connection_failed" && item["driver"] == "postgresql")
    );
    assert!(
        records
            .iter()
            .any(|item| item["event"] == "query_failed" && item["error_class"] == "query")
    );
    assert!(records.iter().any(|item| item["event"] == "cancelled"));
    assert!(
        records
            .iter()
            .any(|item| item["event"] == "error" && item["error_class"] == "internal")
    );
    assert!(
        !records
            .iter()
            .any(|item| item["event"] == "query_succeeded")
    );
    service.stop();
}

#[test]
fn ui_hang_duration_is_bounded_and_bucketed_by_backend() {
    let root = tempfile::tempdir().unwrap();
    let service = Service::new(root.path(), "1.2.3", "build-4");
    assert!(service.start());
    service.record(Record {
        event: Event::UiHangEnd,
        duration_ms: 2_400,
        ..Record::default()
    });
    service.flush();
    let hang = events(service.folder_path())
        .into_iter()
        .find(|item| item["event"] == "ui_hang_end")
        .unwrap();
    assert_eq!(hang["duration_ms"], 2_400);
    assert_eq!(hang["duration_bucket"], "under_10_s");
    service.stop();
}

#[cfg(unix)]
#[test]
fn lock_symlink_is_rejected_without_touching_its_target() {
    use std::os::unix::fs::symlink;
    let root = tempfile::tempdir().unwrap();
    let folder = root.path().join("diagnostics");
    fs::create_dir(&folder).unwrap();
    let target = root.path().join("unrelated.txt");
    fs::write(&target, b"keep user data").unwrap();
    symlink(&target, folder.join(".io.lock")).unwrap();
    let service = Service::new(root.path(), "1.2.3", "build-4");
    assert!(!service.start());
    assert_eq!(fs::read(target).unwrap(), b"keep user data");
}
