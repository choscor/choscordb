use choscordb_driver_api::*;

#[test]
fn history_timestamps_accept_seconds_with_up_to_six_fraction_digits() {
    for text in [
        "2026-10-26 13:45:00",
        "2026-10-26 13:45:00.5",
        "1970-01-01 00:00:00.000001",
        "2024-02-29 23:59:59.999999",
    ] {
        assert_eq!(HistoryTimestamp::parse(text).unwrap().as_str(), text);
    }
}

#[test]
fn history_timestamps_reject_other_shapes_and_impossible_fields() {
    for text in [
        "",
        "2026-10-26",
        "2026-10-26T13:45:00",
        "2026-10-26 13:45",
        "2026-10-26 13:45:00.",
        "2026-10-26 13:45:00.1234567",
        " 2026-10-26 13:45:00",
        "2026-10-26 13:45:00' OR 1=1 -- ",
        "2026-13-01 00:00:00",
        "2026-00-01 00:00:00",
        "2026-02-30 00:00:00",
        "2025-02-29 00:00:00",
        "2026-10-26 24:00:00",
        "2026-10-26 23:60:00",
        "2026-10-26 23:59:60",
        "２０２６-10-26 13:45:00",
    ] {
        let error = HistoryTimestamp::parse(text).unwrap_err();
        assert_eq!(error.kind, ErrorKind::InvalidInput, "{text:?}");
        assert_eq!(
            error.message, "Enter a time as YYYY-MM-DD HH:MM:SS with up to six fraction digits",
            "{text:?}"
        );
    }
}

#[test]
fn only_system_versioned_relations_browse_history() {
    assert!(relation_subtype_has_history("system_versioned"));
    for subtype in ["", "materialized_view", "foreign_table", "System_versioned"] {
        assert!(!relation_subtype_has_history(subtype), "{subtype}");
    }
}

#[test]
fn current_reads_are_the_default_and_need_no_history() {
    assert_eq!(ObjectDataTime::default(), ObjectDataTime::Current);
    assert!(ObjectDataTime::Current.require_current().is_ok());
    for time in [
        ObjectDataTime::All,
        ObjectDataTime::AsOf(HistoryTimestamp::parse("2026-10-26 13:45:00").unwrap()),
    ] {
        let error = time.require_current().unwrap_err();
        assert_eq!(error.kind, ErrorKind::Unsupported);
        assert_eq!(
            error.message,
            "History browsing is unavailable for this connection"
        );
    }
}
