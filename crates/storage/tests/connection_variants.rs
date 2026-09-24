use choscordb_storage::{ConnectionProfile, Storage};
use serde_json::json;

#[test]
fn sqlite_uri_settings_are_validated_before_saving() {
    let mut storage = Storage::in_memory().unwrap();
    for (path, valid) in [
        ("file:/tmp/native.db?mode=ro", true),
        ("file:/tmp/native.db?unknown=1", false),
        ("file:/tmp/native.db?mode=ro&mode=rw", false),
        ("/tmp/literal?unknown=1", true),
    ] {
        let profile: ConnectionProfile = serde_json::from_value(json!({
            "id":"sqlite-uri", "name":"SQLite URI",
            "configuration":{"driver":"sqlite","path":path,"read_only":false}
        }))
        .unwrap();
        assert_eq!(storage.save_profile(&profile).is_ok(), valid, "{path}");
    }
}

fn profile(driver: &str, host: &str, database: &str, user: &str) -> ConnectionProfile {
    serde_json::from_value(json!({
        "id":"variant", "name":"Connection variant", "group_id":null,
        "credential_ref":null,
        "configuration":{"driver":driver,"host":host,"port":5432,
            "database":database,"user":user,"tls":{"mode":"Disable","root_certificate_path":null}}
    }))
    .unwrap()
}

#[test]
fn native_defaults_and_unix_endpoints_survive_profile_storage() {
    for expected in [
        profile("postgres", "localhost", "", "reader"),
        profile("mysql", "localhost", "", ""),
        profile("postgres", "/tmp/pg socket", "postgres", "reader"),
        profile("mysql", "/tmp/mysql socket.sock", "", "root"),
    ] {
        let mut storage = Storage::in_memory().unwrap();
        storage.save_profile(&expected).unwrap();
        assert_eq!(storage.profile("variant").unwrap(), Some(expected));
    }
}

#[test]
fn unix_endpoints_reject_tls_and_ssh_instead_of_ignoring_them() {
    for driver in ["postgres", "mysql"] {
        for security in ["tls", "ssh"] {
            let mut json = serde_json::to_value(profile(driver, "/tmp/socket", "db", "u")).unwrap();
            if security == "tls" {
                json["configuration"]["tls"]["mode"] = json!("VerifyFull");
            } else {
                json["configuration"]["ssh"] = json!({"host":"bastion","port":22,"user":"u","authentication":"agent","identity_file":null});
            }
            let profile = serde_json::from_value(json).unwrap();
            assert!(
                Storage::in_memory()
                    .unwrap()
                    .save_profile(&profile)
                    .is_err()
            );
        }
    }
}
