use choscordb_driver_api::*;
use std::io::Write;
fn options(host: &str, database: &str, user: &str) -> ConnectionOptions {
    ConnectionOptions::Postgres {
        ssh_jump_secrets: Default::default(),
        ssh_private_key: None,
        ssh_jump_private_keys: Default::default(),
        proxy: None,
        proxy_secret: None,
        host: host.into(),
        port: 5432,
        database: database.into(),
        user: user.into(),
        password: None,
        ssh_secret: None,
        tls: TlsMode::Disable,
        root_certificate: None,
        tls_identity: None,
        ssh: None,
    }
}
#[tokio::test]
async fn passfile_first_match_escapes_and_default_database_fill_missing_user() {
    let mut file = tempfile::NamedTempFile::new().unwrap();
    writeln!(file,"# ignored\nwrong:5432:*:wrong:no\nlocalhost:5432:alice:alice:p\\:ass\\\\word\n*:*:*:*:later").unwrap();
    let auth = DatabaseAuthentication::PgPass {
        hostname: None,
        path: Some(file.path().to_str().unwrap().into()),
    };
    let mut settings = options("/tmp/pg-socket", "", "");
    resolve_database_authentication(&auth, &mut settings)
        .await
        .unwrap();
    let ConnectionOptions::Postgres { user, password, .. } = settings else {
        panic!("PG")
    };
    assert_eq!(user, "alice");
    assert_eq!(password.unwrap().expose(), "p:ass\\word");
}

#[cfg(unix)]
#[tokio::test]
async fn password_command_uses_working_directory_and_first_trimmed_line() {
    let directory = tempfile::tempdir().unwrap();
    std::fs::write(
        directory.path().join("credential"),
        "  first-secret\r\nsecond-secret\n",
    )
    .unwrap();
    let auth = DatabaseAuthentication::Command {
        command: "cat credential".into(),
        working_directory: Some(directory.path().to_str().unwrap().into()),
        timeout_seconds: 2,
    };
    let mut settings = options("localhost", "db", "alice");
    resolve_database_authentication(&auth, &mut settings)
        .await
        .unwrap();
    let ConnectionOptions::Postgres { password, .. } = settings else {
        panic!("PG")
    };
    assert_eq!(password.unwrap().expose(), "first-secret");
    assert!(!format!("{auth:?}").contains("cat credential"));
}

#[tokio::test]
async fn passfile_matches_original_ipv6_endpoint_and_explicit_empty_override() {
    let mut file = tempfile::NamedTempFile::new().unwrap();
    writeln!(
        file,
        "bad line\n\\:\\:1:5432:db:wrong:wrong\n\\:\\:1:5432:db:alice:ipv6-secret\n*:*:*:*:fallback"
    )
    .unwrap();
    let auth = DatabaseAuthentication::PgPass {
        hostname: None,
        path: Some(file.path().to_str().unwrap().into()),
    };
    let mut settings = options("[::1]", "db", "alice");
    resolve_database_authentication(&auth, &mut settings)
        .await
        .unwrap();
    let ConnectionOptions::Postgres { password, .. } = &mut settings else {
        panic!("PG")
    };
    assert_eq!(password.as_ref().unwrap().expose(), "ipv6-secret");
    *password = Some(Secret::new(""));
    drop(file);
    resolve_database_authentication(&auth, &mut settings)
        .await
        .unwrap();
    let ConnectionOptions::Postgres { password, .. } = settings else {
        panic!("PG")
    };
    assert_eq!(password.unwrap().expose(), "");
}

#[cfg(unix)]
#[tokio::test]
async fn unsafe_passfile_permissions_and_oversized_files_fail_without_credentials_in_errors() {
    use std::os::unix::fs::PermissionsExt;
    let mut file = tempfile::NamedTempFile::new().unwrap();
    writeln!(file, "*:*:*:*:hidden-secret").unwrap();
    std::fs::set_permissions(file.path(), std::fs::Permissions::from_mode(0o644)).unwrap();
    let auth = DatabaseAuthentication::PgPass {
        hostname: None,
        path: Some(file.path().to_str().unwrap().into()),
    };
    let error = resolve_database_authentication(&auth, &mut options("localhost", "db", "alice"))
        .await
        .unwrap_err();
    assert!(error.message.contains("permissions"));
    assert!(!error.message.contains("hidden-secret"));
    std::fs::set_permissions(file.path(), std::fs::Permissions::from_mode(0o600)).unwrap();
    file.as_file().set_len(1024 * 1024 + 1).unwrap();
    assert!(
        resolve_database_authentication(&auth, &mut options("localhost", "db", "alice"))
            .await
            .unwrap_err()
            .message
            .contains("1 MiB")
    );
}

