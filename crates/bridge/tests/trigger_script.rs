use choscordb_bridge::{ffi, trigger_script, trigger_script_kinds};

#[test]
fn trigger_script_kinds_and_scripts_come_from_the_engine() {
    let kinds: Vec<_> = trigger_script_kinds()
        .into_iter()
        .map(|kind| (kind.code, kind.label, kind.needs_trigger))
        .collect();
    assert_eq!(
        kinds,
        [
            ("create".to_string(), "New trigger".to_string(), false),
            ("edit".to_string(), "Edit trigger".to_string(), true),
            ("drop".to_string(), "Drop trigger".to_string(), true),
        ]
    );
    let request = ffi::TriggerScriptDto {
        driver: "sqlite".into(),
        kind: "drop".into(),
        table_id: r#"["main","orders"]"#.into(),
        table_name: "main.orders".into(),
        trigger: "guard".into(),
        definition: String::new(),
    };
    let plan = trigger_script(&request);
    assert_eq!(
        (plan.sql.as_str(), plan.error.as_str()),
        ("DROP TRIGGER \"main\".\"guard\";", "")
    );
    let unknown = trigger_script(&ffi::TriggerScriptDto {
        kind: "rename".into(),
        ..request
    });
    assert_eq!(
        (unknown.sql.as_str(), unknown.error.as_str()),
        ("", "Choose a trigger action.")
    );
}
