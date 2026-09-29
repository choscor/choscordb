//! Verified update installation. Native process and file operations stay in this Rust service.
#[cfg(target_os = "linux")]
#[path = "linux_helper.rs"]
mod linux_helper;
#[cfg(target_os = "linux")]
use crate::StagingLocation;
#[cfg(target_os = "linux")]
use crate::parse_signed_update_metadata;
use crate::{CheckedUpdate, StagedUpdate, UpdateRecord, verify_update_file};
#[cfg(target_os = "linux")]
use base64::Engine as _;
#[cfg(target_os = "linux")]
pub use linux_helper::run_linux_helper;
use std::{
    path::{Path, PathBuf},
    process::{Command, Stdio},
};

#[derive(Clone, Copy, Debug, PartialEq, Eq, thiserror::Error)]
pub enum InstallError {
    #[error("The update could not be authenticated. Your previous version is unchanged.")]
    AuthenticationFailed,
    #[error(
        "The verified update is no longer safe to install. Your previous version is unchanged."
    )]
    UnsafePackage,
    #[error("Process state could not be checked safely. The previous AppImage remains usable.")]
    ProcessStateUnknown,
    #[error("Update startup could not be verified. The previous AppImage remains usable.")]
    StartupUnverified,
    #[error("The current AppImage could not be safely replaced. It remains usable.")]
    ReplacementFailed,
    #[error(
        "The replacement AppImage failed verification. The previous version was restored where possible."
    )]
    ReplacementInvalid,
    #[error("The new AppImage did not confirm startup. Restore the previous version manually.")]
    ManualRestoreNeeded,
    #[error("The previous AppImage was restored.")]
    PreviousRestored,
    #[error("The installer could not start. Your current version is still available.")]
    LaunchFailed,
    #[error(
        "The downloaded installer failed integrity verification. Please download the update again."
    )]
    WindowsPackageInvalid,
    #[error("This updater is unavailable on this platform")]
    UnsupportedPlatform,
}

pub fn take_windows_install_failure_marker() -> bool {
    #[cfg(windows)]
    {
        let Some(directory) = std::env::var_os("LOCALAPPDATA") else {
            return false;
        };
        let directory = Path::new(&directory);
        if !directory.is_absolute() {
            return false;
        }
        let marker = directory
            .join("ChoscorDB")
            .join("update-install-failure.txt");
        std::fs::remove_file(marker).is_ok()
    }
    #[cfg(not(windows))]
    {
        false
    }
}

#[cfg(target_os = "linux")]
#[derive(Clone, Copy, PartialEq, Eq)]
struct FileIdentity {
    device: u64,
    inode: u64,
}

#[cfg(target_os = "linux")]
impl FileIdentity {
    fn at(path: &Path) -> Option<Self> {
        use std::os::unix::fs::MetadataExt;
        let metadata = std::fs::symlink_metadata(path).ok()?;
        if !metadata.is_file() || metadata.file_type().is_symlink() {
            return None;
        }
        Some(Self {
            device: metadata.dev(),
            inode: metadata.ino(),
        })
    }
}

#[cfg(target_os = "linux")]
pub struct LinuxHelperRequest {
    target: PathBuf,
    staged: PathBuf,
    target_identity: FileIdentity,
    staged_identity: FileIdentity,
    record: UpdateRecord,
    envelope: Vec<u8>,
    parent_pid: i32,
}

pub struct InstallCommand {
    executable: PathBuf,
    arguments: Vec<String>,
    staged: PathBuf,
    record: UpdateRecord,
    kind: InstallKind,
}

#[derive(Clone, Copy)]
enum InstallKind {
    Windows,
    #[cfg(target_os = "linux")]
    Linux(FileIdentity),
}

impl InstallCommand {
    pub fn executable(&self) -> &Path {
        &self.executable
    }
    pub fn arguments(&self) -> &[String] {
        &self.arguments
    }
}