#[cfg(unix)]
#[tokio::test]
async fn password_command_failure_modes_are_bounded_and_redacted() {
    for (command, kind) in [
        (
            "printf 'private-error' >&2; exit 7",
            ErrorKind::Authentication,
        ),
        ("printf '\\n'", ErrorKind::Authentication),
        ("printf '\\377'", ErrorKind::Authentication),
        ("head -c 16385 /dev/zero", ErrorKind::Authentication),
        ("exec sleep 5", ErrorKind::Timeout),
    ] {
        let auth = DatabaseAuthentication::Command {
            command: command.into(),
            working_directory: None,
            timeout_seconds: 1,
        };
        let error = tokio::time::timeout(
            std::time::Duration::from_secs(3),
            resolve_database_authentication(&auth, &mut options("localhost", "db", "alice")),
        )
        .await
        .unwrap()
        .unwrap_err();
        assert_eq!(error.kind, kind, "{command}");
        assert!(!error.message.contains("private-error"));
    }
}

#[cfg(unix)]
#[test]
fn default_passfile_environment_and_wildcard_username() {
    const CHILD: &str = "CHOSCORDB_PGPASS_TEST_CHILD";
    if std::env::var_os(CHILD).is_some() {
        let runtime = tokio::runtime::Builder::new_current_thread()
            .enable_all()
            .build()
            .unwrap();
        runtime.block_on(async {
            let mut settings = options("localhost", "", "");
            resolve_database_authentication(
                &DatabaseAuthentication::PgPass {
                    hostname: None,
                    path: None,
                },
                &mut settings,
            )
            .await
            .unwrap();
            let ConnectionOptions::Postgres { user, password, .. } = settings else {
                panic!("PG")
            };
            assert_eq!(user, std::env::var("CHOSCORDB_EXPECTED_OS_USER").unwrap());
            assert_eq!(password.unwrap().expose(), "wildcard-password");
        });
        return;
    }
    use std::os::unix::fs::PermissionsExt;
    let directory = tempfile::tempdir().unwrap();
    let path = directory.path().join(".pgpass");
    let account = String::from_utf8(
        std::process::Command::new("id")
            .arg("-un")
            .output()
            .unwrap()
            .stdout,
    )
    .unwrap()
    .trim()
    .to_owned();
    std::fs::write(
        &path,
        format!("localhost:5432:{account}:*:wildcard-password\n"),
    )
    .unwrap();
    std::fs::set_permissions(&path, std::fs::Permissions::from_mode(0o600)).unwrap();
    for explicit in [true, false] {
        let mut child = std::process::Command::new(std::env::current_exe().unwrap());
        child
            .args([
                "--exact",
                "default_passfile_environment_and_wildcard_username",
            ])
            .env(CHILD, "1")
            .env("USER", "unrelated-environment-user")
            .env("CHOSCORDB_EXPECTED_OS_USER", &account)
            .env("HOME", directory.path());
        if explicit {
            child.env("PGPASSFILE", &path);
        } else {
            child.env_remove("PGPASSFILE");
        }
        assert!(child.output().unwrap().status.success());
    }
}

