use super::{FileIdentity, InstallError, LinuxHelperRequest, linux_stage_valid};
use crate::{StagingLocation, verify_update_file, wait_for_update_readiness};
use rustix::{
    event::{PollFd, PollFlags, Timespec, poll},
    fd::OwnedFd,
    fs::{CWD, RenameFlags, renameat_with},
    io::Errno,
    process::{Pid, PidfdFlags, Signal, pidfd_open, pidfd_send_signal},
};
use std::{
    fs::{self, File},
    io::Read,
    os::{unix::ffi::OsStrExt, unix::fs::PermissionsExt, unix::process::CommandExt},
    path::Path,
    process::{Command, Stdio},
    thread,
    time::Duration,
};

#[derive(Clone, Copy, PartialEq, Eq)]
enum ProcessState {
    Running,
    Exited,
    Unknown,
}

struct ProcessHandle {
    descriptor: Option<OwnedFd>,
    missing: bool,
}

impl ProcessHandle {
    fn open(raw_pid: i32) -> Self {
        let Some(pid) = Pid::from_raw(raw_pid) else {
            return Self {
                descriptor: None,
                missing: false,
            };
        };
        match pidfd_open(pid, PidfdFlags::empty()) {
            Ok(descriptor) => Self {
                descriptor: Some(descriptor),
                missing: false,
            },
            Err(Errno::SRCH) => Self {
                descriptor: None,
                missing: true,
            },
            Err(_) => Self {
                descriptor: None,
                missing: false,
            },
        }
    }

    fn valid(&self) -> bool {
        self.descriptor.is_some()
    }

    fn state(&self) -> ProcessState {
        let Some(descriptor) = &self.descriptor else {
            return if self.missing {
                ProcessState::Exited
            } else {
                ProcessState::Unknown
            };
        };
        let mut fds = [PollFd::new(descriptor, PollFlags::IN)];
        match poll(&mut fds, Some(&Timespec::default())) {
            Ok(0) => ProcessState::Running,
            Ok(1) if fds[0].revents().contains(PollFlags::IN) => ProcessState::Exited,
            _ => ProcessState::Unknown,
        }
    }

    fn signal(&self, signal: Signal) -> bool {
        self.descriptor
            .as_ref()
            .is_some_and(|fd| pidfd_send_signal(fd, signal).is_ok())
    }
}

fn exchange_files(first: &Path, second: &Path) -> bool {
    renameat_with(CWD, first, CWD, second, RenameFlags::EXCHANGE).is_ok()
}

fn rollback_exchange(request: &LinuxHelperRequest) -> bool {
    if FileIdentity::at(&request.target) != Some(request.staged_identity)
        || FileIdentity::at(&request.staged) != Some(request.target_identity)
    {
        return false;
    }
    exchange_files(&request.staged, &request.target)
        && FileIdentity::at(&request.target) == Some(request.target_identity)
}

fn stop_process(handle: &ProcessHandle) -> bool {
    if handle.state() == ProcessState::Exited {
        return true;
    }
    if handle.state() != ProcessState::Running {
        return false;
    }
    if !handle.signal(Signal::TERM) {
        return handle.state() == ProcessState::Exited;
    }
    for _ in 0..20 {
        if handle.state() == ProcessState::Exited {
            return true;
        }
        thread::sleep(Duration::from_millis(100));
    }
    if !handle.signal(Signal::KILL) {
        return handle.state() == ProcessState::Exited;
    }
    for _ in 0..20 {
        if handle.state() == ProcessState::Exited {
            return true;
        }
        thread::sleep(Duration::from_millis(100));
    }
    handle.state() == ProcessState::Exited
}

fn has_readiness_environment(pid: u32, path: &Path) -> bool {
    let Ok(file) = File::open(format!("/proc/{pid}/environ")) else {
        return false;
    };
    let mut bytes = Vec::new();
    if file.take(1024 * 1024).read_to_end(&mut bytes).is_err() {
        return false;
    }
    let mut expected = b"CHOSCORDB_UPDATE_READY_FILE=".to_vec();
    expected.extend_from_slice(path.as_os_str().as_bytes());
    bytes
        .split(|byte| *byte == 0)
        .any(|entry| entry == expected)
}

