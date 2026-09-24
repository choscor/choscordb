use choscordb_driver_api::SshTunnel;

#[test]
fn persisted_advanced_ssh_settings_round_trip() {
    let json = serde_json::json!({
        "host": "bastion.example", "port": 2222, "user": "alice",
        "authentication": "agent", "identity_file": null,
        "options": {"connect_timeout_seconds": 9, "server_alive_interval_seconds": 20,
            "server_alive_count_max": 4, "agent_socket": "/tmp/agent.sock",
            "known_hosts_file": "/tmp/known hosts", "jump_hosts": [
                {"host":"[2001:db8::1]", "port":2200, "user":"jump"}]}
    });
    let settings: SshTunnel =
        serde_json::from_value(json.clone()).expect("advanced settings supported");
    settings.validate().unwrap();
    assert_eq!(serde_json::to_value(settings).unwrap(), json);
}

fn tunnel() -> SshTunnel {
    serde_json::from_value(serde_json::json!({"host":"ssh.example", "port":22,
        "user":"alice", "authentication":"agent"}))
    .unwrap()
}

#[test]
fn advanced_settings_reject_unbounded_or_ambiguous_values() {
    for options in [
        serde_json::json!({"connect_timeout_seconds":0}),
        serde_json::json!({"connect_timeout_seconds":301}),
        serde_json::json!({"server_alive_interval_seconds":86401}),
        serde_json::json!({"server_alive_count_max":0}),
        serde_json::json!({"agent_socket":"/tmp/socket\nmalicious"}),
        serde_json::json!({"known_hosts_file":""}),
        serde_json::json!({"jump_hosts":[{"host":"ssh.example", "port":22,"user":"user,other"}]}),
        serde_json::json!({"jump_hosts":[{"host":"ssh.example", "port":0,"user":"alice"}]}),
        serde_json::json!({"jump_hosts":vec![serde_json::json!({"host":"ssh.example","port":22,"user":"alice"});6]}),
        serde_json::json!({"jump_hosts":[{"host":"ssh.example", "port":22,"user":"$(touch /tmp/pwn)"}]}),
        serde_json::json!({"jump_hosts":[{"host":"ssh.example", "port":22,"user":"alice;id"}]}),
    ] {
        let mut settings = tunnel();
        settings.options = serde_json::from_value(options.clone()).unwrap();
        assert!(settings.validate().is_err(), "accepted invalid {options}");
    }
}

#[test]
fn legacy_profiles_get_transport_defaults() {
    let settings = tunnel();
    assert_eq!(settings.options.connect_timeout_seconds, 15);
    assert_eq!(settings.options.server_alive_interval_seconds, 0);
    assert_eq!(settings.options.server_alive_count_max, 3);
    assert!(settings.options.jump_hosts.is_empty());
    settings.validate().unwrap();
}

#[tokio::test]
async fn shared_command_applies_advanced_options_and_ipv6_jumps() {
    let mut settings = tunnel();
    settings.options = serde_json::from_value(serde_json::json!({
        "connect_timeout_seconds":9, "server_alive_interval_seconds":20,
        "server_alive_count_max":4, "agent_socket":"/tmp/agent socket",
        "known_hosts_file":"/tmp/known hosts", "jump_hosts":[
            {"host":"[2001:db8::1]","port":2200,"user":"jump"},
            {"host":"second.example","port":22,"user":"bob"}]
    }))
    .unwrap();
    let mut command = tokio::process::Command::new("ssh");
    choscordb_driver_api::configure_ssh_authentication(&mut command, &settings, None).unwrap();
    let args: Vec<_> = command
        .as_std()
        .get_args()
        .map(|v| v.to_str().unwrap())
        .collect();
    for expected in [
        "ConnectTimeout=9",
        "ServerAliveInterval=20",
        "ServerAliveCountMax=4",
        "StrictHostKeyChecking=yes",
        "IdentityAgent=\"/tmp/agent socket\"",
        "UserKnownHostsFile=\"/tmp/known hosts\"",
        "jump@[2001:db8::1]:2200,bob@second.example:22",
    ] {
        assert!(args.contains(&expected), "missing {expected}: {args:?}");
    }
}

#[test]
fn jump_sessions_cannot_receive_target_askpass_secrets() {
    use choscordb_driver_api::{Secret, SshAuthentication, SshJumpHost, validate_ssh_secret};
    let mut settings = tunnel();
    settings.options.jump_hosts.push(SshJumpHost {
        id: None,
        authentication: Default::default(),
        identity_source: Default::default(),
        identity_file: None,
        agent_socket: None,
        known_hosts_file: None,
        host: "jump.example".into(),
        port: 22,
        user: "jump".into(),
    });
    let secret = Secret::new("target-only-password");
    settings.authentication = SshAuthentication::Password;
    assert!(validate_ssh_secret(&settings, Some(&secret)).is_err());
    settings.authentication = SshAuthentication::PublicKey;
    settings.identity_file = Some("/tmp/key".into());
    assert!(validate_ssh_secret(&settings, Some(&secret)).is_err());
    assert!(validate_ssh_secret(&settings, None).is_ok());
}

#[test]
fn default_advanced_settings_do_not_change_legacy_json() {
    let value = serde_json::to_value(tunnel()).unwrap();
    assert!(value.get("options").is_none());
}

