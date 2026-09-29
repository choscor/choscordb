//! Typed transport for Rust-owned signed update and readiness policy.
use crate::ffi::{
    UpdateCheckDto, UpdateConsentDto, UpdateDownloadDto, UpdateHelperDto, UpdateInstallDto,
    UpdateParseDto, UpdateProgressDto, UpdateRecordDto, UpdateVerifyDto,
};
#[cfg(target_os = "linux")]
use base64::Engine as _;
use choscordb_update::{
    CheckedUpdate, InstallError, NetworkError, StagedUpdate, StagingLocation, UpdateNetworkService,
    UpdatePreferenceStore, UpdateRecord, parse_signed_update_metadata, verify_update_file,
};
use std::{
    path::Path,
    sync::{
        Mutex,
        atomic::{AtomicBool, AtomicU64, Ordering},
    },
};
use tokio_util::sync::CancellationToken;

struct UpdateSessionState {
    available: Option<CheckedUpdate>,
    staged: Option<(CheckedUpdate, StagedUpdate)>,
    cancellation: CancellationToken,
}

pub struct RustUpdateSession {
    service: Option<UpdateNetworkService>,
    runtime: Option<tokio::runtime::Runtime>,
    error: String,
    platform: String,
    state: Mutex<UpdateSessionState>,
    received: AtomicU64,
    total: AtomicU64,
    verifying: AtomicBool,
}

fn update_session(
    service: Result<UpdateNetworkService, NetworkError>,
    platform: &str,
) -> Box<RustUpdateSession> {
    let (service, error) = match service {
        Ok(service) => (Some(service), String::new()),
        Err(error) => (None, error.to_string()),
    };
    let runtime = tokio::runtime::Builder::new_current_thread()
        .enable_all()
        .build();
    let (runtime, runtime_error) = match runtime {
        Ok(runtime) => (Some(runtime), String::new()),
        Err(error) => (None, error.to_string()),
    };
    Box::new(RustUpdateSession {
        service,
        runtime,
        error: if error.is_empty() {
            runtime_error
        } else {
            error
        },
        platform: platform.into(),
        state: Mutex::new(UpdateSessionState {
            available: None,
            staged: None,
            cancellation: CancellationToken::new(),
        }),
        received: AtomicU64::new(0),
        total: AtomicU64::new(0),
        verifying: AtomicBool::new(false),
    })
}

pub fn update_session_new(
    feed_base: &str,
    public_key: &[u8],
    platform: &str,
    arch: &str,
    repository: &str,
) -> Box<RustUpdateSession> {
    update_session(
        UpdateNetworkService::new_production(feed_base, public_key, platform, arch, repository),
        platform,
    )
}

pub fn update_session_new_failure_fixture(feed_url: &str) -> Box<RustUpdateSession> {
    update_session(
        UpdateNetworkService::new_failure_feed_fixture(feed_url),
        "fixture",
    )
}

pub fn update_session_begin_check(session: &RustUpdateSession) {
    let mut state = session.state.lock().expect("updater state");
    state.cancellation.cancel();
    state.cancellation = CancellationToken::new();
}

pub fn update_session_check(session: &RustUpdateSession, current_version: &str) -> UpdateCheckDto {
    let (Some(service), Some(runtime)) = (&session.service, &session.runtime) else {
        return UpdateCheckDto {
            found: false,
            record: UpdateRecordDto::default(),
            error: session.error.clone(),
        };
    };
    let cancellation = session
        .state
        .lock()
        .expect("updater state")
        .cancellation
        .clone();
    match runtime.block_on(service.check(current_version, &cancellation)) {
        Ok(Some(checked)) => {
            let record = record_to_dto(checked.record().clone());
            session.state.lock().expect("updater state").available = Some(checked);
            UpdateCheckDto {
                found: true,
                record,
                error: String::new(),
            }
        }
        Ok(None) => UpdateCheckDto {
            found: false,
            record: UpdateRecordDto::default(),
            error: String::new(),
        },
        Err(error) => UpdateCheckDto {
            found: false,
            record: UpdateRecordDto::default(),
            error: error.to_string(),
        },
    }
}