#[test]
fn provider_configuration_has_bounded_defaults_and_redacted_debug() {
    let auth: DatabaseAuthentication =
        serde_json::from_str(r#"{"method":"command","command":"private-command-argument"}"#)
            .unwrap();
    assert_eq!(auth.command_timeout(), std::time::Duration::from_secs(10));
    assert!(!format!("{auth:?}").contains("private-command-argument"));
    for value in [
        serde_json::json!({"method":"command","command":"","timeout_seconds":10}),
        serde_json::json!({"method":"command","command":"true","timeout_seconds":0}),
        serde_json::json!({"method":"command","command":"true","timeout_seconds":301}),
        serde_json::json!({"method":"pg_pass","path":""}),
    ] {
        assert!(
            serde_json::from_value::<DatabaseAuthentication>(value)
                .unwrap()
                .validate()
                .is_err()
        );
    }
}

#[cfg(unix)]
#[tokio::test]
async fn password_command_timeout_terminates_background_children() {
    let directory = tempfile::tempdir().unwrap();
    let auth = DatabaseAuthentication::Command {
        command: "(sleep 2; printf leaked > late-output) & echo $! > child-pid; wait".into(),
        working_directory: Some(directory.path().to_str().unwrap().into()),
        timeout_seconds: 1,
    };
    let error = resolve_database_authentication(&auth, &mut options("localhost", "db", "alice"))
        .await
        .unwrap_err();
    assert_eq!(error.kind, ErrorKind::Timeout);
    let pid = std::fs::read_to_string(directory.path().join("child-pid")).unwrap();
    let pid = pid.trim().parse::<u32>().unwrap();
    tokio::time::sleep(std::time::Duration::from_millis(1500)).await;
    assert!(
        !directory.path().join("late-output").exists(),
        "timed-out provider left a descendant running"
    );
    assert!(
        !std::process::Command::new("kill")
            .args(["-0", &pid.to_string()])
            .stderr(std::process::Stdio::null())
            .status()
            .unwrap()
            .success()
    );
}

#[cfg(unix)]
#[tokio::test]
async fn cancelling_password_resolution_terminates_its_process_group() {
    let directory = tempfile::tempdir().unwrap();
    let auth = DatabaseAuthentication::Command {
        command: "(sleep 2; printf leaked > late-output) & echo $! > child-pid; wait".into(),
        working_directory: Some(directory.path().to_str().unwrap().into()),
        timeout_seconds: 30,
    };
    let worker = tokio::spawn(async move {
        resolve_database_authentication(&auth, &mut options("localhost", "db", "alice")).await
    });
    let deadline = tokio::time::Instant::now() + std::time::Duration::from_secs(2);
    while !directory.path().join("child-pid").exists() {
        assert!(tokio::time::Instant::now() < deadline);
        tokio::time::sleep(std::time::Duration::from_millis(5)).await;
    }
    worker.abort();
    assert!(worker.await.unwrap_err().is_cancelled());
    tokio::time::sleep(std::time::Duration::from_millis(2200)).await;
    assert!(!directory.path().join("late-output").exists());
}

#[tokio::test]
async fn passfile_prefers_ssh_target_host_before_local_database_entry() {
    let mut file = tempfile::NamedTempFile::new().unwrap();
    writeln!(
        file,
        "localhost:5432:db:alice:database-entry\nbastion.example:5432:db:alice:ssh-entry"
    )
    .unwrap();
    let auth = DatabaseAuthentication::PgPass {
        hostname: None,
        path: Some(file.path().to_str().unwrap().into()),
    };
    let mut settings = options("localhost", "db", "alice");
    if let ConnectionOptions::Postgres { ssh, .. } = &mut settings {
        *ssh = Some(SshTunnel {
            host: "bastion.example".into(),
            port: 22,
            user: "ssh-user".into(),
            authentication: SshAuthentication::Agent,
            identity_source: Default::default(),
            identity_file: None,
            options: Default::default(),
        });
    }
    resolve_database_authentication(&auth, &mut settings)
        .await
        .unwrap();
    let ConnectionOptions::Postgres { password, .. } = settings else {
        panic!("PG")
    };
    assert_eq!(password.unwrap().expose(), "ssh-entry");
}

#[tokio::test]
async fn passfile_hostname_override_takes_priority_and_falls_back_to_database_host() {
    let mut file = tempfile::NamedTempFile::new().unwrap();
    writeln!(
        file,
        "db.example:5432:db:alice:database-entry\nalias.example:5432:db:alice:alias-entry"
    )
    .unwrap();
    for (hostname, expected) in [
        ("alias.example", "alias-entry"),
        ("not-found.example", "database-entry"),
    ] {
        let auth:DatabaseAuthentication=serde_json::from_value(serde_json::json!({"method":"pg_pass","path":file.path().to_str().unwrap(),"hostname":hostname})).expect("passfile hostname override supported");
        let mut settings = options("db.example", "db", "alice");
        resolve_database_authentication(&auth, &mut settings)
            .await
            .unwrap();
        let ConnectionOptions::Postgres { password, .. } = settings else {
            panic!("PG")
        };
        assert_eq!(password.unwrap().expose(), expected);
    }
}
