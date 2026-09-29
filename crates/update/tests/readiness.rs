use choscordb_update::{wait_for_update_readiness, write_update_readiness_file};
use std::{fs, path::PathBuf, thread, time::Duration};

fn unused_marker_path() -> PathBuf {
    let file = tempfile::Builder::new()
        .prefix("ChoscorDB-update-ready-")
        .suffix(".txt")
        .tempfile_in(std::env::temp_dir())
        .unwrap();
    let path = file.path().to_path_buf();
    file.close().unwrap();
    path
}

#[test]
fn marker_is_exclusive_and_constrained_to_system_temp_directory() {
    let path = unused_marker_path();
    assert!(write_update_readiness_file(&path));
    assert_eq!(fs::read(&path).unwrap(), b"ready\n");
    assert!(!write_update_readiness_file(&path));
    fs::remove_file(&path).unwrap();
    assert!(!write_update_readiness_file(
        &std::env::temp_dir().join("unrelated.txt")
    ));

    let other = tempfile::tempdir().unwrap();
    let other_path = other.path().join(path.file_name().unwrap());
    assert!(!write_update_readiness_file(&other_path));
    assert!(!other_path.exists());
}

#[test]
fn stable_exact_marker_confirms_live_process() {
    let path = unused_marker_path();
    assert!(write_update_readiness_file(&path));
    let mut checks = 0;
    assert!(wait_for_update_readiness(
        &path,
        Duration::from_millis(500),
        Duration::from_millis(150),
        || {
            checks += 1;
            true
        }
    ));
    assert!(checks >= 3);
    fs::remove_file(path).unwrap();
}

#[test]
fn process_exit_or_invalid_marker_never_confirms_startup() {
    let path = unused_marker_path();
    assert!(!wait_for_update_readiness(
        &path,
        Duration::from_millis(200),
        Duration::from_millis(50),
        || false
    ));
    fs::write(&path, b"ready\nextra").unwrap();
    assert!(!wait_for_update_readiness(
        &path,
        Duration::from_millis(220),
        Duration::ZERO,
        || true
    ));
    fs::write(&path, b"ready\n").unwrap();
    let mut checks = 0;
    assert!(!wait_for_update_readiness(
        &path,
        Duration::from_millis(400),
        Duration::from_millis(200),
        || {
            checks += 1;
            checks < 3
        }
    ));
    assert_eq!(checks, 3);
    fs::remove_file(path).unwrap();
}

#[test]
fn transient_marker_resets_stability_and_missing_marker_times_out() {
    let path = unused_marker_path();
    fs::write(&path, b"ready\n").unwrap();
    let removed = path.clone();
    let writer = thread::spawn(move || {
        thread::sleep(Duration::from_millis(120));
        fs::remove_file(removed).unwrap();
    });
    assert!(!wait_for_update_readiness(
        &path,
        Duration::from_millis(500),
        Duration::from_millis(350),
        || true
    ));
    writer.join().unwrap();
    assert!(!path.exists());
    assert!(!wait_for_update_readiness(
        &path,
        Duration::from_millis(120),
        Duration::ZERO,
        || true
    ));
}

#[cfg(unix)]
#[test]
fn writer_rejects_symlink_marker_without_touching_target() {
    use std::os::unix::fs::symlink;

    let path = unused_marker_path();
    let target = tempfile::NamedTempFile::new().unwrap();
    fs::write(target.path(), b"unchanged").unwrap();
    symlink(target.path(), &path).unwrap();
    assert!(!write_update_readiness_file(&path));
    assert_eq!(fs::read(target.path()).unwrap(), b"unchanged");
    fs::remove_file(path).unwrap();
}

#[cfg(unix)]
#[test]
fn waiter_does_not_trust_symlinked_readiness_acknowledgment() {
    use std::os::unix::fs::symlink;

    let path = unused_marker_path();
    let target = tempfile::NamedTempFile::new().unwrap();
    fs::write(target.path(), b"ready\n").unwrap();
    symlink(target.path(), &path).unwrap();
    assert!(!wait_for_update_readiness(
        &path,
        Duration::from_millis(150),
        Duration::ZERO,
        || true
    ));
    fs::remove_file(path).unwrap();
}
