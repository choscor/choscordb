use base64::Engine as _;
#[cfg(target_os = "linux")]
use choscordb_update::run_linux_helper;
use choscordb_update::{CheckedUpdate, InstallError, prepare_windows_install};
#[cfg(target_os = "linux")]
use choscordb_update::{prepare_linux_helper, prepare_linux_install};
use ed25519_dalek::{Signer as _, SigningKey};
use serde_json::json;
use std::fs;
#[cfg(target_os = "linux")]
use std::path::Path;

#[cfg(target_os = "linux")]
fn signed_envelope(package: &[u8]) -> (Vec<u8>, [u8; 32]) {
    use sha2::{Digest, Sha256};
    let signing = SigningKey::from_bytes(&[9u8; 32]);
    let digest = Sha256::digest(package);
    let digest_hex = digest
        .iter()
        .map(|byte| format!("{byte:02x}"))
        .collect::<String>();
    let payload = serde_json::to_vec(&json!({
        "version": "1.2.4", "platform": "linux", "arch": "x86_64",
        "url": "https://github.com/choscor/choscordb/releases/download/v1.2.4/ChoscorDB-1.2.4-linux-x86_64.AppImage",
        "size": package.len(), "sha256": digest_hex, "notes": "Stable improvements"
    })).unwrap();
    let envelope = serde_json::to_vec(&json!({
        "key_id": "windows-linux-v1",
        "payload": base64::engine::general_purpose::STANDARD.encode(&payload),
        "signature": base64::engine::general_purpose::STANDARD.encode(signing.sign(&payload).to_bytes())
    })).unwrap();
    (envelope, signing.verifying_key().to_bytes())
}

fn signed_windows_envelope(package: &[u8]) -> (Vec<u8>, [u8; 32]) {
    use sha2::{Digest, Sha256};
    let signing = SigningKey::from_bytes(&[9u8; 32]);
    let digest = Sha256::digest(package);
    let digest_hex = digest
        .iter()
        .map(|byte| format!("{byte:02x}"))
        .collect::<String>();
    let payload = serde_json::to_vec(&json!({
        "version": "1.2.4", "platform": "windows", "arch": "x64",
        "url": "https://github.com/choscor/choscordb/releases/download/v1.2.4/ChoscorDB-1.2.4-windows-x64-setup.exe",
        "size": package.len(), "sha256": digest_hex, "notes": "Stable improvements"
    })).unwrap();
    let envelope = serde_json::to_vec(&json!({
        "key_id": "windows-linux-v1",
        "payload": base64::engine::general_purpose::STANDARD.encode(&payload),
        "signature": base64::engine::general_purpose::STANDARD.encode(signing.sign(&payload).to_bytes())
    })).unwrap();
    (envelope, signing.verifying_key().to_bytes())
}

#[cfg(target_os = "linux")]
fn executable_appimage(path: &Path) {
    fs::write(path, b"old appimage").unwrap();
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        fs::set_permissions(path, fs::Permissions::from_mode(0o755)).unwrap();
    }
}