pub fn update_session_cancel(session: &RustUpdateSession) {
    session
        .state
        .lock()
        .expect("updater state")
        .cancellation
        .cancel();
}

pub fn update_session_discard_staged(session: &RustUpdateSession) {
    session.state.lock().expect("updater state").staged = None;
}

pub fn update_session_progress(session: &RustUpdateSession) -> UpdateProgressDto {
    UpdateProgressDto {
        received: session.received.load(Ordering::Relaxed),
        total: session.total.load(Ordering::Relaxed),
        verifying: session.verifying.load(Ordering::Relaxed),
    }
}

pub fn update_session_download(
    session: &RustUpdateSession,
    appimage: &str,
    invoked: &str,
) -> UpdateDownloadDto {
    #[cfg(not(target_os = "linux"))]
    let _ = (appimage, invoked);
    let (Some(service), Some(runtime)) = (&session.service, &session.runtime) else {
        return UpdateDownloadDto {
            success: false,
            cancelled: false,
            staging_unavailable: false,
            error: session.error.clone(),
        };
    };
    let (checked, cancellation) = {
        let state = session.state.lock().expect("updater state");
        (state.available.clone(), state.cancellation.clone())
    };
    let Some(checked) = checked else {
        return UpdateDownloadDto {
            success: false,
            cancelled: false,
            staging_unavailable: false,
            error: "No authenticated update is available".into(),
        };
    };
    // Replacing a previously verified download can unlink its temporary file.
    // This function is called by the Qt worker, never by the event thread.
    session.state.lock().expect("updater state").staged = None;
    let staging = match session.platform.as_str() {
        "windows" => Ok(StagingLocation::for_windows_temp()),
        #[cfg(target_os = "linux")]
        "linux" => StagingLocation::for_linux_appimage(Path::new(appimage), Path::new(invoked)),
        _ => Err(NetworkError::StagingUnavailable),
    };
    let Ok(staging) = staging else {
        return UpdateDownloadDto {
            success: false,
            cancelled: false,
            staging_unavailable: true,
            error: NetworkError::StagingUnavailable.to_string(),
        };
    };
    session.received.store(0, Ordering::Relaxed);
    session
        .total
        .store(checked.record().size, Ordering::Relaxed);
    session.verifying.store(false, Ordering::Relaxed);
    let result =
        runtime.block_on(
            service.download(&checked, &staging, &cancellation, |received, total| {
                session.received.store(received, Ordering::Relaxed);
                session.total.store(total, Ordering::Relaxed);
                if received == total && total > 0 {
                    session.verifying.store(true, Ordering::Relaxed);
                }
            }),
        );
    session.verifying.store(false, Ordering::Relaxed);
    match result {
        Ok(staged) => {
            session.state.lock().expect("updater state").staged = Some((checked, staged));
            UpdateDownloadDto {
                success: true,
                cancelled: false,
                staging_unavailable: false,
                error: String::new(),
            }
        }
        Err(error) => UpdateDownloadDto {
            success: false,
            cancelled: matches!(error, NetworkError::Cancelled),
            staging_unavailable: matches!(error, NetworkError::StagingUnavailable),
            error: error.to_string(),
        },
    }
}