pub fn prepare_windows_install(
    update: &CheckedUpdate,
    staged: &Path,
    parent_pid: u32,
) -> Result<InstallCommand, InstallError> {
    let metadata =
        std::fs::symlink_metadata(staged).map_err(|_| InstallError::WindowsPackageInvalid)?;
    let name = staged
        .file_name()
        .and_then(|name| name.to_str())
        .ok_or(InstallError::WindowsPackageInvalid)?;
    let temp = std::env::temp_dir()
        .canonicalize()
        .map_err(|_| InstallError::WindowsPackageInvalid)?;
    let parent = staged
        .parent()
        .and_then(|parent| parent.canonicalize().ok())
        .ok_or(InstallError::WindowsPackageInvalid)?;
    #[cfg(windows)]
    let trusted_directory = parent
        .to_string_lossy()
        .eq_ignore_ascii_case(&temp.to_string_lossy());
    #[cfg(not(windows))]
    let trusted_directory = parent == temp;
    if update.platform() != "windows"
        || parent_pid <= 1
        || !staged.is_absolute()
        || !metadata.is_file()
        || metadata.file_type().is_symlink()
        || !trusted_directory
        || !name.starts_with("ChoscorDB-update-")
        || !name.ends_with(".exe")
        || verify_update_file(staged, update.record()).is_err()
    {
        return Err(InstallError::WindowsPackageInvalid);
    }
    Ok(InstallCommand {
        executable: staged.to_path_buf(),
        arguments: vec!["/S".into(), format!("/WAITPID={parent_pid}")],
        staged: staged.to_path_buf(),
        record: update.record().clone(),
        kind: InstallKind::Windows,
    })
}

#[cfg(target_os = "linux")]
#[allow(clippy::too_many_arguments)]
pub fn prepare_linux_install(
    update: &CheckedUpdate,
    staged: &Path,
    appimage: &Path,
    invoked: &Path,
    parent_pid: u32,
) -> Result<InstallCommand, InstallError> {
    if update.platform() != "linux" {
        return Err(InstallError::UnsafePackage);
    }
    StagingLocation::for_linux_appimage(appimage, invoked)
        .map_err(|_| InstallError::UnsafePackage)?;
    let target = appimage
        .canonicalize()
        .map_err(|_| InstallError::UnsafePackage)?;
    let target_identity = FileIdentity::at(&target).ok_or(InstallError::UnsafePackage)?;
    if !linux_stage_valid(&target, staged, update.record(), parent_pid) {
        return Err(InstallError::UnsafePackage);
    }
    Ok(InstallCommand {
        executable: target,
        arguments: vec![
            "--apply-update".into(),
            staged.to_string_lossy().into_owned(),
            base64::engine::general_purpose::STANDARD.encode(update.envelope()),
            parent_pid.to_string(),
        ],
        staged: staged.to_path_buf(),
        record: update.record().clone(),
        kind: InstallKind::Linux(target_identity),
    })
}

pub fn launch_prepared_install(
    command: InstallCommand,
    staged: StagedUpdate,
) -> Result<u32, InstallError> {
    let invalid = match command.kind {
        InstallKind::Windows => InstallError::WindowsPackageInvalid,
        #[cfg(target_os = "linux")]
        InstallKind::Linux(_) => InstallError::UnsafePackage,
    };
    if staged.path() != command.staged
        || verify_update_file(staged.path(), &command.record).is_err()
    {
        return Err(invalid);
    }
    let metadata = std::fs::symlink_metadata(staged.path()).map_err(|_| invalid)?;
    if !metadata.is_file() || metadata.file_type().is_symlink() {
        return Err(invalid);
    }
    #[cfg(target_os = "linux")]
    if let InstallKind::Linux(identity) = command.kind {
        if StagingLocation::for_linux_appimage(&command.executable, &command.executable).is_err()
            || FileIdentity::at(&command.executable) != Some(identity)
            || staged.path().parent() != command.executable.parent()
        {
            return Err(invalid);
        }
    }
    let persisted = staged
        .into_temp_path()
        .keep()
        .map_err(|_| InstallError::LaunchFailed)?;
    #[cfg(windows)]
    let _installer_lock = if matches!(command.kind, InstallKind::Windows) {
        use std::os::windows::fs::{MetadataExt, OpenOptionsExt};
        // TempPath::keep changes file attributes, so persist before locking.
        // The handle then refuses writes and renames until CreateProcess opens it.
        let handle = std::fs::OpenOptions::new()
            .read(true)
            .share_mode(0x0000_0001)
            .custom_flags(0x0020_0000)
            .open(&persisted);
        match handle {
            Ok(handle)
                if handle.metadata().is_ok_and(|metadata| {
                    metadata.is_file() && metadata.file_attributes() & 0x0000_0400 == 0
                }) =>
            {
                Some(handle)
            }
            _ => {
                let _ = std::fs::remove_file(&persisted);
                return Err(invalid);
            }
        }
    } else {
        None
    };
    #[cfg(windows)]
    if verify_update_file(&persisted, &command.record).is_err() {
        drop(_installer_lock);
        let _ = std::fs::remove_file(&persisted);
        return Err(invalid);
    }
    let mut process = Command::new(&command.executable);
    process
        .args(&command.arguments)
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null());
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        process.creation_flags(0x0000_0008 | 0x0000_0200);
    }
    #[cfg(unix)]
    {
        use std::os::unix::process::CommandExt;
        process.process_group(0);
    }
    match process.spawn() {
        Ok(child) => Ok(child.id()),
        Err(_) => {
            #[cfg(windows)]
            drop(_installer_lock);
            let _ = std::fs::remove_file(persisted);
            Err(InstallError::LaunchFailed)
        }
    }
}

