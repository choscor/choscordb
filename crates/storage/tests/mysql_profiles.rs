use choscordb_storage::{ConnectionProfile, Storage};

fn profile_json() -> serde_json::Value {
    serde_json::json!({"id":"mysql", "name":"MySQL", "group_id":null,
        "credential_ref":"vault-item", "configuration":{"driver":"mysql",
        "host":"db.example", "port":3307, "database":"inventory", "user":"reader",
        "tls":{"mode":"VerifyFull", "root_certificate_path":"/certs/root.pem"}}})
}

#[test]
fn mysql_profile_survives_save_reload_and_duplicate() {
    let profile: ConnectionProfile = serde_json::from_value(profile_json()).unwrap();
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("profiles.db");
    Storage::open(&path)
        .unwrap()
        .save_profile(&profile)
        .unwrap();
    let mut storage = Storage::open(&path).unwrap();
    assert_eq!(
        serde_json::to_value(storage.profile("mysql").unwrap().unwrap()).unwrap(),
        profile_json()
    );
    assert_eq!(
        storage
            .duplicate_profile("mysql", "copy", "Copy")
            .unwrap()
            .configuration,
        profile.configuration
    );
}

#[test]
fn mysql_options_preserve_endpoint_tls_and_transient_secret() {
    use choscordb_driver_api::{ConnectionOptions, Secret, TlsMode};
    let profile: ConnectionProfile = serde_json::from_value(profile_json()).unwrap();
    let ConnectionOptions::Mysql {
        host,
        port,
        database,
        user,
        password,
        tls,
        root_certificate,
        ssh: _,
        ssh_secret: _,
    } = profile
        .configuration
        .connection_options(Some(Secret::new("transient")), None)
    else {
        panic!("wrong driver")
    };
    assert_eq!(
        (host.as_str(), port, database.as_str(), user.as_str()),
        ("db.example", 3307, "inventory", "reader")
    );
    assert_eq!(password.unwrap().expose(), "transient");
    assert_eq!(tls, TlsMode::VerifyFull);
    assert_eq!(root_certificate.unwrap().to_str(), Some("/certs/root.pem"));
    assert!(
        !serde_json::to_string(&profile)
            .unwrap()
            .contains("transient")
    );
}

#[test]
fn invalid_mysql_settings_cannot_replace_saved_profile() {
    let profile: ConnectionProfile = serde_json::from_value(profile_json()).unwrap();
    let mut storage = Storage::in_memory().unwrap();
    storage.save_profile(&profile).unwrap();
    for (field, value) in [
        ("host", serde_json::json!("")),
        ("port", serde_json::json!(0)),
        ("database", serde_json::json!("")),
        ("user", serde_json::json!("\u{0000}")),
    ] {
        let mut invalid = profile_json();
        invalid["configuration"][field] = value;
        let invalid: ConnectionProfile = serde_json::from_value(invalid).unwrap();
        assert!(storage.save_profile(&invalid).is_err());
        assert_eq!(storage.profile("mysql").unwrap(), Some(profile.clone()));
    }
}

#[test]
fn mysql_ssh_profile_round_trips_and_validates() {
    let mut json = profile_json();
    json["configuration"]["ssh"] = serde_json::json!({"host":"bastion.example","port":2222,"user":"tunnel","authentication":"agent","identity_file":null});
    let profile: ConnectionProfile =
        serde_json::from_value(json.clone()).expect("MySQL SSH profile");
    let mut storage = Storage::in_memory().unwrap();
    storage.save_profile(&profile).unwrap();
    assert_eq!(
        serde_json::to_value(storage.profile("mysql").unwrap().unwrap()).unwrap(),
        json
    );
    json["configuration"]["ssh"]["host"] = serde_json::json!("-unsafe");
    let invalid: ConnectionProfile = serde_json::from_value(json).unwrap();
    assert!(storage.save_profile(&invalid).is_err());
}