#[cfg(target_os = "linux")]
#[test]
fn helper_accepts_only_signed_adjacent_regular_stage_for_direct_appimage() {
    let directory = tempfile::tempdir().unwrap();
    let directory = directory.path().canonicalize().unwrap();
    let target = directory.join("ChoscorDB.AppImage");
    executable_appimage(&target);
    let staged = directory.join(".ChoscorDB-update-ABC123.AppImage");
    fs::write(&staged, b"abcd").unwrap();
    let (envelope, key) = signed_envelope(b"abcd");
    let request = prepare_linux_helper(
        &envelope,
        &key,
        "1.2.3",
        "choscor/choscordb",
        &target,
        &target,
        &staged,
        42,
    )
    .unwrap();
    assert_eq!(request.target(), target);
    assert_eq!(request.staged(), staged);
    assert_eq!(request.parent_pid(), 42);
    assert_eq!(request.record().version, "1.2.4");

    fs::write(&staged, b"abce").unwrap();
    assert!(matches!(
        prepare_linux_helper(
            &envelope,
            &key,
            "1.2.3",
            "choscor/choscordb",
            &target,
            &target,
            &staged,
            42
        ),
        Err(InstallError::UnsafePackage)
    ));
    fs::write(&staged, b"abcd").unwrap();
    assert!(matches!(
        prepare_linux_helper(
            &envelope,
            &key,
            "1.2.3",
            "choscor/choscordb",
            &target,
            &target,
            &staged,
            1
        ),
        Err(InstallError::UnsafePackage)
    ));
    assert!(matches!(
        prepare_linux_helper(
            &envelope,
            &key,
            "1.2.4",
            "choscor/choscordb",
            &target,
            &target,
            &staged,
            42
        ),
        Err(InstallError::AuthenticationFailed)
    ));
    assert!(matches!(
        prepare_linux_helper(
            &envelope,
            &key,
            "1.2.3",
            "choscor/choscordb",
            &target,
            Path::new("different.AppImage"),
            &staged,
            42
        ),
        Err(InstallError::UnsafePackage)
    ));
    let elsewhere = tempfile::tempdir().unwrap();
    let other_stage = elsewhere.path().join(".ChoscorDB-update-ABC123.AppImage");
    fs::write(&other_stage, b"abcd").unwrap();
    assert!(matches!(
        prepare_linux_helper(
            &envelope,
            &key,
            "1.2.3",
            "choscor/choscordb",
            &target,
            &target,
            &other_stage,
            42
        ),
        Err(InstallError::UnsafePackage)
    ));
    use std::os::unix::fs::symlink;
    fs::remove_file(&staged).unwrap();
    symlink(&other_stage, &staged).unwrap();
    assert!(matches!(
        prepare_linux_helper(
            &envelope,
            &key,
            "1.2.3",
            "choscor/choscordb",
            &target,
            &target,
            &staged,
            42
        ),
        Err(InstallError::UnsafePackage)
    ));
}

#[test]
fn windows_plan_rechecks_verified_stage_and_passes_silent_waitpid_arguments() {
    let (envelope, key) = signed_windows_envelope(b"abcd");
    let update = CheckedUpdate::authenticate(
        &envelope,
        &key,
        "1.2.3",
        "windows",
        "x64",
        "choscor/choscordb",
    )
    .unwrap()
    .unwrap();
    let stage = tempfile::Builder::new()
        .prefix("ChoscorDB-update-")
        .suffix(".exe")
        .tempfile_in(std::env::temp_dir())
        .unwrap();
    fs::write(stage.path(), b"abcd").unwrap();
    let command = prepare_windows_install(&update, stage.path(), 4242).unwrap();
    assert_eq!(command.executable(), stage.path());
    assert_eq!(command.arguments(), ["/S", "/WAITPID=4242"]);
    fs::write(stage.path(), b"abce").unwrap();
    assert!(matches!(
        prepare_windows_install(&update, stage.path(), 4242),
        Err(InstallError::WindowsPackageInvalid)
    ));
}

#[cfg(target_os = "linux")]
#[test]
fn linux_plan_rechecks_stage_and_passes_authenticated_helper_arguments() {
    let directory = tempfile::tempdir().unwrap();
    let directory = directory.path().canonicalize().unwrap();
    let target = directory.join("ChoscorDB.AppImage");
    executable_appimage(&target);
    let staged = directory.join(".ChoscorDB-update-ABC123.AppImage");
    fs::write(&staged, b"abcd").unwrap();
    let (envelope, key) = signed_envelope(b"abcd");
    let update = CheckedUpdate::authenticate(
        &envelope,
        &key,
        "1.2.3",
        "linux",
        "x86_64",
        "choscor/choscordb",
    )
    .unwrap()
    .unwrap();
    let command = prepare_linux_install(&update, &staged, &target, &target, 4242).unwrap();
    assert_eq!(command.executable(), target);
    assert_eq!(command.arguments()[0], "--apply-update");
    assert_eq!(command.arguments()[1], staged.to_str().unwrap());
    assert_eq!(
        base64::engine::general_purpose::STANDARD
            .decode(&command.arguments()[2])
            .unwrap(),
        envelope
    );
    assert_eq!(command.arguments()[3], "4242");
    fs::write(&staged, b"abce").unwrap();
    assert!(matches!(
        prepare_linux_install(&update, &staged, &target, &target, 4242),
        Err(InstallError::UnsafePackage)
    ));
}

