use choscordb_bridge::*;

fn draft() -> ffi::ProfileDto {
    ffi::ProfileDto {
        id: "pg".into(),
        name: "TLS profile".into(),
        driver: "postgres".into(),
        host: "localhost".into(),
        port: 5432,
        user: "alice".into(),
        tls: "verify_full".into(),
        tls_client_identity: "/tmp/client.p12".into(),
        ..Default::default()
    }
}

#[test]
fn connection_dialog_rejects_removed_transport_settings() {
    let mut proxy = draft();
    proxy.proxy_options = r#"{"protocol":"socks5","host":"proxy.example","port":1080}"#.into();
    assert!(!validate_connection_profile(proxy).is_empty());

    for options in [
        r#"{"remote_host":"other.example"}"#,
        r#"{"local_port":5433}"#,
        r#"{"connect_timeout_seconds":45}"#,
        r#"{"jump_hosts":[{"host":"jump.example","port":22,"user":"alice"}]}"#,
    ] {
        let mut profile = draft();
        profile.ssh_enabled = true;
        profile.ssh_host = "ssh.example".into();
        profile.ssh_port = 22;
        profile.ssh_user = "alice".into();
        profile.ssh_authentication = "agent".into();
        profile.ssh_options = options.into();
        assert!(
            !validate_connection_profile(profile).is_empty(),
            "{options}"
        );
    }
}

#[test]
fn proxy_validation_rejects_unknown_fields_and_ignores_sqlite_stale_settings() {
    let mut profile = draft();
    profile.proxy_options = r#"{"host":"proxy","port":1080,"password":"secret-marker"}"#.into();
    let error = validate_connection_profile(profile);
    assert!(!error.is_empty());
    assert!(!error.contains("secret-marker"));
    let mut sqlite = draft();
    sqlite.driver = "sqlite".into();
    sqlite.path = ":memory:".into();
    sqlite.proxy_options = "obsolete-invalid-settings".into();
    assert!(validate_connection_profile(sqlite).is_empty());
}

fn event(engine: &mut BridgeEngine) -> ffi::BridgeEvent {
    let deadline = std::time::Instant::now() + std::time::Duration::from_secs(3);
    while std::time::Instant::now() < deadline {
        if let Some(event) = drain_events(engine).into_iter().next() {
            return event;
        }
        std::thread::sleep(std::time::Duration::from_millis(1));
    }
    panic!("missing event")
}
#[test]
fn tls_save_update_reaches_credential_store_without_exposing_password() {
    let mut engine = new_engine();
    let credentials = ffi::ProfileCredentialsDto {
        database_action: "keep".into(),
        ssh_action: "keep".into(),
        tls_action: "replace".into(),
        tls: "private-tls-password".into(),
        ..Default::default()
    };
    let submitted = profile_save_credentials(&mut engine, draft(), credentials, 71);
    assert!(submitted.accepted, "{}", submitted.error);
    let failed = event(&mut engine);
    assert_eq!(failed.kind, "profile_failed");
    assert_eq!(failed.request_token, 71);
    assert!(failed.error.contains("unavailable"));
    assert!(!failed.error.contains("private-tls-password"));
}

#[test]
fn disabled_ssh_ignores_stale_advanced_settings() {
    let mut engine = new_engine();
    let mut profile = draft();
    profile.ssh_options = "old invalid settings".into();
    profile.ssh_authentication = "obsolete".into();
    let submitted = profile_save(&mut engine, profile, 72);
    assert!(submitted.accepted, "{}", submitted.error);
    assert_eq!(event(&mut engine).kind, "profile_saved");
}

#[test]
fn trust_bridge_rejects_target_secrets_and_reports_fingerprint_failure_asynchronously() {
    let mut engine = new_engine();
    let mut profile = draft();
    profile.ssh_enabled = true;
    profile.ssh_host = "ssh.example".into();
    profile.ssh_port = 22;
    profile.ssh_user = "ssh-user".into();
    profile.ssh_authentication = "agent".into();
    let rejected = profile_inspect_ssh_host_keys(
        &mut engine,
        profile,
        ffi::ProfileCredentialsDto {
            ssh: "never-send-target".into(),
            has_ssh: true,
            ..Default::default()
        },
        "target",
        "",
        0,
        92,
    );
    assert!(!rejected.accepted);
    assert!(!rejected.error.contains("never-send-target"));

    let directory = tempfile::tempdir().unwrap();
    let path = directory.path().join("known_hosts");
    let candidate = serde_json::json!({
        "target":{"kind":"target"},"original_host":"ssh.example",
        "hostname":"ssh.example","port":22,"host_key_alias":null,
        "key_type":"ssh-ed25519","public_key":"invalid",
        "sha256":"SHA256:not-a-real-fingerprint"
    })
    .to_string();
    let submitted = approve_ssh_host_key(
        &mut engine,
        &candidate,
        "SHA256:different",
        path.to_str().unwrap(),
        93,
    );
    assert!(submitted.accepted, "{}", submitted.error);
    let failed = event(&mut engine);
    assert_eq!(failed.kind, "ssh_host_key_failed");
    assert_eq!(failed.request_token, 93);
    assert!(!path.exists());
}
#[test]
fn tls_and_ssh_profile_options_survive_save_reload() {
    let mut engine = new_engine();
    let mut profile = draft();
    profile.tls = "verify_ca".into();
    profile.root_certificate = "/tmp/ca.pem".into();
    profile.ssh_enabled = true;
    profile.ssh_host = "[::1]".into();
    profile.ssh_port = 2222;
    profile.ssh_user = "tunnel".into();
    profile.ssh_authentication = "agent".into();
    profile.ssh_options = "{}".into();
    let submitted = profile_save(&mut engine, profile, 73);
    assert!(submitted.accepted, "{}", submitted.error);
    let saved = event(&mut engine).profiles.pop().unwrap();
    assert_eq!(saved.tls, "verify_ca");
    assert_eq!(saved.tls_client_identity, "/tmp/client.p12");
    assert_eq!(saved.root_certificate, "/tmp/ca.pem");
    assert!(saved.database.is_empty());
    assert!(validate_connection_profile(saved).is_empty());
}
#[test]
fn test_and_connect_validate_transient_tls_secret() {
    for connect in [false, true] {
        let mut engine = new_engine();
        let credentials = ffi::ProfileCredentialsDto {
            tls: "x".repeat(16 * 1024 + 1),
            has_tls: true,
            ..Default::default()
        };
        let submitted = if connect {
            profile_connect_credentials(&mut engine, draft(), credentials)
        } else {
            profile_test_credentials(&mut engine, draft(), credentials, 74)
        };
        assert!(!submitted.accepted);
        assert_eq!(submitted.error, "Credential exceeds the supported size");
    }
}
