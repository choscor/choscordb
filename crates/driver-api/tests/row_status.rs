use choscordb_driver_api::{RetryAction, RowStatusCause, RowStatusKind, row_status};

#[test]
fn loading_rows_show_progress_without_a_retry() {
    for cause in [RowStatusCause::Connecting, RowStatusCause::ChildrenLoading] {
        let status = row_status(cause, "");
        assert_eq!(status.kind, RowStatusKind::Loading);
        assert_eq!(status.retry, RetryAction::None);
        assert!(!status.message.is_empty());
    }
}

#[test]
fn failed_rows_name_the_one_refresh_gesture() {
    let failed = row_status(RowStatusCause::ChildrenFailed, "permission denied");
    assert_eq!(failed.kind, RowStatusKind::Failed);
    assert_eq!(failed.retry, RetryAction::Refresh);
    assert_eq!(
        failed.message,
        "Could not load children: permission denied. Refresh to retry."
    );
    assert_eq!(
        row_status(RowStatusCause::ChildrenFailed, "").message,
        "Could not load children. Refresh to retry."
    );
    assert_eq!(
        row_status(
            RowStatusCause::RevealFailed,
            "The object could not be shown."
        )
        .message,
        "The object could not be shown. Refresh to retry."
    );
    for cause in [
        RowStatusCause::ConnectionFailed,
        RowStatusCause::ConnectionUnavailable,
        RowStatusCause::ConnectionNotReady,
        RowStatusCause::ConnectionDeselected,
    ] {
        let status = row_status(cause, "");
        assert_eq!(status.kind, RowStatusKind::Failed);
        assert_eq!(status.retry, RetryAction::Refresh);
        assert!(status.message.ends_with(" Refresh to retry."), "{cause:?}");
    }
}

#[test]
fn connection_states_without_a_problem_have_no_icon() {
    for (cause, message) in [
        (RowStatusCause::Connected, "Connected"),
        (RowStatusCause::Hidden, "Hidden"),
        (RowStatusCause::Disconnected, "Disconnected"),
    ] {
        let status = row_status(cause, "");
        assert_eq!(status.kind, RowStatusKind::None);
        assert_eq!(status.retry, RetryAction::None);
        assert_eq!(status.message, message);
    }
    let missing = row_status(RowStatusCause::ObjectUnavailable, "");
    assert_eq!(missing.kind, RowStatusKind::Unavailable);
    assert_eq!(missing.retry, RetryAction::None);
    assert_eq!(missing.message, "Unavailable");
    let moved = row_status(
        RowStatusCause::ObjectUnavailable,
        "The pinned object is no longer in its saved location.",
    );
    assert_eq!(moved.kind, RowStatusKind::Unavailable);
    assert_eq!(
        moved.message,
        "The pinned object is no longer in its saved location."
    );
}

#[test]
fn recovery_hints_are_one_sentence_and_rows_use_them() {
    use choscordb_driver_api::{RecoveryHint, recovery_hint};
    assert_eq!(recovery_hint(RecoveryHint::Retry), "Retry.");
    assert_eq!(
        recovery_hint(RecoveryHint::RefineOrRetry),
        "Refine the text or retry."
    );
    assert_eq!(
        recovery_hint(RecoveryHint::OpenPreferences),
        "Open Preferences to retry."
    );
    for hint in RecoveryHint::ALL {
        let text = recovery_hint(hint);
        assert!(text.ends_with('.'), "{hint:?}: {text}");
        assert_eq!(text.matches(". ").count(), 0, "{hint:?}: {text}");
    }
    let failed = row_status(RowStatusCause::ConnectionFailed, "");
    assert!(
        failed
            .message
            .ends_with(recovery_hint(RecoveryHint::Refresh))
    );
}

#[test]
fn saved_children_never_show_as_current() {
    let loading = row_status(RowStatusCause::ChildrenCached, "");
    assert_eq!(loading.kind, RowStatusKind::Loading);
    assert_eq!(loading.retry, RetryAction::None);
    assert_eq!(
        loading.message,
        "Showing saved children. Loading current children…"
    );
    let failed = row_status(RowStatusCause::ChildrenCachedFailed, "timeout.");
    assert_eq!(failed.kind, RowStatusKind::Failed);
    assert_eq!(failed.retry, RetryAction::Refresh);
    assert_eq!(
        failed.message,
        "Showing saved children. Could not load current children: timeout. Refresh to retry."
    );
    assert_eq!(
        row_status(RowStatusCause::ChildrenCachedFailed, "").message,
        "Showing saved children. Could not load current children. Refresh to retry."
    );
}
