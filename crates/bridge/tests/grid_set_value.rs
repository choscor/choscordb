use choscordb_bridge::{
    ffi, grid_set_value_policy, grid_temporal_kind_policy, grid_temporal_text_policy,
    set_value_kinds,
};

fn ask(kind: &str, driver: &str, database_type: &str, inserted: bool) -> ffi::GridSetValueDto {
    grid_set_value_policy(ffi::GridSetValueRequestDto {
        kind: kind.into(),
        driver: driver.into(),
        database_type: database_type.into(),
        inserted,
        nullable_known: false,
        nullable: false,
    })
}

#[test]
fn set_value_policy_returns_typed_outcomes_and_errors() {
    let codes: Vec<String> = set_value_kinds()
        .into_iter()
        .map(|kind| kind.code)
        .collect();
    assert_eq!(codes, ["null", "default", "empty", "now"]);
    let now = ask("now", "postgres", "timestamp with time zone", false);
    assert_eq!(
        (now.outcome.as_str(), now.expression.as_str()),
        ("expression", "CURRENT_TIMESTAMP")
    );
    let empty = ask("empty", "mysql", "varchar(10)", false);
    assert_eq!((empty.outcome.as_str(), empty.text.as_str()), ("text", ""));
    assert_eq!(ask("default", "sqlite", "text", true).outcome, "omit");
    let rejected = ask("default", "sqlite", "text", false);
    assert!(rejected.outcome.is_empty());
    assert_eq!(
        rejected.error,
        "SQLite cannot set an existing row to the column default."
    );
    assert_eq!(
        ask("bogus", "sqlite", "text", false).error,
        "Unknown set value choice"
    );
    let not_null = grid_set_value_policy(ffi::GridSetValueRequestDto {
        kind: "null".into(),
        driver: "sqlite".into(),
        database_type: "text".into(),
        inserted: false,
        nullable_known: true,
        nullable: false,
    });
    assert_eq!(not_null.error, "The column does not accept NULL.");
}

#[test]
fn temporal_picker_text_comes_from_the_engine() {
    assert_eq!(grid_temporal_kind_policy("datetime"), "timestamp");
    assert_eq!(grid_temporal_kind_policy("integer"), "");
    let picked = |month| ffi::GridDateTimeDto {
        year: 2026,
        month,
        day: 2,
        hour: 3,
        minute: 4,
        second: 5,
    };
    assert_eq!(
        grid_temporal_text_policy("timestamp", picked(1)).text,
        "2026-01-02 03:04:05"
    );
    assert_eq!(
        grid_temporal_text_policy("date", picked(0)).error,
        "The picked date or time is invalid."
    );
}