#[cfg(target_os = "linux")]
impl LinuxHelperRequest {
    pub fn target(&self) -> &Path {
        &self.target
    }
    pub fn staged(&self) -> &Path {
        &self.staged
    }
    pub fn record(&self) -> &UpdateRecord {
        &self.record
    }
    pub fn envelope(&self) -> &[u8] {
        &self.envelope
    }
    pub fn parent_pid(&self) -> i32 {
        self.parent_pid
    }
}

#[cfg(target_os = "linux")]
#[allow(clippy::too_many_arguments)]
pub fn prepare_linux_helper(
    envelope: &[u8],
    public_key: &[u8],
    current_version: &str,
    repository: &str,
    appimage: &Path,
    invoked: &Path,
    staged: &Path,
    parent_pid: u32,
) -> Result<LinuxHelperRequest, InstallError> {
    let record = parse_signed_update_metadata(
        envelope,
        public_key,
        current_version,
        "linux",
        "x86_64",
        repository,
    )
    .map_err(|_| InstallError::AuthenticationFailed)?
    .ok_or(InstallError::AuthenticationFailed)?;
    StagingLocation::for_linux_appimage(appimage, invoked)
        .map_err(|_| InstallError::UnsafePackage)?;
    let target = appimage
        .canonicalize()
        .map_err(|_| InstallError::UnsafePackage)?;
    if !linux_stage_valid(&target, staged, &record, parent_pid) {
        return Err(InstallError::UnsafePackage);
    }
    let target_identity = FileIdentity::at(&target).ok_or(InstallError::UnsafePackage)?;
    let staged_identity = FileIdentity::at(staged).ok_or(InstallError::UnsafePackage)?;
    Ok(LinuxHelperRequest {
        target,
        staged: staged.to_path_buf(),
        target_identity,
        staged_identity,
        record,
        envelope: envelope.to_vec(),
        parent_pid: parent_pid as i32,
    })
}

#[cfg(target_os = "linux")]
fn linux_stage_valid(target: &Path, staged: &Path, record: &UpdateRecord, parent_pid: u32) -> bool {
    let Ok(metadata) = std::fs::symlink_metadata(staged) else {
        return false;
    };
    let Some(name) = staged.file_name().and_then(|name| name.to_str()) else {
        return false;
    };
    parent_pid > 1
        && parent_pid <= i32::MAX as u32
        && staged.is_absolute()
        && metadata.is_file()
        && !metadata.file_type().is_symlink()
        && staged.parent() == target.parent()
        && name.starts_with(".ChoscorDB-update-")
        && verify_update_file(staged, record).is_ok()
}

#[cfg(all(test, windows))]
mod windows_tests {
    use super::*;
    use base64::Engine as _;
    use ed25519_dalek::{Signer as _, SigningKey};
    use serde_json::json;
    use sha2::{Digest, Sha256};
    use std::{fs, io::Write as _, thread, time::Duration};

