use choscordb_storage::{ConnectionProfile, Storage};

#[test]
fn inline_key_references_roundtrip_without_key_text_and_duplicate_drops_ownership() {
    let value = serde_json::json!({
        "id":"inline", "name":"Inline keys", "group_id":null,
        "credential_ref":null, "ssh_private_key_ref":"opaque-target-key",
        "ssh_jump_private_key_refs":{"hop":"opaque-hop-key"},
        "configuration":{
            "driver":"postgres", "host":"localhost", "port":5432,
            "user":"tester", "database":"db",
            "tls":{"mode":"VerifyFull","root_certificate_path":null},
            "ssh":{"host":"target.example","port":22,"user":"target",
                "authentication":"public_key", "identity_source":"inline",
                "options":{"jump_hosts":[{"id":"hop","host":"jump.example",
                    "port":22,"user":"jump","authentication":"public_key",
                    "identity_source":"inline"}]}}
        }
    });
    let profile: ConnectionProfile = serde_json::from_value(value).unwrap();
    let mut storage = Storage::in_memory().unwrap();
    storage.save_profile(&profile).unwrap();
    let saved = storage.profile("inline").unwrap().unwrap();
    assert_eq!(saved, profile);
    let duplicate = storage.duplicate_profile("inline", "copy", "Copy").unwrap();
    let copy = serde_json::to_value(&duplicate).unwrap();
    assert!(copy.get("ssh_private_key_ref").is_none());
    assert!(copy.get("ssh_jump_private_key_refs").is_none());
    assert_eq!(duplicate.configuration, profile.configuration);
    storage.delete_profile("inline").unwrap();
    let pending = storage.pending_credential_cleanup().unwrap();
    assert!(pending.contains(&"opaque-target-key".to_owned()));
    assert!(pending.contains(&"opaque-hop-key".to_owned()));
}
