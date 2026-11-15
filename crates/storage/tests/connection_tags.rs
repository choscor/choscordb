use choscordb_storage::{
    ConnectionColor, ConnectionEnvironment, ConnectionProfile, ConnectionTone, ExecutionGate,
    SafetyLevel, Storage,
};
use serde_json::json;

fn saved_v020() -> serde_json::Value {
    json!({
        "id":"prod", "name":"Production", "group_id":null, "credential_ref":null,
        "configuration":{"driver":"sqlite","path":"/tmp/app.db","read_only":false}
    })
}

#[test]
fn profiles_from_v020_load_with_default_tag_color_and_safety() {
    let profile: ConnectionProfile = serde_json::from_value(saved_v020()).unwrap();
    assert_eq!(profile.environment, ConnectionEnvironment::None);
    assert_eq!(profile.environment_label, None);
    assert_eq!(profile.color, ConnectionColor::Automatic);
    assert_eq!(profile.safety, SafetyLevel::ConfirmWrites);
    assert_eq!(profile.tone(), None);
    assert_eq!(serde_json::to_value(&profile).unwrap(), saved_v020());
}

#[test]
fn tags_round_trip_and_resolve_a_semantic_tone() {
    let mut storage = Storage::in_memory().unwrap();
    let mut profile: ConnectionProfile = serde_json::from_value(saved_v020()).unwrap();
    profile.environment = ConnectionEnvironment::Production;
    profile.safety = SafetyLevel::ReadOnly;
    storage.save_profile(&profile).unwrap();
    let loaded = storage.profiles().unwrap().remove(0);
    assert_eq!(loaded, profile);
    assert_eq!(loaded.tone(), Some(ConnectionTone::Danger));
    assert_eq!(loaded.tag_label().as_deref(), Some("Production"));
    profile.color = ConnectionColor::Blue;
    assert_eq!(profile.tone(), Some(ConnectionTone::Brand));
    for (environment, tone) in [
        (ConnectionEnvironment::Local, ConnectionTone::Neutral),
        (ConnectionEnvironment::Development, ConnectionTone::Success),
        (ConnectionEnvironment::Testing, ConnectionTone::Success),
        (ConnectionEnvironment::Staging, ConnectionTone::Warning),
    ] {
        profile.environment = environment;
        profile.color = ConnectionColor::Automatic;
        assert_eq!(profile.tone(), Some(tone), "{environment:?}");
    }
    profile.environment = ConnectionEnvironment::None;
    profile.color = ConnectionColor::Green;
    assert_eq!(profile.tone(), Some(ConnectionTone::Success));
    assert_eq!(profile.tag_label(), None);
}

#[test]
fn a_custom_environment_needs_a_short_label() {
    let mut storage = Storage::in_memory().unwrap();
    let mut profile: ConnectionProfile = serde_json::from_value(saved_v020()).unwrap();
    profile.environment = ConnectionEnvironment::Custom;
    assert!(storage.save_profile(&profile).is_err());
    profile.environment_label = Some("x".repeat(33));
    assert!(storage.save_profile(&profile).is_err());
    profile.environment_label = Some("QA-2".into());
    storage.save_profile(&profile).unwrap();
    assert_eq!(profile.tag_label().as_deref(), Some("QA-2"));
    assert_eq!(profile.tone(), Some(ConnectionTone::Brand));
    profile.environment = ConnectionEnvironment::Staging;
    assert!(storage.save_profile(&profile).is_err());
}

#[test]
fn codes_match_the_stored_names() {
    for value in ConnectionEnvironment::ALL {
        assert_eq!(serde_json::to_value(value).unwrap(), value.code());
        assert_eq!(ConnectionEnvironment::from_code(value.code()), Some(value));
    }
    for value in ConnectionColor::ALL {
        assert_eq!(serde_json::to_value(value).unwrap(), value.code());
        assert_eq!(ConnectionColor::from_code(value.code()), Some(value));
    }
    for value in SafetyLevel::ALL {
        assert_eq!(serde_json::to_value(value).unwrap(), value.code());
        assert_eq!(SafetyLevel::from_code(value.code()), Some(value));
    }
    assert_eq!(SafetyLevel::from_code("unknown"), None);
}

#[test]
fn safety_levels_gate_statements() {
    let cases = [
        (
            SafetyLevel::Silent,
            [ExecutionGate::Run, ExecutionGate::Run, ExecutionGate::Run],
        ),
        (
            SafetyLevel::ConfirmWrites,
            [
                ExecutionGate::Run,
                ExecutionGate::Run,
                ExecutionGate::Confirm,
            ],
        ),
        (
            SafetyLevel::ConfirmAll,
            [
                ExecutionGate::Confirm,
                ExecutionGate::Confirm,
                ExecutionGate::Confirm,
            ],
        ),
        (
            SafetyLevel::ReadOnly,
            [
                ExecutionGate::Run,
                ExecutionGate::Reject,
                ExecutionGate::Reject,
            ],
        ),
    ];
    for (level, [read, write, destructive]) in cases {
        assert_eq!(level.gate(true, false), read, "{level:?} read");
        assert_eq!(level.gate(false, false), write, "{level:?} write");
        assert_eq!(
            level.gate(false, true),
            destructive,
            "{level:?} destructive"
        );
    }
    assert_eq!(
        SafetyLevel::ConfirmWrites.label(),
        "Confirm destructive statements"
    );
}