#[cfg(target_os = "linux")]
#[test]
fn linux_helper_exchanges_verified_image_and_confirms_new_process_startup() {
    use std::os::unix::fs::PermissionsExt;
    use std::process::Command;

    let directory = tempfile::tempdir().unwrap();
    let directory = directory.path().canonicalize().unwrap();
    let target = directory.join("ChoscorDB.AppImage");
    let old = b"#!/bin/sh\n# old\nexit 0\n";
    fs::write(&target, old).unwrap();
    fs::set_permissions(&target, fs::Permissions::from_mode(0o755)).unwrap();
    let new = b"#!/bin/sh\nprintf 'ready\\n' > \"$CHOSCORDB_UPDATE_READY_FILE\"\nsleep 4\n";
    let staged = directory.join(".ChoscorDB-update-ABC123.AppImage");
    fs::write(&staged, new).unwrap();
    let (envelope, key) = signed_envelope(new);
    let mut parent = Command::new("sh")
        .args(["-c", "sleep 0.2"])
        .spawn()
        .unwrap();
    let request = prepare_linux_helper(
        &envelope,
        &key,
        "1.2.3",
        "choscor/choscordb",
        &target,
        &target,
        &staged,
        parent.id(),
    )
    .unwrap();
    let child_pid = run_linux_helper(&request).unwrap();
    assert!(child_pid > 1);
    assert_eq!(fs::read(&target).unwrap(), new);
    assert!(!staged.exists());
    parent.wait().unwrap();
}

#[cfg(target_os = "linux")]
#[test]
fn linux_helper_restores_previous_image_when_new_process_exits_before_readiness() {
    use std::os::unix::fs::PermissionsExt;
    use std::process::Command;

    let directory = tempfile::tempdir().unwrap();
    let directory = directory.path().canonicalize().unwrap();
    let target = directory.join("ChoscorDB.AppImage");
    let old = b"#!/bin/sh\nexit 0\n";
    fs::write(&target, old).unwrap();
    fs::set_permissions(&target, fs::Permissions::from_mode(0o755)).unwrap();
    let new = b"#!/bin/sh\n# new\nexit 0\n";
    let staged = directory.join(".ChoscorDB-update-ABC123.AppImage");
    fs::write(&staged, new).unwrap();
    let (envelope, key) = signed_envelope(new);
    let mut parent = Command::new("sh")
        .args(["-c", "sleep 0.2"])
        .spawn()
        .unwrap();
    let request = prepare_linux_helper(
        &envelope,
        &key,
        "1.2.3",
        "choscor/choscordb",
        &target,
        &target,
        &staged,
        parent.id(),
    )
    .unwrap();
    assert!(matches!(
        run_linux_helper(&request),
        Err(InstallError::PreviousRestored)
    ));
    assert_eq!(fs::read(&target).unwrap(), old);
    assert!(!staged.exists());
    parent.wait().unwrap();
}

#[cfg(target_os = "linux")]
#[test]
fn linux_helper_rejects_target_replaced_while_parent_is_still_running() {
    use std::{os::unix::fs::PermissionsExt, process::Command, thread, time::Duration};
    let directory = tempfile::tempdir().unwrap();
    let directory = directory.path().canonicalize().unwrap();
    let target = directory.join("ChoscorDB.AppImage");
    executable_appimage(&target);
    let stage = directory.join(".ChoscorDB-update-ABC123.AppImage");
    fs::write(&stage, b"new appimage").unwrap();
    let (envelope, key) = signed_envelope(b"new appimage");
    let mut parent = Command::new("sleep").arg("0.5").spawn().unwrap();
    let request = prepare_linux_helper(
        &envelope,
        &key,
        "1.2.3",
        "choscor/choscordb",
        &target,
        &target,
        &stage,
        parent.id(),
    )
    .unwrap();
    let replacement = directory.join("replacement.AppImage");
    fs::write(&replacement, b"other appimage").unwrap();
    fs::set_permissions(&replacement, fs::Permissions::from_mode(0o755)).unwrap();
    let target_for_swap = target.clone();
    let swap = thread::spawn(move || {
        thread::sleep(Duration::from_millis(100));
        fs::rename(replacement, target_for_swap).unwrap();
    });
    assert!(matches!(
        run_linux_helper(&request),
        Err(InstallError::ReplacementFailed)
    ));
    swap.join().unwrap();
    parent.wait().unwrap();
    assert_eq!(fs::read(target).unwrap(), b"other appimage");
    assert_eq!(fs::read(stage).unwrap(), b"new appimage");
}

