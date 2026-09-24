#![cfg(unix)]
use choscordb_driver_api::*;
use std::{os::unix::fs::PermissionsExt, process::Command};
fn candidate(host: &str, port: u16) -> SshHostKeyCandidate {
    let directory = tempfile::tempdir().unwrap();
    let path = directory.path().join("key");
    assert!(
        Command::new("ssh-keygen")
            .args(["-q", "-t", "ed25519", "-N", ""])
            .arg("-f")
            .arg(&path)
            .status()
            .unwrap()
            .success()
    );
    let public = std::fs::read_to_string(path.with_extension("pub")).unwrap();
    let fingerprint = Command::new("ssh-keygen")
        .args(["-l", "-E", "sha256", "-f"])
        .arg(path.with_extension("pub"))
        .output()
        .unwrap();
    let fingerprint = String::from_utf8(fingerprint.stdout)
        .unwrap()
        .split_whitespace()
        .nth(1)
        .unwrap()
        .to_owned();
    let mut fields = public.split_whitespace();
    SshHostKeyCandidate {
        target: SshHostKeyTarget::Target,
        original_host: host.into(),
        hostname: host.into(),
        port,
        host_key_alias: None,
        key_type: fields.next().unwrap().into(),
        public_key: fields.next().unwrap().into(),
        sha256: fingerprint,
    }
}
#[tokio::test]
async fn explicit_approval_verifies_fingerprint_and_appends_only_exact_key() {
    let directory = tempfile::tempdir().unwrap();
    let path = directory.path().join("known_hosts");
    let key = candidate("ssh.example", 2222);
    assert!(
        approve_ssh_host_key(&key, "SHA256:wrong", &path)
            .await
            .is_err()
    );
    assert!(
        !path.exists(),
        "rejected approval must not create a trust store"
    );
    approve_ssh_host_key(&key, &key.sha256, &path)
        .await
        .unwrap();
    assert_eq!(
        std::fs::metadata(&path).unwrap().permissions().mode() & 0o777,
        0o600
    );
    let expected = format!("[ssh.example]:2222 {} {}\n", key.key_type, key.public_key);
    assert_eq!(std::fs::read_to_string(&path).unwrap(), expected);
    approve_ssh_host_key(&key, &key.sha256, &path)
        .await
        .unwrap();
    assert_eq!(
        std::fs::read_to_string(&path).unwrap(),
        expected,
        "same approval is idempotent"
    );
    let changed = candidate("ssh.example", 2222);
    assert!(
        approve_ssh_host_key(&changed, &changed.sha256, &path)
            .await
            .is_err()
    );
    assert_eq!(
        std::fs::read_to_string(&path).unwrap(),
        expected,
        "changed host must not be replaced"
    );
}
#[tokio::test]
async fn revoked_wildcard_keys_are_never_overridden() {
    let directory = tempfile::tempdir().unwrap();
    let path = directory.path().join("known_hosts");
    let key = candidate("ssh.example", 22);
    let existing = format!("@revoked *.example {} {}\n", key.key_type, key.public_key);
    std::fs::write(&path, &existing).unwrap();
    assert!(
        approve_ssh_host_key(&key, &key.sha256, &path)
            .await
            .is_err()
    );
    assert_eq!(std::fs::read_to_string(path).unwrap(), existing);
}
#[test]
fn strict_transport_does_not_allow_a_localhost_bypass() {
    let settings: SshTunnel = serde_json::from_value(
        serde_json::json!({"host":"localhost","port":22,"user":"user","authentication":"agent"}),
    )
    .unwrap();
    let mut command = tokio::process::Command::new("ssh");
    configure_ssh_authentication(&mut command, &settings, None).unwrap();
    assert!(
        command
            .as_std()
            .get_args()
            .any(|arg| arg == "NoHostAuthenticationForLocalhost=no")
    );
}
#[tokio::test]
async fn concurrent_approvals_preserve_unrelated_entries_and_alias_scope() {
    let directory = tempfile::tempdir().unwrap();
    let path = directory.path().join("known_hosts");
    let mut first = candidate("one.example", 2222);
    first.host_key_alias = Some("explicit-alias".into());
    let second = candidate("two.example", 22);
    let (a, b) = tokio::join!(
        approve_ssh_host_key(&first, &first.sha256, &path),
        approve_ssh_host_key(&second, &second.sha256, &path)
    );
    a.unwrap();
    b.unwrap();
    let contents = std::fs::read_to_string(&path).unwrap();
    assert!(contents.contains(&format!(
        "explicit-alias {} {}\n",
        first.key_type, first.public_key
    )));
    assert!(contents.contains(&format!(
        "two.example {} {}\n",
        second.key_type, second.public_key
    )));
    assert_eq!(contents.lines().count(), 2);
}
#[tokio::test]
async fn hashed_records_and_symlinks_do_not_bypass_existing_trust() {
    let directory = tempfile::tempdir().unwrap();
    let path = directory.path().join("known_hosts");
    let first = candidate("ssh.example", 22);
    approve_ssh_host_key(&first, &first.sha256, &path)
        .await
        .unwrap();
    assert!(
        Command::new("ssh-keygen")
            .arg("-H")
            .arg("-f")
            .arg(&path)
            .stdout(std::process::Stdio::null())
            .stderr(std::process::Stdio::null())
            .status()
            .unwrap()
            .success()
    );
    let original = std::fs::read(&path).unwrap();
    approve_ssh_host_key(&first, &first.sha256, &path)
        .await
        .unwrap();
    let changed = candidate("ssh.example", 22);
    assert!(
        approve_ssh_host_key(&changed, &changed.sha256, &path)
            .await
            .is_err()
    );
    assert_eq!(std::fs::read(&path).unwrap(), original);
    let link = directory.path().join("link");
    std::os::unix::fs::symlink(&path, &link).unwrap();
    assert!(
        approve_ssh_host_key(&first, &first.sha256, &link)
            .await
            .is_err()
    );
    assert_eq!(std::fs::read(&path).unwrap(), original);
}
#[tokio::test]
async fn inspection_deadline_and_external_cancellation_close_scanner_sockets() {
    use tokio::io::AsyncReadExt;
    for abort in [false, true] {
        let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
        let port = listener.local_addr().unwrap().port();
        let settings:SshTunnel=serde_json::from_value(serde_json::json!({"host":"127.0.0.1","port":port,"user":"unused-destination-user","authentication":"password","options":{"connect_timeout_seconds":1}})).unwrap();
        let task = tokio::spawn(async move {
            inspect_ssh_host_keys(
                &settings,
                SshHostKeyTarget::Target,
                Default::default(),
                Default::default(),
            )
            .await
        });
        let mut sockets = Vec::new();
        for _ in 0..3 {
            sockets.push(
                tokio::time::timeout(std::time::Duration::from_secs(2), listener.accept())
                    .await
                    .unwrap()
                    .unwrap()
                    .0,
            );
        }
        if abort {
            task.abort();
            assert!(task.await.unwrap_err().is_cancelled());
        } else {
            assert_eq!(task.await.unwrap().unwrap_err().kind, ErrorKind::Timeout);
        }
        for mut socket in sockets {
            let mut bytes = Vec::new();
            tokio::time::timeout(
                std::time::Duration::from_secs(2),
                socket.read_to_end(&mut bytes),
            )
            .await
            .expect("scanner retained transport after stop")
            .unwrap();
            assert!(
                !String::from_utf8_lossy(&bytes).contains("unused-destination-user"),
                "host inspection must not authenticate"
            );
        }
    }
}
#[tokio::test]
async fn held_store_lock_times_out_without_altering_trust() {
    let directory = tempfile::tempdir().unwrap();
    let path = directory.path().join("known_hosts");
    std::fs::write(&path, "# existing trust\n").unwrap();
    let file = std::fs::File::open(&path).unwrap();
    fs2::FileExt::lock_exclusive(&file).unwrap();
    let key = candidate("ssh.example", 22);
    let result = tokio::time::timeout(
        std::time::Duration::from_secs(6),
        approve_ssh_host_key(&key, &key.sha256, &path),
    )
    .await
    .expect("approval waited indefinitely for lock");
    assert_eq!(result.unwrap_err().kind, ErrorKind::Timeout);
    assert_eq!(std::fs::read_to_string(path).unwrap(), "# existing trust\n");
}
#[tokio::test]
async fn approval_rejects_record_injection_without_creating_a_store() {
    let directory = tempfile::tempdir().unwrap();
    let path = directory.path().join("known_hosts");
    let mut key = candidate("ssh.example", 22);
    key.host_key_alias = Some("alias\nattacker".into());
    assert!(
        approve_ssh_host_key(&key, &key.sha256, &path)
            .await
            .is_err()
    );
    assert!(!path.exists());
}
#[tokio::test]
async fn approval_requires_a_literal_absolute_normalized_path() {
    let relative = tempfile::tempdir_in(".").unwrap();
    let directory = tempfile::tempdir().unwrap();
    let key = candidate("ssh.example", 22);
    let invalid = [
        relative.path().join("known_hosts"),
        std::path::PathBuf::from("~/.ssh/known_hosts"),
        directory.path().join("%h-hosts"),
        directory.path().join("${USER}-hosts"),
        directory.path().join("quote\"hosts"),
        directory.path().join("back\\hosts"),
        directory.path().join("line\nhosts"),
        directory.path().join("child/../known_hosts"),
        directory.path().join("./known_hosts"),
    ];
    for path in invalid {
        assert!(
            approve_ssh_host_key(&key, &key.sha256, &path)
                .await
                .is_err(),
            "expanded or non-normalized trust path accepted"
        );
        assert!(!path.exists(), "rejected trust path was written");
    }
    let literal = directory.path().join("known hosts");
    assert_eq!(
        approve_ssh_host_key(&key, &key.sha256, &literal)
            .await
            .unwrap(),
        SshHostKeyApproval::Approved
    );
    assert!(literal.is_file());
}