pub fn update_session_install(
    session: &RustUpdateSession,
    appimage: &str,
    invoked: &str,
    parent_pid: u32,
) -> UpdateInstallDto {
    #[cfg(not(target_os = "linux"))]
    let _ = (appimage, invoked);
    let staged = session.state.lock().expect("updater state").staged.take();
    let Some((checked, staged)) = staged else {
        return UpdateInstallDto {
            success: false,
            pid: 0,
            invalid_package: true,
            error: "No staged update is available".into(),
        };
    };
    let command = match session.platform.as_str() {
        "windows" => choscordb_update::prepare_windows_install(&checked, staged.path(), parent_pid),
        #[cfg(target_os = "linux")]
        "linux" => choscordb_update::prepare_linux_install(
            &checked,
            staged.path(),
            Path::new(appimage),
            Path::new(invoked),
            parent_pid,
        ),
        _ => Err(InstallError::UnsupportedPlatform),
    };
    let command = match command {
        Ok(command) => command,
        Err(error) => {
            let invalid_package = matches!(error, InstallError::WindowsPackageInvalid);
            if !invalid_package {
                session.state.lock().expect("updater state").staged = Some((checked, staged));
            }
            return UpdateInstallDto {
                success: false,
                pid: 0,
                invalid_package,
                error: error.to_string(),
            };
        }
    };
    match choscordb_update::launch_prepared_install(command, staged) {
        Ok(pid) => UpdateInstallDto {
            success: true,
            pid,
            invalid_package: false,
            error: String::new(),
        },
        Err(error) => UpdateInstallDto {
            success: false,
            pid: 0,
            invalid_package: matches!(error, InstallError::WindowsPackageInvalid),
            error: error.to_string(),
        },
    }
}

pub fn update_run_linux_helper(
    arguments: Vec<String>,
    public_key: &[u8],
    current_version: &str,
    repository: &str,
    appimage: &str,
    invoked: &str,
) -> UpdateHelperDto {
    #[cfg(target_os = "linux")]
    {
        if arguments.len() != 5 || arguments[1] != "--apply-update" {
            return UpdateHelperDto {
                success: false,
                error: "Invalid update helper arguments".into(),
                manual_url: String::new(),
            };
        }
        let envelope = base64::engine::general_purpose::STANDARD
            .decode(&arguments[3])
            .unwrap_or_default();
        if base64::engine::general_purpose::STANDARD.encode(&envelope) != arguments[3] {
            return UpdateHelperDto {
                success: false,
                error: InstallError::AuthenticationFailed.to_string(),
                manual_url: String::new(),
            };
        }
        let manual_url = parse_signed_update_metadata(
            &envelope,
            public_key,
            current_version,
            "linux",
            "x86_64",
            repository,
        )
        .ok()
        .flatten()
        .map_or_else(String::new, |record| record.url);
        let pid = arguments[4].parse::<u32>().unwrap_or(0);
        let request = choscordb_update::prepare_linux_helper(
            &envelope,
            public_key,
            current_version,
            repository,
            Path::new(appimage),
            Path::new(invoked),
            Path::new(&arguments[2]),
            pid,
        );
        let request = match request {
            Ok(request) => request,
            Err(error) => {
                return UpdateHelperDto {
                    success: false,
                    error: error.to_string(),
                    manual_url,
                };
            }
        };
        match choscordb_update::run_linux_helper(&request) {
            Ok(_) => UpdateHelperDto {
                success: true,
                error: String::new(),
                manual_url: String::new(),
            },
            Err(error) => UpdateHelperDto {
                success: false,
                error: error.to_string(),
                manual_url,
            },
        }
    }
    #[cfg(not(target_os = "linux"))]
    {
        let _ = (
            arguments,
            public_key,
            current_version,
            repository,
            appimage,
            invoked,
        );
        UpdateHelperDto {
            success: false,
            error: InstallError::UnsupportedPlatform.to_string(),
            manual_url: String::new(),
        }
    }
}

fn record_to_dto(record: UpdateRecord) -> UpdateRecordDto {
    UpdateRecordDto {
        version: record.version,
        url: record.url,
        size: record.size,
        sha256: record.sha256.into(),
        notes: record.notes,
    }
}

fn record_from_dto(dto: UpdateRecordDto) -> Option<UpdateRecord> {
    if dto.size == 0 {
        return None;
    }
    Some(UpdateRecord {
        version: dto.version,
        url: dto.url,
        size: dto.size,
        sha256: dto.sha256.try_into().ok()?,
        notes: dto.notes,
    })
}