    #[test]
    fn real_windows_executable_survives_keep_lock_verify_and_spawn() {
        let package = fs::read(std::env::current_exe().unwrap()).unwrap();
        let key = SigningKey::from_bytes(&[11u8; 32]);
        let digest = Sha256::digest(&package)
            .iter()
            .map(|byte| format!("{byte:02x}"))
            .collect::<String>();
        let payload = serde_json::to_vec(&json!({
            "version": "1.2.4", "platform": "windows", "arch": "x64",
            "url": "https://github.com/choscor/choscordb/releases/download/v1.2.4/ChoscorDB-1.2.4-windows-x64-setup.exe",
            "size": package.len(), "sha256": digest, "notes": "Stable improvements"
        })).unwrap();
        let envelope = serde_json::to_vec(&json!({
            "key_id": "windows-linux-v1",
            "payload": base64::engine::general_purpose::STANDARD.encode(&payload),
            "signature": base64::engine::general_purpose::STANDARD.encode(key.sign(&payload).to_bytes()),
        })).unwrap();
        let update = CheckedUpdate::authenticate(
            &envelope,
            &key.verifying_key().to_bytes(),
            "1.2.3",
            "windows",
            "x64",
            "choscor/choscordb",
        )
        .unwrap()
        .unwrap();
        let mut stage = tempfile::Builder::new()
            .prefix("ChoscorDB-update-")
            .suffix(".exe")
            .tempfile_in(std::env::temp_dir())
            .unwrap();
        stage.as_file_mut().write_all(&package).unwrap();
        let stage_path = stage.path().to_path_buf();
        let command = prepare_windows_install(&update, &stage_path, 4242).unwrap();
        let result =
            launch_prepared_install(command, StagedUpdate::fixture(stage.into_temp_path()));
        assert!(result.is_ok(), "{result:?}");
        assert!(stage_path.exists());
        let mut removed = false;
        for _ in 0..100 {
            if fs::remove_file(&stage_path).is_ok() {
                removed = true;
                break;
            }
            thread::sleep(Duration::from_millis(50));
        }
        assert!(
            removed,
            "launched executable did not release its staged path"
        );
    }
}

#[cfg(all(test, target_os = "linux"))]
mod tests {
    use super::*;
    use ed25519_dalek::{Signer as _, SigningKey};
    use serde_json::json;
    use sha2::{Digest, Sha256};
    use std::{fs, os::unix::fs::PermissionsExt};

    fn checked_update(package: &[u8]) -> CheckedUpdate {
        let key = SigningKey::from_bytes(&[11u8; 32]);
        let digest = Sha256::digest(package)
            .iter()
            .map(|byte| format!("{byte:02x}"))
            .collect::<String>();
        let payload = serde_json::to_vec(&json!({
            "version": "1.2.4", "platform": "linux", "arch": "x86_64",
            "url": "https://github.com/choscor/choscordb/releases/download/v1.2.4/ChoscorDB-1.2.4-linux-x86_64.AppImage",
            "size": package.len(), "sha256": digest, "notes": "Stable improvements"
        })).unwrap();
        let envelope = serde_json::to_vec(&json!({
            "key_id": "windows-linux-v1",
            "payload": base64::engine::general_purpose::STANDARD.encode(&payload),
            "signature": base64::engine::general_purpose::STANDARD.encode(key.sign(&payload).to_bytes()),
        })).unwrap();
        CheckedUpdate::authenticate(
            &envelope,
            &key.verifying_key().to_bytes(),
            "1.2.3",
            "linux",
            "x86_64",
            "choscor/choscordb",
        )
        .unwrap()
        .unwrap()
    }

    fn checked_windows_update(package: &[u8]) -> CheckedUpdate {
        let key = SigningKey::from_bytes(&[11u8; 32]);
        let digest = Sha256::digest(package)
            .iter()
            .map(|byte| format!("{byte:02x}"))
            .collect::<String>();
        let payload = serde_json::to_vec(&json!({
            "version": "1.2.4", "platform": "windows", "arch": "x64",
            "url": "https://github.com/choscor/choscordb/releases/download/v1.2.4/ChoscorDB-1.2.4-windows-x64-setup.exe",
            "size": package.len(), "sha256": digest, "notes": "Stable improvements"
        })).unwrap();
        let envelope = serde_json::to_vec(&json!({
            "key_id": "windows-linux-v1",
            "payload": base64::engine::general_purpose::STANDARD.encode(&payload),
            "signature": base64::engine::general_purpose::STANDARD.encode(key.sign(&payload).to_bytes()),
        })).unwrap();
        CheckedUpdate::authenticate(
            &envelope,
            &key.verifying_key().to_bytes(),
            "1.2.3",
            "windows",
            "x64",
            "choscor/choscordb",
        )
        .unwrap()
        .unwrap()
    }