#[cfg(target_os = "linux")]
#[test]
fn linux_helper_rejects_missing_stage_without_replacing_current_image() {
    use std::process::Command;
    let directory = tempfile::tempdir().unwrap();
    let directory = directory.path().canonicalize().unwrap();
    let target = directory.join("ChoscorDB.AppImage");
    executable_appimage(&target);
    let stage = directory.join(".ChoscorDB-update-ABC123.AppImage");
    fs::write(&stage, b"new appimage").unwrap();
    let (envelope, key) = signed_envelope(b"new appimage");
    let mut parent = Command::new("sleep").arg("0.2").spawn().unwrap();
    let request = prepare_linux_helper(
        &envelope,
        &key,
        "1.2.3",
        "choscor/choscordb",
        &target,
        &target,
        &stage,
        parent.id(),
    )
    .unwrap();
    fs::remove_file(&stage).unwrap();
    assert!(matches!(
        run_linux_helper(&request),
        Err(InstallError::ReplacementFailed)
    ));
    parent.wait().unwrap();
    assert_eq!(fs::read(target).unwrap(), b"old appimage");
}

#[cfg(target_os = "linux")]
#[test]
fn linux_helper_restores_previous_image_when_child_never_signals_ready() {
    use std::process::Command;
    let directory = tempfile::tempdir().unwrap();
    let directory = directory.path().canonicalize().unwrap();
    let target = directory.join("ChoscorDB.AppImage");
    executable_appimage(&target);
    let new = b"#!/bin/sh\nsleep 1\n";
    let stage = directory.join(".ChoscorDB-update-ABC123.AppImage");
    fs::write(&stage, new).unwrap();
    let (envelope, key) = signed_envelope(new);
    let mut parent = Command::new("sleep").arg("0.2").spawn().unwrap();
    let request = prepare_linux_helper(
        &envelope,
        &key,
        "1.2.3",
        "choscor/choscordb",
        &target,
        &target,
        &stage,
        parent.id(),
    )
    .unwrap();
    assert!(matches!(
        run_linux_helper(&request),
        Err(InstallError::PreviousRestored)
    ));
    parent.wait().unwrap();
    assert_eq!(fs::read(target).unwrap(), b"old appimage");
    assert!(!stage.exists());
}

#[cfg(target_os = "linux")]
#[test]
fn linux_helper_reports_manual_restore_if_previous_image_disappears() {
    use std::process::Command;
    let directory = tempfile::tempdir().unwrap();
    let directory = directory.path().canonicalize().unwrap();
    let target = directory.join("ChoscorDB.AppImage");
    executable_appimage(&target);
    let stage = directory.join(".ChoscorDB-update-ABC123.AppImage");
    let new = format!("#!/bin/sh\nrm -f '{}'\nsleep 0.3\n", stage.display());
    fs::write(&stage, new.as_bytes()).unwrap();
    let (envelope, key) = signed_envelope(new.as_bytes());
    let mut parent = Command::new("sleep").arg("0.2").spawn().unwrap();
    let request = prepare_linux_helper(
        &envelope,
        &key,
        "1.2.3",
        "choscor/choscordb",
        &target,
        &target,
        &stage,
        parent.id(),
    )
    .unwrap();
    assert!(matches!(
        run_linux_helper(&request),
        Err(InstallError::ManualRestoreNeeded)
    ));
    parent.wait().unwrap();
    assert_eq!(fs::read(target).unwrap(), new.as_bytes());
    assert!(!stage.exists());
}