pub fn update_parse_signed_metadata(
    envelope: &[u8],
    public_key: &[u8],
    current_version: &str,
    platform: &str,
    arch: &str,
    repository: &str,
) -> UpdateParseDto {
    match parse_signed_update_metadata(
        envelope,
        public_key,
        current_version,
        platform,
        arch,
        repository,
    ) {
        Ok(Some(record)) => UpdateParseDto {
            found: true,
            record: record_to_dto(record),
            error: String::new(),
        },
        Ok(None) => UpdateParseDto {
            found: false,
            record: UpdateRecordDto::default(),
            error: String::new(),
        },
        Err(error) => UpdateParseDto {
            found: false,
            record: UpdateRecordDto::default(),
            error: error.to_string(),
        },
    }
}

pub fn update_verify_file(path: &str, record: UpdateRecordDto) -> UpdateVerifyDto {
    let error = record_from_dto(record)
        .ok_or(choscordb_update::UpdateError::PackageIntegrityFailed)
        .and_then(|record| verify_update_file(Path::new(path), &record))
        .err()
        .map_or_else(String::new, |error| error.to_string());
    UpdateVerifyDto {
        success: error.is_empty(),
        error,
    }
}

pub fn update_write_readiness(path: &str) -> bool {
    choscordb_update::write_update_readiness_file(Path::new(path))
}

pub fn update_take_windows_failure_marker() -> bool {
    choscordb_update::take_windows_install_failure_marker()
}

pub fn update_consent_load(directory: &str) -> UpdateConsentDto {
    match UpdatePreferenceStore::new(Path::new(directory)).load() {
        Ok(Some(value)) => UpdateConsentDto {
            has_value: true,
            value,
            error: String::new(),
        },
        Ok(None) => UpdateConsentDto {
            has_value: false,
            value: false,
            error: String::new(),
        },
        Err(error) => UpdateConsentDto {
            has_value: false,
            value: false,
            error: error.to_string(),
        },
    }
}

pub fn update_consent_load_native(directory: &str) -> UpdateConsentDto {
    match UpdatePreferenceStore::new(Path::new(directory)).load_with_native_legacy() {
        Ok(value) => UpdateConsentDto {
            has_value: value.is_some(),
            value: value.unwrap_or(false),
            error: String::new(),
        },
        Err(error) => UpdateConsentDto {
            has_value: false,
            value: false,
            error: error.to_string(),
        },
    }
}

pub fn update_consent_load_legacy_ini(directory: &str, paths: Vec<String>) -> UpdateConsentDto {
    let paths = paths
        .into_iter()
        .map(std::path::PathBuf::from)
        .collect::<Vec<_>>();
    match UpdatePreferenceStore::new(Path::new(directory)).load_with_legacy_ini(&paths) {
        Ok(value) => UpdateConsentDto {
            has_value: value.is_some(),
            value: value.unwrap_or(false),
            error: String::new(),
        },
        Err(error) => UpdateConsentDto {
            has_value: false,
            value: false,
            error: error.to_string(),
        },
    }
}

pub fn update_consent_migrate(
    directory: &str,
    has_legacy: bool,
    legacy_value: bool,
) -> UpdateConsentDto {
    let legacy = has_legacy.then_some(legacy_value);
    match UpdatePreferenceStore::new(Path::new(directory)).load_or_migrate(legacy) {
        Ok(Some(value)) => UpdateConsentDto {
            has_value: true,
            value,
            error: String::new(),
        },
        Ok(None) => UpdateConsentDto {
            has_value: false,
            value: false,
            error: String::new(),
        },
        Err(error) => UpdateConsentDto {
            has_value: false,
            value: false,
            error: error.to_string(),
        },
    }
}

pub fn update_consent_save(directory: &str, value: bool) -> String {
    UpdatePreferenceStore::new(Path::new(directory))
        .save(value)
        .err()
        .map_or_else(String::new, |error| error.to_string())
}