fn unused_readiness_path() -> Option<std::path::PathBuf> {
    let marker = tempfile::Builder::new()
        .prefix("ChoscorDB-update-ready-")
        .suffix(".txt")
        .tempfile_in(std::env::temp_dir())
        .ok()?;
    let path = marker.path().to_path_buf();
    marker.close().ok()?;
    Some(path)
}

pub fn run_linux_helper(request: &LinuxHelperRequest) -> Result<u32, InstallError> {
    let self_pid =
        i32::try_from(std::process::id()).map_err(|_| InstallError::ProcessStateUnknown)?;
    let support = ProcessHandle::open(self_pid);
    let parent = ProcessHandle::open(request.parent_pid);
    if !support.valid() || parent.state() == ProcessState::Unknown {
        return Err(InstallError::ProcessStateUnknown);
    }
    for _ in 0..900 {
        match parent.state() {
            ProcessState::Exited => break,
            ProcessState::Unknown => return Err(InstallError::ProcessStateUnknown),
            ProcessState::Running => thread::sleep(Duration::from_millis(100)),
        }
    }
    let ready_path = unused_readiness_path().ok_or(InstallError::StartupUnverified)?;
    if parent.state() != ProcessState::Exited
        || StagingLocation::for_linux_appimage(&request.target, &request.target).is_err()
        || FileIdentity::at(&request.target) != Some(request.target_identity)
        || FileIdentity::at(&request.staged) != Some(request.staged_identity)
        || !linux_stage_valid(
            &request.target,
            &request.staged,
            &request.record,
            request.parent_pid as u32,
        )
        || fs::set_permissions(&request.staged, fs::Permissions::from_mode(0o755)).is_err()
        || FileIdentity::at(&request.target) != Some(request.target_identity)
        || FileIdentity::at(&request.staged) != Some(request.staged_identity)
        || !exchange_files(&request.staged, &request.target)
    {
        return Err(InstallError::ReplacementFailed);
    }
    let valid_replacement = FileIdentity::at(&request.target) == Some(request.staged_identity)
        && FileIdentity::at(&request.staged) == Some(request.target_identity)
        && verify_update_file(&request.target, &request.record).is_ok();
    if !valid_replacement {
        return if rollback_exchange(request) {
            Err(InstallError::ReplacementInvalid)
        } else {
            Err(InstallError::ManualRestoreNeeded)
        };
    }
    // Command::spawn resolves the path again. A concurrent writer can still replace it
    // between this identity check and exec; renameat2 has no inode-conditional exchange.
    // The updater's install directory must be trusted against concurrent mutation.
    if FileIdentity::at(&request.target) != Some(request.staged_identity) {
        return Err(InstallError::ManualRestoreNeeded);
    }
    let launched = Command::new(&request.target)
        .env("CHOSCORDB_UPDATE_READY_FILE", &ready_path)
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .process_group(0)
        .spawn();
    let child_pid = launched.as_ref().ok().map(std::process::Child::id);
    let child = child_pid
        .and_then(|pid| i32::try_from(pid).ok())
        .map(ProcessHandle::open);
    let mut identified = false;
    if let (Some(pid), Some(handle)) = (child_pid, child.as_ref()) {
        if handle.valid() {
            for _ in 0..50 {
                if handle.state() != ProcessState::Running {
                    break;
                }
                if has_readiness_environment(pid, &ready_path) {
                    identified = true;
                    break;
                }
                thread::sleep(Duration::from_millis(100));
            }
        }
    }
    let ready = identified
        && child.as_ref().is_some_and(|handle| {
            wait_for_update_readiness(
                &ready_path,
                Duration::from_secs(60),
                Duration::from_secs(2),
                || handle.state() == ProcessState::Running,
            )
        });
    if !ready {
        let stopped = launched.is_err()
            || child.as_ref().is_some_and(|handle| {
                handle.state() == ProcessState::Exited || identified && stop_process(handle)
            });
        let _ = fs::remove_file(&ready_path);
        if stopped && rollback_exchange(request) {
            let _ = fs::remove_file(&request.staged);
            return Err(InstallError::PreviousRestored);
        }
        return Err(InstallError::ManualRestoreNeeded);
    }
    let _ = fs::remove_file(&ready_path);
    let _ = fs::remove_file(&request.staged);
    Ok(child_pid.expect("identified child has pid"))
}
