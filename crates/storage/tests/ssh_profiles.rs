use choscordb_storage::{ConnectionProfile, Storage};

fn json() -> serde_json::Value {
    serde_json::json!({
        "id": "ssh", "name": "Remote Postgres", "group_id": null, "credential_ref": null,
        "configuration": {"driver": "postgres", "host": "db.internal", "port": 5433,
            "database": "app", "user": "dbuser",
            "tls": {"mode": "VerifyFull", "root_certificate_path": null},
            "ssh": {"host": "bastion.example", "port": 2222, "user": "operator",
                "identity_file": "/home/operator/.ssh/id_ed25519"}}
    })
}

#[test]
fn ssh_profile_survives_save_reload_and_duplicate() {
    let profile: ConnectionProfile = serde_json::from_value(json()).unwrap();
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("profiles.db");
    Storage::open(&path)
        .unwrap()
        .save_profile(&profile)
        .unwrap();
    let mut storage = Storage::open(&path).unwrap();
    let restored = storage.profile("ssh").unwrap().unwrap();
    assert_eq!(serde_json::to_value(&restored).unwrap(), json());
    let copy = storage.duplicate_profile("ssh", "copy", "Copy").unwrap();
    assert_eq!(copy.configuration, restored.configuration);
}

#[test]
fn legacy_profile_remains_direct_and_ssh_settings_reach_driver() {
    use choscordb_driver_api::ConnectionOptions;
    let mut value = json();
    value["configuration"]
        .as_object_mut()
        .unwrap()
        .remove("ssh");
    let legacy: ConnectionProfile = serde_json::from_value(value).unwrap();
    assert!(matches!(
        legacy.configuration.connection_options(None),
        ConnectionOptions::Postgres { ssh: None, .. }
    ));
    let remote: ConnectionProfile = serde_json::from_value(json()).unwrap();
    let ConnectionOptions::Postgres {
        host,
        port,
        ssh: Some(ssh),
        ..
    } = remote.configuration.connection_options(None)
    else {
        panic!("SSH settings lost")
    };
    assert_eq!((host.as_str(), port), ("db.internal", 5433));
    assert_eq!(
        (
            ssh.host.as_str(),
            ssh.port,
            ssh.user.as_str(),
            ssh.identity_file.as_deref()
        ),
        (
            "bastion.example",
            2222,
            "operator",
            Some("/home/operator/.ssh/id_ed25519")
        )
    );
}

#[test]
fn invalid_ssh_settings_cannot_replace_saved_profile() {
    let profile: ConnectionProfile = serde_json::from_value(json()).unwrap();
    let mut storage = Storage::in_memory().unwrap();
    storage.save_profile(&profile).unwrap();
    for (field, value) in [
        ("host", serde_json::json!("")),
        ("host", serde_json::json!("-oProxyCommand=bad")),
        ("user", serde_json::json!("user\nother")),
        ("port", serde_json::json!(0)),
        ("identity_file", serde_json::json!("")),
    ] {
        let mut invalid = json();
        invalid["configuration"]["ssh"][field] = value;
        let invalid: ConnectionProfile = serde_json::from_value(invalid).unwrap();
        assert!(
            storage.save_profile(&invalid).is_err(),
            "accepted invalid {field}"
        );
        assert_eq!(storage.profile("ssh").unwrap(), Some(profile.clone()));
    }
}
