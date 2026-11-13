use choscordb_core::panel_machine::{PanelEvent, PanelKind, PanelState, present, step};
use choscordb_driver_api::{Body, ToastVariant, Tone};

#[test]
fn step_matches_the_spec_transition_table() {
    crate::table::load("panel").assert_step(&PanelState::ALL, &PanelEvent::ALL, step);
}

#[test]
fn presentation_matches_the_spec_table_for_every_panel() {
    let table = crate::table::load("panel");
    for kind in PanelKind::ALL {
        table.assert_present(&PanelState::ALL, |state| present(kind, state, ""));
    }
}

#[test]
fn a_failed_load_shows_a_toast_and_a_placeholder_that_ends_with_a_hint() {
    let failed = present(PanelKind::History, &PanelState::Failed, "disk is full");
    assert_eq!(failed.tone, Tone::Error);
    assert_eq!(failed.body, Body::FailedPlaceholder);
    assert_eq!(
        failed.message,
        "Could not load query history: disk is full. Retry from Query history."
    );
    let toast = failed.toast.unwrap();
    assert_eq!(toast.variant, ToastVariant::Error);
    assert_eq!(toast.text, failed.message);
}

#[test]
fn empty_and_no_match_have_different_texts_for_each_collection() {
    let golden = [
        (
            PanelKind::SavedSql,
            "No saved queries yet.\nSave a query as a SQL file in the default folder to find it here.",
            "No matching saved queries.\nTry a different filter or clear the search.",
        ),
        (
            PanelKind::History,
            "No query history yet.\nRun a query to see it here.",
            "No matching query history.\nTry a different filter or clear the search.",
        ),
        (
            PanelKind::Pins,
            "No pinned objects yet.\nPin a schema or object from its menu.",
            "No matching pinned objects.\nTry a different filter or clear the search.",
        ),
        (
            PanelKind::Objects,
            "No objects to show.\nRefresh to check for schemas and objects.",
            "No matching objects.\nTry a different filter or clear the search.",
        ),
    ];
    for (kind, empty, no_match) in golden {
        assert_eq!(present(kind, &PanelState::Empty, "").message, empty);
        assert_eq!(present(kind, &PanelState::NoMatch, "").message, no_match);
    }
}

#[test]
fn a_filter_never_hides_a_failure() {
    for event in [
        PanelEvent::FilterMatched,
        PanelEvent::FilterMissed,
        PanelEvent::Disconnect,
    ] {
        assert_eq!(step(&PanelState::Failed, &event), Ok(PanelState::Failed));
    }
    assert_eq!(
        step(&PanelState::Ready, &PanelEvent::FilterMissed),
        Ok(PanelState::NoMatch)
    );
    assert_eq!(
        step(&PanelState::Empty, &PanelEvent::FilterMissed),
        Ok(PanelState::Empty)
    );
    assert!(step(&PanelState::Ready, &PanelEvent::LoadFailed).is_err());
}

#[test]
fn a_disconnected_panel_keeps_the_caller_context_or_uses_the_panel_text() {
    assert_eq!(
        present(
            PanelKind::ObjectDetails,
            &PanelState::Disconnected,
            "orders · Connection unavailable."
        )
        .message,
        "orders · Connection unavailable."
    );
    assert_eq!(
        present(PanelKind::Diagram, &PanelState::Disconnected, "").message,
        "Disconnected. Reconnect to load the diagram."
    );
}

#[test]
fn quick_search_loading_says_searching_and_other_panels_say_loading() {
    assert_eq!(
        present(PanelKind::QuickSearch, &PanelState::Loading, "").message,
        "Searching…"
    );
    assert_eq!(
        present(PanelKind::History, &PanelState::Loading, "").message,
        "Loading…"
    );
}

#[test]
fn panel_facts_settle_in_one_order() {
    use choscordb_core::panel_machine::{PanelFacts, PanelWork, settle};
    let ready = PanelFacts {
        loaded: true,
        has_rows: true,
        ..PanelFacts::default()
    };
    let cases = [
        (
            PanelFacts {
                failed: true,
                work: Some(PanelWork::Loading),
                ..ready
            },
            PanelState::Failed,
        ),
        (
            PanelFacts {
                work: Some(PanelWork::Clearing),
                ..ready
            },
            PanelState::Loading,
        ),
        (PanelFacts::default(), PanelState::Idle),
        (ready, PanelState::Ready),
        (
            PanelFacts {
                has_rows: false,
                filtered: true,
                ..ready
            },
            PanelState::NoMatch,
        ),
        (
            PanelFacts {
                has_rows: false,
                ..ready
            },
            PanelState::Empty,
        ),
    ];
    for (facts, expected) in cases {
        assert_eq!(settle(&facts), expected, "{facts:?}");
    }
}

#[test]
fn panel_facts_choose_the_detail_for_work_and_paged_views() {
    use choscordb_core::panel_machine::{PanelFacts, PanelWork, observe};
    let loaded = PanelFacts {
        loaded: true,
        ..PanelFacts::default()
    };
    let message = |facts| {
        observe(PanelKind::History, &facts, "disk is full")
            .1
            .message
    };
    assert_eq!(
        message(PanelFacts {
            work: Some(PanelWork::Clearing),
            ..loaded
        }),
        "Clearing query history…"
    );
    assert_eq!(
        message(PanelFacts {
            work: Some(PanelWork::Saving),
            ..loaded
        }),
        "Saving the query history preference…"
    );
    assert_eq!(
        message(PanelFacts {
            work: Some(PanelWork::Loading),
            ..loaded
        }),
        "Loading…"
    );
    assert_eq!(
        message(PanelFacts {
            paged: true,
            ..loaded
        }),
        "No query history on this page."
    );
    assert_eq!(
        message(loaded),
        "No query history yet.\nRun a query to see it here."
    );
    assert_eq!(
        message(PanelFacts {
            failed: true,
            ..loaded
        }),
        "Could not load query history: disk is full. Retry from Query history."
    );
}

#[test]
fn a_disconnected_panel_settles_before_its_rows_load() {
    use choscordb_core::panel_machine::{PanelFacts, PanelWork, settle};
    let disconnected = PanelFacts {
        disconnected: true,
        ..PanelFacts::default()
    };
    assert_eq!(settle(&disconnected), PanelState::Disconnected);
    assert_eq!(
        settle(&PanelFacts {
            work: Some(PanelWork::Loading),
            ..disconnected
        }),
        PanelState::Loading
    );
    assert_eq!(
        settle(&PanelFacts {
            loaded: true,
            has_rows: true,
            ..PanelFacts::default()
        }),
        PanelState::Ready
    );
}
