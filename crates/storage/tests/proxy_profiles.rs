use choscordb_storage::{ConnectionProfile, Storage};
fn profile() -> ConnectionProfile {
    serde_json::from_value(serde_json::json!({"id":"proxy","name":"Proxy","group_id":null,"credential_ref":null,"proxy_credential_ref":"opaque-proxy-secret","configuration":{"driver":"postgres","host":"localhost","port":5432,"user":"alice","database":"","tls":{"mode":"VerifyFull","root_certificate_path":null},"proxy":{"host":"proxy.example","username":"proxy-user"}}})).unwrap()
}
#[test]
fn proxy_roundtrips_but_duplicate_drops_secret_reference() {
    let mut storage = Storage::in_memory().unwrap();
    let original = profile();
    storage.save_profile(&original).unwrap();
    assert_eq!(storage.profile("proxy").unwrap().unwrap(), original);
    let duplicate = storage.duplicate_profile("proxy", "copy", "Copy").unwrap();
    assert!(duplicate.proxy_credential_ref.is_none());
    assert_eq!(duplicate.configuration, original.configuration);
    storage.delete_profile("proxy").unwrap();
    assert!(
        storage
            .pending_credential_cleanup()
            .unwrap()
            .contains(&"opaque-proxy-secret".to_owned())
    );
}
#[test]
fn proxy_rejects_incompatible_tunnel() {
    let mut value = serde_json::to_value(profile()).unwrap();
    value["configuration"]["ssh"] = serde_json::json!({"host":"ssh.example","port":22,"user":"ssh-user","authentication":"agent"});
    assert!(
        serde_json::from_value::<ConnectionProfile>(value)
            .unwrap()
            .validate()
            .is_err()
    );
}
