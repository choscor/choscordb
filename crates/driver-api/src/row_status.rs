//! The status of a sidebar row: the icon kind, the retry gesture and the message.

/// The icon that a row shows after its name.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RowStatusKind {
    None,
    Loading,
    Failed,
    Unavailable,
}

/// The gesture that tries a failed operation again.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RetryAction {
    None,
    /// The row's Refresh action, or collapse and expand.
    Refresh,
}

/// Why a row has a status.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RowStatusCause {
    Connecting,
    ChildrenLoading,
    /// The detail is the metadata error.
    ChildrenFailed,
    ConnectionFailed,
    ConnectionUnavailable,
    ConnectionNotReady,
    ConnectionDeselected,
    /// The detail is a sentence that tells why the pinned object was not found.
    RevealFailed,
    /// The optional detail is a sentence that tells why the object is unavailable.
    ObjectUnavailable,
    Connected,
    Hidden,
    Disconnected,
    /// Saved children show while the current children load.
    ChildrenCached,
    /// Saved children stay after the current children failed. The detail is the error.
    ChildrenCachedFailed,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct RowStatus {
    pub kind: RowStatusKind,
    pub retry: RetryAction,
    pub message: String,
}

/// How long an info notice stays on the main panel bottom line.
pub const PANEL_NOTICE_MILLISECONDS: u32 = 6_000;

/// The sentence that tells the user how to recover from a failure. A message ends with it.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RecoveryHint {
    Retry,
    RetryQuery,
    /// For an incomplete search.
    RefineOrRetry,
    /// For a failed sidebar row.
    Refresh,
    ActivatePin,
    RetryFromNavigator,
    RetryFromHistory,
    OpenPreferences,
    RetryOrResetAppearance,
    RetryAppearanceBeforeSave,
    RestoreConnection,
    RefreshNavigator,
    CheckConnection,
    /// For an action with an unknown outcome.
    ReconnectAndRefresh,
    RetryRecovery,
    RetryConnection,
    /// After the SSH settings changed during an approval.
    InspectSshServer,
    /// After the user approved an SSH host key.
    UseApprovedKey,
    RetryApproval,
    /// After the connection of a document closed.
    ReconnectManually,
    /// After an object changed while its connection closed.
    ReconnectAndRefreshNavigator,
}

impl RecoveryHint {
    pub const ALL: [Self; 21] = [
        Self::Retry,
        Self::RetryQuery,
        Self::RefineOrRetry,
        Self::Refresh,
        Self::ActivatePin,
        Self::RetryFromNavigator,
        Self::RetryFromHistory,
        Self::OpenPreferences,
        Self::RetryOrResetAppearance,
        Self::RetryAppearanceBeforeSave,
        Self::RestoreConnection,
        Self::RefreshNavigator,
        Self::CheckConnection,
        Self::ReconnectAndRefresh,
        Self::RetryRecovery,
        Self::RetryConnection,
        Self::InspectSshServer,
        Self::UseApprovedKey,
        Self::RetryApproval,
        Self::ReconnectManually,
        Self::ReconnectAndRefreshNavigator,
    ];
}

pub fn recovery_hint(hint: RecoveryHint) -> &'static str {
    match hint {
        RecoveryHint::Retry => "Retry.",
        RecoveryHint::RetryQuery => "Retry the query.",
        RecoveryHint::RefineOrRetry => "Refine the text or retry.",
        RecoveryHint::Refresh => "Refresh to retry.",
        RecoveryHint::ActivatePin => "Activate the pin to retry.",
        RecoveryHint::RetryFromNavigator => "Retry from the navigator.",
        RecoveryHint::RetryFromHistory => "Retry from Query history.",
        RecoveryHint::OpenPreferences => "Open Preferences to retry.",
        RecoveryHint::RetryOrResetAppearance => "Retry or reset the appearance in Preferences.",
        RecoveryHint::RetryAppearanceBeforeSave => {
            "Retry loading appearance or reset it before saving."
        }
        RecoveryHint::RestoreConnection => "Restore it in the sidebar, then retry.",
        RecoveryHint::RefreshNavigator => "Refresh the navigator and try again.",
        RecoveryHint::CheckConnection => "Check the connection and retry.",
        RecoveryHint::ReconnectAndRefresh => "Reconnect and refresh before retrying.",
        RecoveryHint::RetryRecovery => "Retry recovery or close without saving.",
        RecoveryHint::RetryConnection => "Retry the connection.",
        RecoveryHint::InspectSshServer => "Inspect the current SSH server before retrying.",
        RecoveryHint::UseApprovedKey => "Retry the connection to use it.",
        RecoveryHint::RetryApproval => "Check the selected file or retry approval.",
        RecoveryHint::ReconnectManually => "Reconnect manually.",
        RecoveryHint::ReconnectAndRefreshNavigator => "Reconnect and refresh the navigator.",
    }
}

pub fn row_status(cause: RowStatusCause, detail: &str) -> RowStatus {
    use RetryAction as Retry;
    use RowStatusCause as Cause;
    use RowStatusKind as Kind;
    let detail = detail.trim();
    let (kind, retry, message) = match cause {
        Cause::Connecting => (Kind::Loading, Retry::None, "Connecting…".to_owned()),
        Cause::ChildrenLoading => (Kind::Loading, Retry::None, "Loading children…".to_owned()),
        Cause::ChildrenFailed => (
            Kind::Failed,
            Retry::Refresh,
            could_not("Could not load children", detail),
        ),
        Cause::ChildrenCached => (
            Kind::Loading,
            Retry::None,
            "Showing saved children. Loading current children…".to_owned(),
        ),
        Cause::ChildrenCachedFailed => (
            Kind::Failed,
            Retry::Refresh,
            format!(
                "Showing saved children. {}",
                could_not("Could not load current children", detail)
            ),
        ),
        Cause::ConnectionFailed => failed("Connection failed."),
        Cause::ConnectionUnavailable => failed("Connection unavailable."),
        Cause::ConnectionNotReady => failed("Connection not ready."),
        Cause::ConnectionDeselected => failed("Connection no longer selected."),
        Cause::RevealFailed if detail.is_empty() => return row_status(Cause::ChildrenFailed, ""),
        Cause::RevealFailed => failed(detail),
        Cause::ObjectUnavailable => (
            Kind::Unavailable,
            Retry::None,
            sentence(detail, "Unavailable").to_owned(),
        ),
        Cause::Connected => (Kind::None, Retry::None, "Connected".to_owned()),
        Cause::Hidden => (Kind::None, Retry::None, "Hidden".to_owned()),
        Cause::Disconnected => (Kind::None, Retry::None, "Disconnected".to_owned()),
    };
    RowStatus {
        kind,
        retry,
        message,
    }
}

/// The failure, its reason when there is one, and the Refresh hint.
fn could_not(failure: &str, detail: &str) -> String {
    let hint = recovery_hint(RecoveryHint::Refresh);
    match detail.trim_end_matches('.') {
        "" => format!("{failure}. {hint}"),
        reason => format!("{failure}: {reason}. {hint}"),
    }
}

fn failed(reason: &str) -> (RowStatusKind, RetryAction, String) {
    (
        RowStatusKind::Failed,
        RetryAction::Refresh,
        format!("{reason} {}", recovery_hint(RecoveryHint::Refresh)),
    )
}

fn sentence<'a>(detail: &'a str, fallback: &'a str) -> &'a str {
    if detail.is_empty() { fallback } else { detail }
}