    #[test]
    fn windows_launch_hands_off_silent_waitpid_and_keeps_installer() {
        use std::{thread, time::Duration};
        let directory = tempfile::tempdir().unwrap();
        let output = directory.path().join("args.txt");
        let script = format!(
            "#!/bin/sh\nprintf '%s\\n' \"$@\" > '{}'\n",
            output.display()
        );
        let stage = tempfile::Builder::new()
            .prefix("ChoscorDB-update-")
            .suffix(".exe")
            .tempfile_in(std::env::temp_dir())
            .unwrap();
        fs::write(stage.path(), script.as_bytes()).unwrap();
        fs::set_permissions(stage.path(), fs::Permissions::from_mode(0o755)).unwrap();
        let stage_path = stage.path().to_path_buf();
        let command = prepare_windows_install(
            &checked_windows_update(script.as_bytes()),
            &stage_path,
            4242,
        )
        .unwrap();
        let pid = launch_prepared_install(command, StagedUpdate::fixture(stage.into_temp_path()))
            .unwrap();
        assert!(pid > 1);
        for _ in 0..100 {
            if output.exists() {
                break;
            }
            thread::sleep(Duration::from_millis(10));
        }
        assert_eq!(fs::read_to_string(output).unwrap(), "/S\n/WAITPID=4242\n");
        assert!(stage_path.exists());
        fs::remove_file(stage_path).unwrap();
    }

    #[test]
    fn launch_keeps_verified_stage_for_detached_linux_helper() {
        let directory = tempfile::tempdir().unwrap();
        let directory = directory.path().canonicalize().unwrap();
        let target = directory.join("ChoscorDB.AppImage");
        fs::write(&target, b"#!/bin/sh\nexit 0\n").unwrap();
        fs::set_permissions(&target, fs::Permissions::from_mode(0o755)).unwrap();
        let stage = tempfile::Builder::new()
            .prefix(".ChoscorDB-update-")
            .suffix(".AppImage")
            .tempfile_in(&directory)
            .unwrap();
        fs::write(stage.path(), b"abcd").unwrap();
        let stage_path = stage.path().to_path_buf();
        let command = prepare_linux_install(
            &checked_update(b"abcd"),
            &stage_path,
            &target,
            &target,
            4242,
        )
        .unwrap();
        let pid = launch_prepared_install(command, StagedUpdate::fixture(stage.into_temp_path()))
            .unwrap();
        assert!(pid > 1);
        assert_eq!(fs::read(&stage_path).unwrap(), b"abcd");
        fs::remove_file(stage_path).unwrap();
    }

    #[test]
    fn failed_detached_launch_removes_staged_package() {
        let directory = tempfile::tempdir().unwrap();
        let directory = directory.path().canonicalize().unwrap();
        let target = directory.join("ChoscorDB.AppImage");
        fs::write(&target, b"#!/no/such/choscordb-interpreter\n").unwrap();
        fs::set_permissions(&target, fs::Permissions::from_mode(0o755)).unwrap();
        let stage = tempfile::Builder::new()
            .prefix(".ChoscorDB-update-")
            .suffix(".AppImage")
            .tempfile_in(&directory)
            .unwrap();
        fs::write(stage.path(), b"abcd").unwrap();
        let stage_path = stage.path().to_path_buf();
        let command = prepare_linux_install(
            &checked_update(b"abcd"),
            &stage_path,
            &target,
            &target,
            4242,
        )
        .unwrap();
        let result =
            launch_prepared_install(command, StagedUpdate::fixture(stage.into_temp_path()));
        assert!(
            matches!(result, Err(InstallError::LaunchFailed)),
            "{result:?}"
        );
        assert!(!stage_path.exists());
        assert_eq!(
            fs::read(target).unwrap(),
            b"#!/no/such/choscordb-interpreter\n"
        );
    }

    #[test]
    fn linux_launch_rejects_target_replaced_after_plan() {
        let directory = tempfile::tempdir().unwrap();
        let directory = directory.path().canonicalize().unwrap();
        let target = directory.join("ChoscorDB.AppImage");
        fs::write(&target, b"#!/bin/sh\nexit 0\n").unwrap();
        fs::set_permissions(&target, fs::Permissions::from_mode(0o755)).unwrap();
        let stage = tempfile::Builder::new()
            .prefix(".ChoscorDB-update-")
            .suffix(".AppImage")
            .tempfile_in(&directory)
            .unwrap();
        fs::write(stage.path(), b"abcd").unwrap();
        let stage_path = stage.path().to_path_buf();
        let command = prepare_linux_install(
            &checked_update(b"abcd"),
            &stage_path,
            &target,
            &target,
            4242,
        )
        .unwrap();
        let replacement = directory.join("replacement.AppImage");
        fs::write(&replacement, b"#!/bin/sh\nexit 0\n").unwrap();
        fs::set_permissions(&replacement, fs::Permissions::from_mode(0o755)).unwrap();
        fs::rename(replacement, &target).unwrap();
        assert!(matches!(
            launch_prepared_install(command, StagedUpdate::fixture(stage.into_temp_path())),
            Err(InstallError::UnsafePackage)
        ));
        assert!(!stage_path.exists());
    }
}
