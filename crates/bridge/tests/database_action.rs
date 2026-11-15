use choscordb_bridge::{database_action_kinds, database_action_plan, ffi};

#[test]
fn kinds_and_plans_come_from_the_engine() {
    let kinds: Vec<_> = database_action_kinds()
        .into_iter()
        .map(|kind| {
            (
                kind.code,
                kind.label,
                [
                    kind.existing,
                    kind.new_name,
                    kind.settings,
                    kind.confirmation,
                ],
            )
        })
        .collect();
    assert_eq!(
        kinds,
        [
            (
                "create".to_string(),
                "New database".to_string(),
                [false, false, true, false]
            ),
            (
                "rename".to_string(),
                "Rename database".to_string(),
                [true, true, false, false]
            ),
            (
                "drop".to_string(),
                "Drop database".to_string(),
                [true, false, false, true]
            ),
        ]
    );
    let action = ffi::DatabaseActionDto {
        driver: "postgres".into(),
        kind: "drop".into(),
        current: "app".into(),
        name: "old".into(),
        confirmation: "old".into(),
        ..Default::default()
    };
    let plan = database_action_plan(&action);
    assert_eq!(
        (plan.sql.as_str(), plan.error.as_str()),
        ("DROP DATABASE \"old\";", "")
    );
    let plan = database_action_plan(&ffi::DatabaseActionDto {
        kind: "move".into(),
        ..action
    });
    assert_eq!(
        (plan.sql.as_str(), plan.error.as_str()),
        ("", "Choose a database action.")
    );
}