#[tokio::test]
async fn selected_ssh_deadline_bounds_credential_broker_lifetime() {
    let mut settings = tunnel();
    settings.authentication = choscordb_driver_api::SshAuthentication::Password;
    settings.options.connect_timeout_seconds = 1;
    let mut command = tokio::process::Command::new("ssh");
    let secret = choscordb_driver_api::Secret::new("test-password");
    let broker =
        choscordb_driver_api::configure_ssh_authentication(&mut command, &settings, Some(&secret))
            .unwrap()
            .unwrap();
    let retrieved = choscordb_driver_api::request_ssh_secret(broker.address(), broker.token())
        .await
        .unwrap();
    assert_eq!(retrieved.expose(), "test-password");
    tokio::time::sleep(std::time::Duration::from_secs(7)).await;
    assert!(
        choscordb_driver_api::request_ssh_secret(broker.address(), broker.token())
            .await
            .is_err()
    );
}

#[tokio::test]
async fn longer_ssh_deadline_keeps_credentials_available_after_thirty_seconds() {
    let mut settings = tunnel();
    settings.authentication = choscordb_driver_api::SshAuthentication::Password;
    settings.options.connect_timeout_seconds = 40;
    let mut command = tokio::process::Command::new("ssh");
    let secret = choscordb_driver_api::Secret::new("test-password");
    let broker =
        choscordb_driver_api::configure_ssh_authentication(&mut command, &settings, Some(&secret))
            .unwrap()
            .unwrap();
    tokio::time::sleep(std::time::Duration::from_secs(31)).await;
    let retrieved = choscordb_driver_api::request_ssh_secret(broker.address(), broker.token())
        .await
        .unwrap();
    assert_eq!(retrieved.expose(), "test-password");
}

#[tokio::test]
async fn cleared_passphrase_allows_unencrypted_key_with_jumps_without_askpass() {
    use choscordb_driver_api::{
        Secret, SshAuthentication, SshJumpHost, configure_ssh_authentication, validate_ssh_secret,
    };
    let mut settings = tunnel();
    settings.authentication = SshAuthentication::PublicKey;
    settings.identity_file = Some("/tmp/unencrypted-key".into());
    settings.options.jump_hosts.push(SshJumpHost {
        id: None,
        authentication: Default::default(),
        identity_source: Default::default(),
        identity_file: None,
        agent_socket: None,
        known_hosts_file: None,
        host: "jump.example".into(),
        port: 22,
        user: "jump".into(),
    });
    let empty = Secret::new("");
    assert!(validate_ssh_secret(&settings, Some(&empty)).is_ok());
    let mut command = tokio::process::Command::new("ssh");
    assert!(
        configure_ssh_authentication(&mut command, &settings, Some(&empty))
            .unwrap()
            .is_none()
    );
    assert!(
        command
            .as_std()
            .get_args()
            .any(|arg| arg == "BatchMode=yes")
    );
    assert_eq!(
        command
            .as_std()
            .get_envs()
            .find(|(name, _)| *name == "CHOSCORDB_SSH_ASKPASS_TOKEN")
            .map(|(_, value)| value),
        Some(None),
        "a cleared key passphrase must also remove any inherited broker token"
    );
    assert!(validate_ssh_secret(&settings, Some(&Secret::new("nonempty"))).is_err());
    settings.authentication = SshAuthentication::Password;
    settings.identity_file = None;
    assert!(validate_ssh_secret(&settings, Some(&empty)).is_err());
}

#[test]
fn remote_forwarding_overrides_are_persisted_independently() {
    for options in [
        serde_json::json!({"remote_host":"[::1]"}),
        serde_json::json!({"remote_port":15432}),
        serde_json::json!({"remote_host":"db.internal", "remote_port":15432}),
    ] {
        let mut settings = tunnel();
        settings.options =
            serde_json::from_value(options.clone()).expect("remote forwarding settings");
        settings.validate().unwrap();
        let stored = serde_json::to_value(&settings.options).unwrap();
        for (key, value) in options.as_object().unwrap() {
            assert_eq!(&stored[key], value);
        }
    }
}

#[test]
fn remote_forwarding_rejects_invalid_hosts_and_zero_ports() {
    for options in [
        serde_json::json!({"remote_host":""}),
        serde_json::json!({"remote_host":"db:5432"}),
        serde_json::json!({"remote_host":"[not-ip]"}),
        serde_json::json!({"remote_host":"db\nother"}),
        serde_json::json!({"remote_host":"/tmp/socket"}),
        serde_json::json!({"remote_port":0}),
    ] {
        let mut settings = tunnel();
        settings.options = serde_json::from_value(options.clone()).unwrap();
        assert!(settings.validate().is_err(), "accepted {options}");
    }
}

#[test]
fn forwarding_destination_preserves_independent_defaults_and_formats_ipv6() {
    let mut settings = tunnel();
    assert_eq!(
        settings
            .options
            .forwarding_destination("tls.example", 5432)
            .unwrap(),
        "tls.example:5432"
    );
    settings.options.remote_host = Some("[::1]".into());
    assert_eq!(
        settings
            .options
            .forwarding_destination("tls.example", 5432)
            .unwrap(),
        "[::1]:5432"
    );
    settings.options.remote_port = Some(15432);
    assert_eq!(
        settings
            .options
            .forwarding_destination("tls.example", 5432)
            .unwrap(),
        "[::1]:15432"
    );
    settings.options.remote_host = None;
    assert_eq!(
        settings
            .options
            .forwarding_destination("tls.example", 5432)
            .unwrap(),
        "tls.example:15432"
    );
    assert_eq!(settings.host, "ssh.example");
}
